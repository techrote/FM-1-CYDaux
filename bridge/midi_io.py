"""Small, explicit MIDI endpoints; no default-port selection or replay queue."""

from __future__ import annotations

from collections import deque
from importlib import import_module
from queue import Empty, Full, Queue
from typing import Any, Callable, Iterable


MAX_MIDI_BYTES = 256
MAX_INPUT_MESSAGES = 128


class EndpointUnavailable(RuntimeError):
    """The selected MIDI endpoint cannot accept a message now."""


def validate_midi_bytes(data: Iterable[int]) -> tuple[int, ...]:
    """Validate one complete MIDI message without importing Mido.

    Running status and interleaved real-time bytes belong in a byte-stream
    parser, not this message API. SysEx is deliberately bounded.
    """
    values = tuple(data)
    if not values or len(values) > MAX_MIDI_BYTES:
        raise ValueError(f"MIDI message must contain 1..{MAX_MIDI_BYTES} bytes")
    if any(type(value) is not int or not 0 <= value <= 255 for value in values):
        raise ValueError("MIDI bytes must be integers in 0..255")
    status = values[0]
    if status < 0x80:
        raise ValueError("MIDI running status is not accepted")
    if status == 0xF0:
        if len(values) < 2 or values[-1] != 0xF7:
            raise ValueError("SysEx must end with F7")
        if any(value > 127 for value in values[1:-1]):
            raise ValueError("SysEx data bytes must be 7-bit")
        return values
    if status < 0xF0:
        expected = 2 if status & 0xF0 in (0xC0, 0xD0) else 3
    else:
        lengths = {0xF1: 2, 0xF2: 3, 0xF3: 2, 0xF6: 1,
                   0xF8: 1, 0xFA: 1, 0xFB: 1, 0xFC: 1, 0xFE: 1, 0xFF: 1}
        expected = lengths.get(status)
    if expected is None or len(values) != expected:
        raise ValueError("Unsupported status or wrong MIDI message length")
    if any(value > 127 for value in values[1:]):
        raise ValueError("MIDI data bytes must be 7-bit")
    return values


def load_mido() -> Any:
    """Load the optional dependencies with a useful command-line error."""
    try:
        mido = import_module("mido")
        import_module("rtmidi")
    except ImportError as error:
        raise EndpointUnavailable(
            "USB MIDI needs optional dependencies: python -m pip install -r "
            "bridge/requirements.txt (or use --demo without them)"
        ) from error
    mido.set_backend("mido.backends.rtmidi")
    return mido


class MockMidi:
    """A bounded, deterministic MIDI echo for development without an FM-1.

    Echoes are simulated return traffic, not proof of hardware readback.
    """

    def __init__(self, *, echo: bool = True) -> None:
        self.connected = True
        self.detail = "demo: simulated MIDI endpoint"
        self.dropped_input = 0
        self.sent: deque[tuple[int, ...]] = deque(maxlen=MAX_INPUT_MESSAGES)
        self._incoming: deque[tuple[int, ...]] = deque(maxlen=MAX_INPUT_MESSAGES)
        self.echo = echo

    def tick(self, now: float) -> None:
        del now

    def send(self, data: Iterable[int]) -> None:
        if not self.connected:
            raise EndpointUnavailable("Demo endpoint is closed")
        message = validate_midi_bytes(data)
        self.sent.append(message)
        if self.echo:
            self.inject(message)

    def inject(self, data: Iterable[int]) -> None:
        message = validate_midi_bytes(data)
        if len(self._incoming) == self._incoming.maxlen:
            self.dropped_input += 1
        self._incoming.append(message)

    def receive(self, limit: int = MAX_INPUT_MESSAGES) -> list[tuple[int, ...]]:
        return [self._incoming.popleft() for _ in range(min(limit, len(self._incoming)))]

    def close(self) -> None:
        self.connected = False
        self._incoming.clear()


class UsbMidi:
    """Mido/RtMidi endpoint with exact port names and bounded callback ingress.

    All port open/close/list operations happen on the caller's thread. A Mido
    callback only validates and enqueues bytes; it never touches sockets or
    sends MIDI. Failed sends are never kept for later replay.
    """

    def __init__(
        self,
        output_name: str,
        input_name: str | None = None,
        *,
        reconnect_seconds: float = 2.0,
        mido_loader: Callable[[], Any] = load_mido,
    ) -> None:
        if not output_name:
            raise ValueError("An explicit MIDI output port is required")
        if reconnect_seconds < 0.1:
            raise ValueError("Reconnect interval must be at least 0.1 seconds")
        self.output_name = output_name
        self.input_name = input_name
        self.reconnect_seconds = reconnect_seconds
        self.connected = False
        self.detail = "waiting for selected MIDI ports"
        self.dropped_input = 0
        self._mido = mido_loader()
        self._output: Any = None
        self._input: Any = None
        self._incoming: Queue[tuple[int, ...]] = Queue(MAX_INPUT_MESSAGES)
        self._next_probe = 0.0
        self._closed = False

    def _callback(self, message: Any) -> None:
        try:
            data = validate_midi_bytes(message.bytes())
            self._incoming.put_nowait(data)
        except (ValueError, TypeError, Full):
            self.dropped_input += 1

    def _disconnect(self, reason: str) -> None:
        self.connected = False
        self.detail = reason
        for port in (self._input, self._output):
            if port is not None:
                try:
                    port.close()
                except Exception:
                    pass
        self._input = None
        self._output = None
        self.receive()

    def tick(self, now: float) -> None:
        if self._closed or now < self._next_probe:
            return
        self._next_probe = now + self.reconnect_seconds
        try:
            outputs = self._mido.get_output_names()
            inputs = self._mido.get_input_names() if self.input_name else []
            if self.output_name not in outputs:
                raise EndpointUnavailable(f"MIDI output unavailable: {self.output_name}")
            if self.input_name and self.input_name not in inputs:
                raise EndpointUnavailable(f"MIDI input unavailable: {self.input_name}")
            if outputs.count(self.output_name) != 1:
                raise EndpointUnavailable("MIDI output name is ambiguous; select a uniquely named port")
            if self.input_name and inputs.count(self.input_name) != 1:
                raise EndpointUnavailable("MIDI input name is ambiguous; select a uniquely named port")
            if self.connected:
                if self._output.closed or (self._input is not None and self._input.closed):
                    raise EndpointUnavailable("MIDI port closed; waiting to reconnect")
                return
            self._output = self._mido.open_output(self.output_name)
            if self.input_name:
                self._input = self._mido.open_input(self.input_name, callback=self._callback)
            self.connected = True
            self.detail = "selected USB MIDI ports connected"
        except Exception as error:
            self._disconnect(str(error))

    def send(self, data: Iterable[int]) -> None:
        message = validate_midi_bytes(data)
        if not self.connected or self._output is None:
            raise EndpointUnavailable(self.detail)
        try:
            self._output.send(self._mido.Message.from_bytes(list(message)))
        except Exception as error:
            self._disconnect(f"MIDI send failed: {error}")
            raise EndpointUnavailable(self.detail) from error

    def receive(self, limit: int = MAX_INPUT_MESSAGES) -> list[tuple[int, ...]]:
        messages = []
        for _ in range(limit):
            try:
                messages.append(self._incoming.get_nowait())
            except Empty:
                break
        return messages

    def close(self) -> None:
        self._closed = True
        self._disconnect("MIDI endpoint closed")
