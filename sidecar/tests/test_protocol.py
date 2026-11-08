import io
import json
import os
import threading
import time

from pinsel_sidecar.protocol import Job, OutOfMemory, serve


class FakeEngine:
    name = "fake"

    def run(self, job: Job) -> dict:
        request = job.request
        if request["op"] == "oom":
            raise OutOfMemory("CUDA out of memory")
        if request["op"] == "fail":
            raise RuntimeError("broken checkpoint")
        steps = request.get("steps", 3)
        for step in range(steps):
            job.progress("generate", step + 1, steps)
            time.sleep(request.get("delay", 0))
        return {"output": "out.png"}


def run(requests: list[dict]) -> list[dict]:
    stdin = io.StringIO("".join(json.dumps(r) + "\n" for r in requests))
    stdout = io.StringIO()
    serve(FakeEngine(), stdin, stdout)
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


def test_cancel_stops_the_running_job():
    read_fd, write_fd = os.pipe()
    stdin = os.fdopen(read_fd, "r")
    pinsel = os.fdopen(write_fd, "w")
    stdout = io.StringIO()
    server = threading.Thread(target=serve, args=(FakeEngine(), stdin, stdout))
    server.start()
    pinsel.write(json.dumps({"op": "generate", "job": 1, "steps": 1000, "delay": 0.01}) + "\n")
    pinsel.flush()
    while '"progress"' not in stdout.getvalue():
        time.sleep(0.01)
    pinsel.write(json.dumps({"op": "cancel", "job": 1}) + "\n")
    pinsel.close()
    server.join(timeout=10)
    last = json.loads(stdout.getvalue().splitlines()[-1])
    assert last == {"event": "error", "job": 1, "kind": "cancelled", "message": "Cancelled"}

