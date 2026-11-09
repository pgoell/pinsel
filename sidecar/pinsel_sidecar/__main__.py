import io
import os
import sys

from .protocol import serve


def main() -> None:
    # stdout carries only the protocol. Libraries that print go to stderr,
    # which pinsel keeps for the message when the sidecar stops.
    events = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", newline="\n", line_buffering=True)
    requests = io.TextIOWrapper(sys.stdin.buffer, encoding="utf-8")
    sys.stdout = sys.stderr
    # Loads read only local files; this stops any library from reaching the Hub.
    os.environ["HF_HUB_OFFLINE"] = "1"
    from .diffusers_engine import DiffusersEngine

    serve(DiffusersEngine(), requests, events)


if __name__ == "__main__":
    main()
