"""Exercise actual localhost TCP sockets; no MIDI device or third party deps."""

import socket
import time
import unittest
from unittest.mock import patch

from bridge.midi_io import EndpointUnavailable, MockMidi
from bridge.protocol import LineDecoder, encode_frame
from bridge.server import BridgeServer, MAX_OUTGOING_BYTES


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

    def test_cc_only_session_does_not_silence_an_external_keyboard_on_disconnect(self):
        self.hello()
        self.peer.send({"type": "midi", "data": [0xB0, 7, 93]})
        self.peer.send({"type": "ping", "id": 4})
        self.assertEqual(self.peer.next()["type"], "pong")
        self.peer.close()
        for _ in range(3):
            self.server.step(0.001)
        self.assertEqual(list(self.endpoint.sent), [(0xB0, 7, 93)])


def cy(op, session, sequence, payload=()):
    return (0xF0, 0x7D, 0x43, 0x59, 1, 0, op,
            *(session >> (7 * i) & 127 for i in range(4)),
            sequence & 127, sequence >> 7, len(payload), *payload, 0xF7)


class FailingEndpoint(MockMidi):
    def __init__(self):
        super().__init__(echo=False)
        self.fail_next = False
        self.attempted = []

    def send(self, data):
        self.attempted.append(tuple(data))
        if self.fail_next:
            self.fail_next = False
            self.connected = False
            self.detail = "simulated USB send failure"
            raise EndpointUnavailable(self.detail)
        super().send(data)


class CompanionEpochSocketTests(unittest.TestCase):
    """Real TCP framing/connection lifecycle; simulated USB endpoint loss."""

    def setUp(self):
        self.clock = Clock()
        self.endpoint = FailingEndpoint()
        self.server = BridgeServer(self.endpoint, "test-token", port=0,
                                   clock=self.clock, mode="midi",
                                   profile="xenoresonator-native")
        self.peer = Peer(self.server)

    def tearDown(self):
        self.peer.close()
        self.server.close()

    def hello(self, ready=True):
        self.peer.send({"type": "hello", "protocol": 1, "token": "test-token"})
        frame = self.peer.next()
        self.assertEqual(frame["type"], "hello")
        self.assertEqual(frame["mode"], "midi")
        self.assertIs(frame["ready"], ready)

    def send_cy(self, message):
        self.peer.send({"type": "midi", "data": list(message)})

    def barrier(self):
        self.peer.send({"type": "ping", "id": 42})
        self.assertEqual(self.peer.next(), {"type": "pong", "id": 42})

    def play(self, session):
        self.send_cy(cy(1, session, 0, (2,)))
        self.send_cy(cy(7, session, 1, (60, 100)))
        self.barrier()

    def reconnect(self):
        self.peer.close()
        self.endpoint.connected = True
        self.endpoint.detail = "replacement USB endpoint ready"
        self.server.step(0)
        self.peer = Peer(self.server)
        self.hello()

    def test_usb_loss_retires_tcp_and_new_session_owns_release(self):
        self.hello()
        self.play(99)
        # A profile metadata change cannot relinquish the CY1 epoch.
        self.peer.send({"type": "profile", "id": "sloop"})
        self.barrier()
        self.endpoint.connected = False
        self.endpoint.detail = "USB device removed"
        self.assertIsNone(self.peer.next())
        self.assertTrue(self.peer.closed)
        self.assertIsNone(self.server.client)
        self.assertEqual(list(self.endpoint.sent), [cy(1, 99, 0, (2,)),
                                                     cy(7, 99, 1, (60, 100))])
        self.reconnect()
        self.assertEqual(len(self.endpoint.sent), 2)
        self.play(100)
        self.peer.close()
        for _ in range(3):
            self.server.step(0.001)
        self.assertEqual(list(self.endpoint.sent)[2:], [cy(1, 100, 0, (2,)),
                                                        cy(7, 100, 1, (60, 100)),
                                                        cy(8, 100, 2)])

    def test_send_failure_discards_remaining_frames_in_same_tcp_batch(self):
        self.hello()
        self.play(99)
        self.endpoint.fail_next = True
        failed = cy(5, 99, 2, (0, 127))
        stale = cy(7, 99, 3, (64, 120))
        self.peer.socket.sendall(b"".join(encode_frame(frame) for frame in (
            {"type": "midi", "data": list(failed)},
            {"type": "midi", "data": list(stale)},
            {"type": "ping", "id": 9},
        )))
        self.assertIsNone(self.peer.next())
        self.assertTrue(self.peer.closed)
        self.assertIn(failed, self.endpoint.attempted)
        self.assertNotIn(stale, self.endpoint.attempted)
        self.reconnect()
        self.assertEqual(len(self.endpoint.sent), 2)
        self.play(101)
        self.peer.close()
        for _ in range(3):
            self.server.step(0.001)
        self.assertEqual(self.endpoint.sent[-1], cy(8, 101, 2))
        self.assertNotIn(stale, self.endpoint.attempted)

    def test_discovery_retry_cannot_move_emergency_release_sequence(self):
        self.hello()
        self.play(99)
        # Endpoint accepts repeated Identity requests without moving its
        # mutation sequence. The bridge must retain the accepted note sequence.
        self.send_cy(cy(1, 99, 8192, (2,)))
        self.send_cy(cy(1, 100, 8192, (3,)))
        self.send_cy(cy(8, 99, 1))  # duplicate panic must not clear release duty
        self.barrier()
        self.peer.close()
        for _ in range(10):
            self.server.step(0.001)
        self.assertEqual(self.endpoint.sent[-1], cy(8, 99, 2))
        self.assertIsNone(self.server.client)

    def test_discovery_retry_preserves_release_after_sequence_wrap(self):
        self.hello()
        self.send_cy(cy(1, 99, 16382, (2,)))
        self.send_cy(cy(7, 99, 16383, (60, 100)))
        self.send_cy(cy(1, 99, 8190, (2,)))
        self.barrier()
        self.clock.value += 6.0
        self.assertIsNone(self.peer.next())
        self.assertEqual(self.endpoint.sent[-1], cy(8, 99, 1))

    def test_malformed_cy1_cannot_poison_release_sequence(self):
        self.hello()
        self.play(99)
        # The actual C endpoint consumes a forward sequence before validating
        # the operation/size. With delta 8191 the old emergency seq 2 is stale.
        for message in (cy(5, 99, 8192, (0,)), cy(10, 99, 8192),
                        cy(8, 99, 8192, (1,))):
            self.send_cy(message)
            self.assertEqual(self.peer.next(), {
                "type": "error", "message": "Invalid CY1 operation or payload size"})
        self.barrier()
        self.assertEqual(list(self.endpoint.sent),
                         [cy(1, 99, 0, (2,)), cy(7, 99, 1, (60, 100))])
        self.peer.close()
        for _ in range(10):
            self.server.step(0.001)
        self.assertEqual(self.endpoint.sent[-1], cy(8, 99, 2))
        self.assertIsNone(self.server.client)

    def test_non_cy1_sysex_and_other_version_are_not_filtered(self):
        self.hello()
        legacy = (0xF0, 0x7D, 0x46, 0x4C, 1, 0xF7)
        future = list(cy(10, 99, 1))
        future[4] = 2
        self.send_cy(legacy)
        self.send_cy(future)
        self.barrier()
        self.assertEqual(list(self.endpoint.sent), [legacy, tuple(future)])

    def test_slow_reader_recovers_below_output_budget_in_order(self):
        self.hello()
        self.play(99)
        frames = [cy(4, 99, i, (0,) * 23) for i in range(64)]
        # Model a temporarily unwritable TCP socket deterministically.
        # Actual localhost framing, queueing and later writes still run.
        with patch.object(self.server, "_write", return_value=None):
            for message in frames:
                self.endpoint.inject(message)
            self.server.step(0)
            self.assertIsNotNone(self.server.client)
            self.assertEqual(bytes(self.server.client.outgoing), b"".join(
                encode_frame({"type": "midi", "data": list(m)}) for m in frames))
        for message in frames:
            self.assertEqual(self.peer.next(), {"type": "midi", "data": list(message)})
        self.barrier()
        self.assertEqual(len(self.server.client.outgoing), 0)
        self.assertEqual(len(self.endpoint.sent), 2)

    def test_slow_reader_over_budget_releases_and_never_replays(self):
        self.hello()
        self.play(99)
        message = cy(4, 99, 1, (0,) * 23)
        encoded = encode_frame({"type": "midi", "data": list(message)})
        capacity = MAX_OUTGOING_BYTES // len(encoded)
        retired = self.server.client
        with patch.object(self.server, "_write", return_value=None):
            remaining = capacity
            while remaining:
                batch = min(remaining, 128)
                for _ in range(batch):
                    self.endpoint.inject(message)
                self.server.step(0)
                self.assertIs(self.server.client, retired)
                remaining -= batch
            self.assertEqual(len(retired.outgoing), capacity * len(encoded))
            self.assertLessEqual(len(retired.outgoing), MAX_OUTGOING_BYTES)
            self.endpoint.inject(message)
            self.server.step(0)
        self.assertIsNone(self.server.client)
        self.assertEqual(len(retired.outgoing), 0)
        self.assertEqual(list(self.endpoint.sent), [
            cy(1, 99, 0, (2,)), cy(7, 99, 1, (60, 100)), cy(8, 99, 2)])
        self.assertIsNone(self.peer.next())
        # Simulate an old USB reply arriving before a new authenticated client.
        self.endpoint.inject(message)
        self.peer.close()
        self.peer = Peer(self.server)
        self.hello()
        self.barrier()
        self.assertEqual(len(self.endpoint.sent), 3)
        self.play(100)
        self.peer.close()
        for _ in range(10):
            self.server.step(0.001)
        self.assertEqual(list(self.endpoint.sent)[3:], [
            cy(1, 100, 0, (2,)), cy(7, 100, 1, (60, 100)), cy(8, 100, 2)])

    def test_initial_wait_is_not_a_lost_epoch(self):
        self.endpoint.connected = False
        self.endpoint.detail = "waiting for USB"
        self.hello(ready=False)
        self.endpoint.connected = True
        self.endpoint.detail = "USB ready"
        self.assertEqual(self.peer.next(), {"type": "status", "ready": True,
                                           "reason": "USB ready"})
        self.play(99)
        self.assertEqual(len(self.endpoint.sent), 2)


if __name__ == "__main__":
    unittest.main()
