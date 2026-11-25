"""The diagnostic checks the protocol without playing or reconfiguring MIDI."""

import threading
import unittest
from unittest.mock import MagicMock, patch

from bridge.midi_io import MockMidi
from bridge.probe import probe
from bridge.protocol import ProtocolError, encode_frame
from bridge.server import BridgeServer


class ProbeTests(unittest.TestCase):
    def test_real_socket_probe_authenticates_without_sending_any_midi(self):
        endpoint = MockMidi(echo=False)
        # Readiness is reported, not inferred from a successful network check.
        endpoint.connected = False
        server = BridgeServer(endpoint, "probe-test-token", port=0)
        stop = threading.Event()
        thread = threading.Thread(target=server.serve, args=(stop,), daemon=True)
        thread.start()
        try:
            frames = probe(*server.address, "probe-test-token")
            self.assertEqual(frames[0]["mode"], "mock")
            self.assertFalse(frames[0]["ready"])
            self.assertEqual(frames[-1], {"type": "pong", "id": 1})
            self.assertEqual(list(endpoint.sent), [])
        finally:
            stop.set()
            thread.join(timeout=2)
            endpoint.close()
        self.assertFalse(thread.is_alive())

    def test_malformed_hello_cannot_be_reported_as_a_successful_check(self):
        valid = {"type": "hello", "protocol": 1, "mode": "mock",
                 "ready": True, "name": "Demo FM-1"}
        for change in ({"protocol": True}, {"ready": 1}, {"name": None},
                       {"mode": "hardware"}):
            with self.subTest(change=change):
                connection = MagicMock()
                connection.__enter__.return_value = connection
                connection.recv.return_value = (
                    encode_frame({**valid, **change})
                    + encode_frame({"type": "pong", "id": 1})
                )
                with patch("bridge.probe.socket.create_connection", return_value=connection):
                    with self.assertRaises(ProtocolError):
                        probe("127.0.0.1", 45821, "test-token")
                # A rejected hello must not even advance to the diagnostic ping.
                self.assertEqual(connection.sendall.call_count, 1)


if __name__ == "__main__":
    unittest.main()
