"""Single-controller TCP server; musical messages are never replayed."""

from __future__ import annotations

from dataclasses import dataclass, field
import logging
import secrets
import select
import socket
import time
from typing import Any

from .midi_io import EndpointUnavailable, validate_midi_bytes
from .protocol import DEFAULT_PORT, PROTOCOL_VERSION, LineDecoder, ProtocolError, encode_frame


LOG = logging.getLogger("fm1cyd.bridge")
CLIENT_TIMEOUT = 6.0
MAX_OUTGOING_BYTES = 16 * 1024
LEGACY_PROFILES = frozenset(("sloop", "skiff", "melodee", "x0x"))
COMPANION_PROFILES = frozenset(("zaaggenz-native", "xenoresonator-native", "grimoire-native"))
PROFILES = LEGACY_PROFILES | COMPANION_PROFILES
CY1_PREFIX = (0xF0, 0x7D, 0x43, 0x59, 1, 0)
CY1_SIZES = {1: 1, 2: 6, 3: 0, 4: 23, 5: 2, 6: 1, 7: 2, 8: 0, 9: 1}


def valid_cy1_envelope(message: tuple[int, ...]) -> bool:
    return (len(message) >= 15 and message[:6] == CY1_PREFIX
            and len(message) == 15 + message[13]
            and CY1_SIZES.get(message[6]) == message[13])


@dataclass
class Session:
    endpoint: Any
    token: str
    mode: str
    profile: str
    name: str = "Demo FM-1"
    authenticated: bool = False
    closing: bool = False
    channels: set[int] = field(default_factory=set)
    companion_session: int = 0
    companion_sequence: int = 0
    companion_notes: bool = False

    @property
    def companion_epoch(self) -> bool:
        # Profile metadata may be changed after discovery; it must not
        # relinquish the release responsibility of a negotiated session.
        return bool(self.companion_session or self.profile in COMPANION_PROFILES)

    def _observe_companion(self, message: tuple[int, ...]) -> None:
        """Track only the bounded CY1 envelope for loss-of-link emergency panic.

        This does not negotiate capabilities or acknowledge a device. The
        endpoint/controller own those checks. Unmodified Fune ignores CY1.
        """
        if not valid_cy1_envelope(message):
            return
        session = sum(message[7 + i] << (7 * i) for i in range(4))
        sequence = message[11] | message[12] << 7
        if not session:
            return
        if message[6] == 1:
            # Discovery retries return Identity but do not advance the active
            # endpoint's mutation sequence. Never let a retry move the bridge's
            # emergency-release sequence outside the endpoint's forward window.
            if not self.companion_session:
                self.companion_session = session
                self.companion_sequence = sequence
            return
        if session != self.companion_session:
            return
        delta = (sequence - self.companion_sequence) & 0x3FFF
        if delta and delta < 0x2000:
            self.companion_sequence = sequence
        else:
            # Duplicate mutations cannot have been accepted by the endpoint.
            # Do not allow an equal-sequence Panic to cancel Note release duty.
            return
        if message[6] == 7 and message[13] == 2 and message[15] > 0:
            self.companion_notes = True
        elif message[6] == 8 and message[13] == 0:
            self.companion_notes = False

    def handle(self, frame: dict[str, Any]) -> list[dict[str, Any]]:
        kind = frame.get("type")
        if not self.authenticated:
            supplied = frame.get("token")
            if (kind != "hello" or type(frame.get("protocol")) is not int
                    or frame["protocol"] != PROTOCOL_VERSION):
                self.closing = True
                return [{"type": "error", "message": "Expected protocol 1 hello"}]
            try:
                token_matches = isinstance(supplied, str) and secrets.compare_digest(
                    supplied.encode("utf-8"), self.token.encode("utf-8"))
            except UnicodeError:
                token_matches = False
            if not token_matches:
                self.closing = True
                return [{"type": "error", "message": "Authentication failed"}]
            self.authenticated = True
            return [{"type": "hello", "protocol": PROTOCOL_VERSION,
                     "mode": self.mode, "ready": bool(self.endpoint.connected), "name": self.name}]

        try:
            if kind == "ping":
                ping_id = frame.get("id")
                if type(ping_id) is not int or not 0 <= ping_id <= 0xFFFFFFFF:
                    raise ValueError("Ping id must be an integer in 0..4294967295")
                return [{"type": "pong", "id": ping_id}]
            if kind == "profile":
                profile = frame.get("id")
                if not isinstance(profile, str) or profile not in PROFILES:
                    raise ValueError("Unknown profile")
                if self.mode == "mock" and profile not in LEGACY_PROFILES:
                    raise ValueError("Native companion profiles require an actual negotiated endpoint; mock unsupported")
                if self.mode == "mock" and hasattr(self.endpoint, "set_profile"):
                    self.endpoint.set_profile(profile)
                self.profile = profile
                # No MIDI command: this is metadata, never a firmware switch.
                return []
            if kind == "midi":
                data = frame.get("data")
                if not isinstance(data, list):
                    raise ValueError("MIDI data must be an array")
                message = validate_midi_bytes(data)
                # Actual CY1 firmware can consume a forward sequence even if
                # it refuses an unknown operation or wrong payload size.
                # Reject these locally before forwarding so release tracking
                # cannot diverge. Other SysEx namespaces/versions are unchanged.
                if message[:6] == CY1_PREFIX:
                    if not valid_cy1_envelope(message):
                        raise ValueError("Invalid CY1 operation or payload size")
                    # An authenticated TCP lifetime owns exactly one CY1 epoch.
                    # An old firmware lease can outlive a dropped TCP socket:
                    # a forward Note before a new Discover would otherwise
                    # sound without the new socket owning emergency release.
                    session = sum(message[7 + i] << (7 * i) for i in range(4))
                    if not session or (self.companion_session
                                       and session != self.companion_session):
                        raise ValueError("CY1 session differs; reconnect required")
                    if not self.companion_session and message[6] != 1:
                        raise ValueError("CY1 discovery required on this connection")
                self.endpoint.send(message)
                self._observe_companion(message)
                # A CC-only editor session must not silence somebody else's
                # keyboard on disconnect. Release only our note-on channels.
                if message[0] & 0xF0 == 0x90 and message[2] > 0:
                    self.channels.add(message[0] & 15)
                # No acknowledgement: successful send does not prove readback.
                return []
            raise ValueError("Unknown command")
        except (ValueError, TypeError, EndpointUnavailable) as error:
            return [{"type": "error", "message": str(error)[:300]}]

    def release(self) -> None:
        """Release only channels used by this session, without storing retries."""
        if self.companion_session and self.companion_notes:
            sequence = (self.companion_sequence + 1) & 0x3FFF or 1
            panic = (0xF0, 0x7D, 0x43, 0x59, 1, 0, 8,
                     *(self.companion_session >> (7 * i) & 127 for i in range(4)),
                     sequence & 127, sequence >> 7, 0, 0xF7)
            try:
                self.endpoint.send(panic)
            except (ValueError, EndpointUnavailable):
                pass
        self.companion_session = 0
        self.companion_sequence = 0
        self.companion_notes = False
        for channel in sorted(self.channels):
            try:
                self.endpoint.send((0xB0 | channel, 64, 0))
                self.endpoint.send((0xB0 | channel, 123, 0))
            except (ValueError, EndpointUnavailable):
                break
        self.channels.clear()
        self.authenticated = False


@dataclass
class Client:
    socket: socket.socket
    session: Session
    last_frame: float
    decoder: LineDecoder = field(default_factory=LineDecoder)
    outgoing: bytearray = field(default_factory=bytearray)
    close_at: float | None = None


class BridgeServer:
    def __init__(
        self,
        endpoint: Any,
        token: str,
        *,
        bind: str = "127.0.0.1",
        port: int = DEFAULT_PORT,
        mode: str = "mock",
        profile: str = "sloop",
        name: str | None = None,
        clock=time.monotonic,
    ) -> None:
        if mode not in ("mock", "midi") or profile not in PROFILES:
            raise ValueError("Unknown bridge mode or profile")
        if mode == "mock" and profile not in LEGACY_PROFILES:
            raise ValueError("Native companion profiles require an actual negotiated endpoint; mock unsupported")
        if not token or len(token) > 128:
            raise ValueError("Token must contain 1..128 characters")
        self.endpoint = endpoint
        self.token = token
        self.mode = mode
        self.profile = profile
        self.name = name or ("Demo FM-1" if mode == "mock" else "FM-1 USB MIDI")
        self.clock = clock
        self.client: Client | None = None
        self.closed = False
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.listener.bind((bind, port))
            self.listener.listen(4)
            self.listener.setblocking(False)
        except Exception:
            self.listener.close()
            raise
        self.address = self.listener.getsockname()
        self._backend_state = (bool(endpoint.connected), endpoint.detail)

    def _drop(self, reason: str) -> None:
        client, self.client = self.client, None
        if client is None:
            return
        # No performance bytes are carried into another connection.
        client.outgoing.clear()
        client.session.release()
        try:
            client.socket.close()
        except OSError:
            pass
        LOG.info("Controller disconnected: %s", reason)

    def _queue(self, frame: dict[str, Any]) -> None:
        client = self.client
        if client is None:
            return
        encoded = encode_frame(frame)
        if len(client.outgoing) + len(encoded) > MAX_OUTGOING_BYTES:
            self._drop("controller is not reading; bounded output queue full")
            return
        client.outgoing.extend(encoded)

    def _accept(self, now: float) -> None:
        try:
            connection, _address = self.listener.accept()
        except (BlockingIOError, ConnectionAbortedError):
            return
        try:
            connection.setblocking(False)
            connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        except OSError:
            connection.close()
            return
        if self.client is not None:
            try:
                connection.send(encode_frame({"type": "error", "message": "Another controller is connected"}))
            except OSError:
                pass
            connection.close()
            return
        self.client = Client(connection, Session(
            self.endpoint, self.token, self.mode, self.profile, self.name,
        ), now)

    def _read(self, now: float) -> None:
        client = self.client
        if client is None:
            return
        try:
            chunk = client.socket.recv(4096)
            if not chunk:
                self._drop("connection closed")
                return
            frames = client.decoder.feed(chunk)
            for frame in frames:
                client.last_frame = now
                was_ready = bool(self.endpoint.connected)
                responses = client.session.handle(frame)
                if was_ready and not self.endpoint.connected and client.session.companion_epoch:
                    # A send can retire USB synchronously, halfway through
                    # a TCP batch. Do not forward the remaining old commands,
                    # or leave this authenticated socket alive for a new port.
                    self._drop("companion USB endpoint lost during command; reconnect required")
                    break
                for response in responses:
                    self._queue(response)
                if self.client is None:
                    break
                if client.session.closing:
                    client.close_at = now + 0.25
                    break
        except ProtocolError as error:
            self._queue({"type": "error", "message": str(error)})
            if self.client is not None:
                client.session.closing = True
                client.close_at = now + 0.25
        except BlockingIOError:
            return
        except (ConnectionError, OSError):
            self._drop("socket read failed")

    def _write(self) -> None:
        client = self.client
        if client is None:
            return
        try:
            sent = client.socket.send(client.outgoing)
            if sent:
                del client.outgoing[:sent]
            else:
                self._drop("socket closed during write")
        except BlockingIOError:
            return
        except (ConnectionError, OSError):
            self._drop("socket write failed")

    def step(self, timeout: float = 0.01) -> None:
        if self.closed:
            return
        now = self.clock()
        self.endpoint.tick(now)
        current_state = (bool(self.endpoint.connected), self.endpoint.detail)
        client = self.client
        if current_state != self._backend_state:
            was_ready = self._backend_state[0]
            self._backend_state = current_state
            LOG.info("MIDI endpoint: %s", current_state[1])
            if client and client.session.authenticated:
                if was_ready and not current_state[0] and client.session.companion_epoch:
                    # One enhanced TCP session belongs to one USB lifetime.
                    # Release is best effort on a dead port; no old panic or
                    # mutation may be retried on the replacement endpoint.
                    self._drop("companion USB endpoint lost; reconnect required")
                else:
                    self._queue({"type": "status", "ready": current_state[0], "reason": current_state[1][:300]})
        for message in self.endpoint.receive():
            if self.client and self.client.session.authenticated and not self.client.session.closing:
                self._queue({"type": "midi", "data": list(message)})
        client = self.client
        if client and now - client.last_frame >= CLIENT_TIMEOUT:
            self._drop("six-second controller timeout")
        elif client and client.session.closing and (
                not client.outgoing or (client.close_at is not None and now >= client.close_at)):
            self._drop("protocol rejected")
        readable = [self.listener]
        writable = []
        client = self.client
        if client:
            if not client.session.closing:
                readable.append(client.socket)
            if client.outgoing:
                writable.append(client.socket)
        reads, writes, _errors = select.select(readable, writable, [], timeout)
        if self.listener in reads:
            self._accept(self.clock())
        if self.client and self.client.socket in reads:
            self._read(self.clock())
        if self.client and self.client.socket in writes:
            self._write()

    def serve(self, stop_event: Any = None) -> None:
        try:
            while not self.closed and not (stop_event and stop_event.is_set()):
                self.step()
        finally:
            self.close()

    def close(self) -> None:
        if self.closed:
            return
        self.closed = True
        self._drop("bridge stopped")
        self.listener.close()
