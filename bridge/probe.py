"""Check a bridge connection from a PC without sending musical commands."""

from __future__ import annotations

import argparse
import json
import os
import socket
import time

from .protocol import DEFAULT_PORT, LineDecoder, ProtocolError, encode_frame


def probe(host: str, port: int, token: str, timeout: float = 3.0) -> list[dict]:
    """Authenticate and ping. Never send MIDI, profile changes or an update."""
    frames = []
    decoder = LineDecoder()
    with socket.create_connection((host, port), timeout=timeout) as connection:
        connection.sendall(encode_frame({"type": "hello", "protocol": 1, "token": token}))
        deadline = time.monotonic() + timeout
        greeted = False
        while time.monotonic() < deadline:
            connection.settimeout(max(0.01, deadline - time.monotonic()))
            data = connection.recv(4096)
            if not data:
                raise ProtocolError("Bridge closed before the check completed")
            for frame in decoder.feed(data):
                if frame.get("type") == "error":
                    raise ProtocolError(str(frame.get("message", "Bridge rejected connection")))
                frames.append(frame)
                if frame.get("type") == "hello" and not greeted:
                    if frame.get("protocol") != 1 or frame.get("mode") not in ("mock", "midi"):
                        raise ProtocolError("Unexpected bridge protocol or mode")
                    greeted = True
                    connection.sendall(encode_frame({"type": "ping", "id": 1}))
                if greeted and frame.get("type") == "pong" and frame.get("id") == 1:
                    return frames
        raise TimeoutError("Bridge did not answer within the timeout")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--token", default=os.environ.get("FM1_CYD_TOKEN"), required=not os.environ.get("FM1_CYD_TOKEN"))
    args = parser.parse_args(argv)
    try:
        for frame in probe(args.host, args.port, args.token):
            print(json.dumps(frame, separators=(",", ":")))
        print("Bridge authentication and ping passed. No MIDI was sent.")
        return 0
    except (OSError, ValueError, TimeoutError) as error:
        print(f"Bridge check failed: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
