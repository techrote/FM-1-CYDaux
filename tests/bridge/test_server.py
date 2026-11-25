"""Exercise actual localhost TCP sockets; no MIDI device or third party deps."""

import socket
import time
import unittest

from bridge.midi_io import MockMidi
from bridge.protocol import LineDecoder, encode_frame
from bridge.server import BridgeServer


class Clock:
    value = 100.0

    def __call__(self):
        return self.value


class Peer:
    def __init__(self, server):
        self.server = server
        self.socket = socket.create_connection(server.address, timeout=1)
        self.socket.setblocking(False)
        self.decoder = LineDecoder()
        self.pending = []
        self.closed = False

    def send(self, frame):
        self.socket.sendall(encode_frame(frame))

    def next(self, timeout=1):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.pending:
                return self.pending.pop(0)
            self.server.step(0.001)
            try:
                chunk = self.socket.recv(4096)
                if not chunk:
                    self.closed = True
                    return None
                self.pending.extend(self.decoder.feed(chunk))
            except BlockingIOError:
                pass
        return None

    def close(self):
        self.socket.close()


class SocketBridgeTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.endpoint = MockMidi(echo=False)
        self.server = BridgeServer(self.endpoint, "test-token", port=0, clock=self.clock)
        self.peer = Peer(self.server)

    def tearDown(self):
        self.peer.close()
        self.server.close()

    def hello(self):
        self.peer.send({"type": "hello", "protocol": 1, "token": "test-token"})
        frame = self.peer.next()
        self.assertEqual(frame, {"type": "hello", "protocol": 1, "mode": "mock",
                                 "ready": True, "name": "Demo FM-1"})

    def test_auth_then_send_and_readback_without_feedback_loop(self):
        self.hello()
        self.peer.send({"type": "midi", "data": [0xB0, 74, 93]})
        self.peer.send({"type": "ping", "id": 7})
        self.assertEqual(self.peer.next(), {"type": "pong", "id": 7})
        self.assertEqual(list(self.endpoint.sent), [(0xB0, 74, 93)])
        # Only actual endpoint ingress creates readback; send itself has no ACK.
        self.endpoint.inject([0xB0, 74, 52])
        self.assertEqual(self.peer.next(), {"type": "midi", "data": [0xB0, 74, 52]})
        self.assertEqual(list(self.endpoint.sent), [(0xB0, 74, 93)])

    def test_bad_auth_closes_without_exposing_token_or_sending_midi(self):
        self.peer.send({"type": "hello", "protocol": 1, "token": "wrong-secret"})
        frame = self.peer.next()
        self.assertEqual(frame, {"type": "error", "message": "Authentication failed"})
        self.assertNotIn("test-token", str(frame))
        self.assertNotIn("wrong-secret", str(frame))
        self.assertIsNone(self.peer.next())
        self.assertTrue(self.peer.closed)
        self.assertEqual(list(self.endpoint.sent), [])

    def test_midi_before_hello_cannot_reach_device(self):
        self.peer.send({"type": "midi", "data": [0x90, 60, 100]})
        self.assertEqual(self.peer.next()["type"], "error")
        self.assertIsNone(self.peer.next())
        self.assertEqual(list(self.endpoint.sent), [])

    def test_surrogate_in_forged_token_does_not_crash_server(self):
        self.peer.socket.sendall(b'{"type":"hello","protocol":1,"token":"\\ud800"}\n')
        self.assertEqual(self.peer.next()["message"], "Authentication failed")
        self.assertIsNone(self.peer.next())

    def test_fragmented_midi_is_forwarded_once_only_after_complete_frame(self):
        self.hello()
        wire = encode_frame({"type": "midi", "data": [0x92, 62, 100]})
        self.peer.socket.sendall(wire[:-1])
        self.server.step(0.01)
        self.assertEqual(list(self.endpoint.sent), [])
        self.peer.socket.sendall(wire[-1:])
        self.peer.send({"type": "ping", "id": 1})
        self.assertEqual(self.peer.next()["type"], "pong")
        self.assertEqual(list(self.endpoint.sent), [(0x92, 62, 100)])

    def test_invalid_midi_rejected_but_session_remains_usable(self):
        self.hello()
        self.peer.send({"type": "midi", "data": [0xB0, 74, 128]})
        self.assertEqual(self.peer.next()["type"], "error")
        self.assertEqual(list(self.endpoint.sent), [])
        self.peer.send({"type": "ping", "id": 2})
        self.assertEqual(self.peer.next(), {"type": "pong", "id": 2})

    def test_expiry_releases_only_used_channels_and_never_replays_notes(self):
        self.hello()
        self.peer.send({"type": "midi", "data": [0x93, 60, 100]})
        self.peer.send({"type": "ping", "id": 1})
        self.assertEqual(self.peer.next()["type"], "pong")
        self.clock.value += 6.0
        self.assertIsNone(self.peer.next())
        self.assertEqual(list(self.endpoint.sent), [
            (0x93, 60, 100), (0xB3, 64, 0), (0xB3, 123, 0),
        ])
        self.peer.close()
        self.peer = Peer(self.server)
        self.hello()
        self.assertEqual(len(self.endpoint.sent), 3)

    def test_second_connection_does_not_displace_authenticated_owner(self):
        self.hello()
        other = Peer(self.server)
        try:
            self.assertEqual(other.next()["message"], "Another controller is connected")
            self.peer.send({"type": "ping", "id": 99})
            self.assertEqual(self.peer.next(), {"type": "pong", "id": 99})
        finally:
            other.close()

    def test_backend_status_is_published_and_failed_commands_not_replayed(self):
        self.hello()
        self.endpoint.connected = False
        self.endpoint.detail = "MIDI device disconnected"
        self.assertEqual(self.peer.next(), {"type": "status", "ready": False,
                                           "reason": "MIDI device disconnected"})
        self.peer.send({"type": "midi", "data": [0x90, 60, 100]})
        self.assertEqual(self.peer.next()["type"], "error")
        self.endpoint.connected = True
        self.endpoint.detail = "ready again"
        self.assertTrue(self.peer.next()["ready"])
        self.assertEqual(list(self.endpoint.sent), [])

    def test_overlong_line_closes_and_releases_played_notes(self):
        self.hello()
        self.peer.send({"type": "midi", "data": [0x90, 60, 100]})
        self.peer.send({"type": "ping", "id": 1})
        self.peer.next()
        self.peer.socket.sendall(b" " * 2049)
        self.assertEqual(self.peer.next()["type"], "error")
        self.assertIsNone(self.peer.next())
        self.assertEqual(list(self.endpoint.sent)[-2:], [(0xB0, 64, 0), (0xB0, 123, 0)])

    def test_profile_metadata_never_sends_firmware_switch_messages(self):
        self.hello()
        self.peer.send({"type": "profile", "id": "skiff"})
        self.peer.send({"type": "ping", "id": 3})
        self.assertEqual(self.peer.next()["type"], "pong")
        self.assertEqual(list(self.endpoint.sent), [])


if __name__ == "__main__":
    unittest.main()
