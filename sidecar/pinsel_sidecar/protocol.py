"""The engine protocol: JSON Lines between pinsel and an engine process.

pinsel writes one request per line to stdin and reads one event per line from
stdout. Nothing here knows about torch, so any engine can stand behind it.
"""

import json
import queue
import threading
from typing import Any, Protocol, TextIO


class Cancelled(Exception):
    """pinsel cancelled the job."""


class OutOfMemory(Exception):
    """The job ran out of GPU memory, and the engine freed what it held."""


class Job:
    def __init__(self, request: dict[str, Any], send) -> None:
        self.id = request["job"]
        self.request = request
        self._send = send
        self.cancelled = threading.Event()

    def progress(self, stage: str, done: int, total: int) -> None:
        """Reports progress, and raises Cancelled once pinsel has cancelled the job."""
        if self.cancelled.is_set():
            raise Cancelled
        self._send({"event": "progress", "job": self.id, "stage": stage, "done": done, "total": total})


class Engine(Protocol):
    name: str

    def run(self, job: Job) -> dict[str, Any]:
        """Runs job.request and returns the fields of its done event."""
        ...

    def unload(self) -> None:
        """Lets go of the model and frees its memory."""
        ...


def serve(engine: Engine, stdin: TextIO, stdout: TextIO) -> None:
    """Runs jobs one at a time, in order, until stdin closes.

    A thread reads stdin, so a cancel reaches the job that is running.
    """
    lock = threading.Lock()

    def send(event: dict[str, Any]) -> None:
        with lock:
            stdout.write(json.dumps(event) + "\n")
            stdout.flush()

    jobs: queue.Queue[Job | None] = queue.Queue()
    open_jobs: dict[Any, Job] = {}

    def read() -> None:
        for line in stdin:
            if not line.strip():
                continue
            try:
                request = json.loads(line)
                op = request["op"]
                job_id = request["job"]
            except (ValueError, KeyError, TypeError) as error:
                send({"event": "error", "job": None, "kind": "failed", "message": f"Bad request: {error}"})
                continue
            if op == "cancel":
                job = open_jobs.get(job_id)
                if job is not None:
                    job.cancelled.set()
            else:
                job = Job(request, send)
                open_jobs[job_id] = job
                jobs.put(job)
        jobs.put(None)

    send({"event": "ready", "engine": engine.name})
    threading.Thread(target=read, daemon=True).start()
    while (job := jobs.get()) is not None:
        try:
            if job.cancelled.is_set():
                raise Cancelled
            # A cancel that comes after the last progress report is too late:
            # the job is done.
            result = engine.run(job)
            send({"event": "done", "job": job.id, **result})
            continue
        except Cancelled:
            error_event = {"kind": "cancelled", "message": "Cancelled"}
        except OutOfMemory as error:
            error_event = {"kind": "oom", "message": str(error) or "Out of GPU memory"}
        except Exception as error:  # noqa: BLE001 - any failure ends only this job
            error_event = {"kind": "failed", "message": f"{type(error).__name__}: {error}"}
        finally:
            open_jobs.pop(job.id, None)
        # pinsel takes a load that did not finish to leave no model.
        if job.request["op"] == "load":
            engine.unload()
        send({"event": "error", "job": job.id, **error_event})
