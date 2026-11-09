import io
import json
import os
import threading
import time

from pinsel_sidecar.protocol import Job, OutOfMemory, serve


class FakeEngine:
    name = "fake"
    unloads = 0

    def unload(self) -> None:
        self.unloads += 1

    def run(self, job: Job) -> dict:
        request = job.request
        if request["op"] == "oom":
            raise OutOfMemory("CUDA out of memory")
        if request["op"] == "fail" or request.get("model") == "missing":
            raise RuntimeError("broken checkpoint")
        steps = request.get("steps", 3)
        for step in range(steps):
            job.progress("generate", step + 1, steps)
            time.sleep(request.get("delay", 0))
        return {"output": "out.png"}


def run(requests: list[dict], engine: FakeEngine | None = None) -> list[dict]:
    stdin = io.StringIO("".join(json.dumps(r) + "\n" for r in requests))
    stdout = io.StringIO()
    serve(engine or FakeEngine(), stdin, stdout)
    return [json.loads(line) for line in stdout.getvalue().splitlines()]


def test_says_ready_first():
    assert run([]) == [{"event": "ready", "engine": "fake"}]


def test_reports_each_step_then_done():
    events = run([{"op": "generate", "job": 1, "steps": 2}])
    assert events[1:] == [
        {"event": "progress", "job": 1, "stage": "generate", "done": 1, "total": 2},
        {"event": "progress", "job": 1, "stage": "generate", "done": 2, "total": 2},
        {"event": "done", "job": 1, "output": "out.png"},
    ]


def test_out_of_memory_ends_only_that_job():
    events = run([{"op": "oom", "job": 1}, {"op": "generate", "job": 2, "steps": 0}])
    assert events[1] == {"event": "error", "job": 1, "kind": "oom", "message": "CUDA out of memory"}
    assert events[2] == {"event": "done", "job": 2, "output": "out.png"}


def test_a_failure_names_its_cause():
    events = run([{"op": "fail", "job": 7}])
    assert events[1] == {"event": "error", "job": 7, "kind": "failed", "message": "RuntimeError: broken checkpoint"}


def test_a_bad_line_is_an_error_without_a_job():
    stdout = io.StringIO()
    serve(FakeEngine(), io.StringIO("not json\n"), stdout)
    event = json.loads(stdout.getvalue().splitlines()[1])
    assert (event["job"], event["kind"]) == (None, "failed")


class Session:
    """serve on a thread, fed through a pipe, so requests can arrive while a job runs."""

    def __init__(self, engine: FakeEngine) -> None:
        read_fd, write_fd = os.pipe()
        self.stdout = io.StringIO()
        self._pinsel = os.fdopen(write_fd, "w")
        self._server = threading.Thread(target=serve, args=(engine, os.fdopen(read_fd, "r"), self.stdout))
        self._server.start()

    def send(self, request: dict) -> None:
        self._pinsel.write(json.dumps(request) + "\n")
        self._pinsel.flush()

    def wait_for_progress(self) -> None:
        while '"progress"' not in self.stdout.getvalue():
            time.sleep(0.01)

    def close(self) -> list[dict]:
        self._pinsel.close()
        self._server.join(timeout=10)
        return [json.loads(line) for line in self.stdout.getvalue().splitlines()]


def test_cancel_stops_the_running_job():
    session = Session(FakeEngine())
    session.send({"op": "generate", "job": 1, "steps": 1000, "delay": 0.01})
    session.wait_for_progress()
    session.send({"op": "cancel", "job": 1})
    assert session.close()[-1] == {"event": "error", "job": 1, "kind": "cancelled", "message": "Cancelled"}


def test_a_cancel_after_the_last_progress_comes_too_late():
    session = Session(FakeEngine())
    session.send({"op": "generate", "job": 1, "steps": 1, "delay": 0.3})
    session.wait_for_progress()
    session.send({"op": "cancel", "job": 1})
    assert session.close()[-1] == {"event": "done", "job": 1, "output": "out.png"}


def test_a_cancelled_queued_load_unloads():
    engine = FakeEngine()
    session = Session(engine)
    session.send({"op": "generate", "job": 1, "steps": 1000, "delay": 0.01})
    session.send({"op": "load", "job": 2, "model": "b.safetensors"})
    session.wait_for_progress()
    session.send({"op": "cancel", "job": 2})
    session.send({"op": "cancel", "job": 1})
    events = session.close()
    assert events[-1] == {"event": "error", "job": 2, "kind": "cancelled", "message": "Cancelled"}
    assert engine.unloads == 1


def test_a_failed_load_unloads_and_a_failed_generate_does_not():
    engine = FakeEngine()
    events = run([{"op": "load", "job": 1, "model": "missing"}, {"op": "oom", "job": 2}], engine)
    assert [e["kind"] for e in events[1:]] == ["failed", "oom"]
    assert engine.unloads == 1


