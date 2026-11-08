import io
import sys

from .protocol import serve


def main() -> None:
    # stdout carries only the protocol. Libraries that print go to stderr,
    # which pinsel keeps for the message when the sidecar stops.
    events = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", newline="\n", line_buffering=True)
    requests = io.TextIOWrapper(sys.stdin.buffer, encoding="utf-8")
    sys.stdout = sys.stderr
    from .diffusers_engine import DiffusersEngine

    serve(DiffusersEngine(), requests, events)


if __name__ == "__main__":
    main()
