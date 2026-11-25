"""Bounded line framing for the shared Wi-Fi bridge v1 contract."""

from __future__ import annotations

import json
from typing import Any


PROTOCOL_VERSION = 1
DEFAULT_PORT = 45821
MAX_LINE_BYTES = 2048


class ProtocolError(ValueError):
    pass


def _object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ProtocolError("Duplicate JSON key")
        result[key] = value
    return result


def _reject_constant(value: str) -> None:
    del value
    raise ProtocolError("Non-finite JSON number")


class LineDecoder:
    """Accept fragmented TCP input without retaining an unbounded line."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, chunk: bytes) -> list[dict[str, Any]]:
        frames = []
        # The server limits each recv to 4096 bytes. Splitting here also permits
        # several individually bounded frames in a single TCP packet.
        for part_index, part in enumerate(chunk.split(b"\n")):
            if part_index:
                try:
                    frame = json.loads(
                        self._buffer.decode("utf-8"),
                        object_pairs_hook=_object,
                        parse_constant=_reject_constant,
                    )
                except (UnicodeError, ValueError, RecursionError) as error:
                    self._buffer.clear()
                    raise ProtocolError("Invalid JSON frame") from error
                self._buffer.clear()
                if not isinstance(frame, dict):
                    raise ProtocolError("A frame must be a JSON object")
                frames.append(frame)
            if len(self._buffer) + len(part) > MAX_LINE_BYTES:
                self._buffer.clear()
                raise ProtocolError("JSON line exceeds 2048 bytes")
            self._buffer.extend(part)
        return frames


def encode_frame(frame: dict[str, Any]) -> bytes:
    result = json.dumps(frame, separators=(",", ":"), ensure_ascii=True, allow_nan=False).encode("ascii")
    if len(result) > MAX_LINE_BYTES:
        raise ProtocolError("Outgoing JSON line exceeds 2048 bytes")
    return result + b"\n"
