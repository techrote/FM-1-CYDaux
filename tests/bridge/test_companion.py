"""Native profile routing never claims mock or unmodified FM-1 support."""
import unittest

from bridge.midi_io import MockMidi
from bridge.server import BridgeServer, Session


def cy(op, session, sequence, payload=()):
    return (0xF0, 0x7D, 0x43, 0x59, 1, 0, op,
            *(session >> (7 * i) & 127 for i in range(4)),
            sequence & 127, sequence >> 7, len(payload), *payload, 0xF7)


class CompanionBridgeTests(unittest.TestCase):
    def test_mock_rejection_does_not_change_existing_profile(self):
        endpoint = MockMidi(echo=False)
        session = Session(endpoint, "token", "mock", "sloop", authenticated=True)
        result = session.handle({"type": "profile", "id": "zaaggenz-native"})
        self.assertEqual(result[0]["type"], "error")
        self.assertEqual(session.profile, "sloop")
        self.assertEqual(list(endpoint.sent), [])
        with self.assertRaisesRegex(ValueError, "actual negotiated"):
            BridgeServer(endpoint, "token", port=0, mode="mock", profile="xenoresonator-native")

    def test_midi_accepts_metadata_without_implicitly_negotiating(self):
        endpoint = MockMidi(echo=False)  # Only a send recorder; no native behavior.
        session = Session(endpoint, "token", "midi", "sloop", authenticated=True)
        self.assertEqual(session.handle({"type": "profile", "id": "xenoresonator-native"}), [])
        self.assertEqual(session.profile, "xenoresonator-native")
        self.assertEqual(list(endpoint.sent), [])

    def test_companion_note_session_gets_one_bounded_panic_on_disconnect(self):
        endpoint = MockMidi(echo=False)
        session = Session(endpoint, "token", "midi", "zaaggenz-native", authenticated=True)
        messages = [cy(1, 0x2345678, 0, (1,)), cy(7, 0x2345678, 1, (60, 100)),
                    cy(5, 0x2345678, 4, (0, 127))]
        for message in messages:
            self.assertEqual(session.handle({"type": "midi", "data": list(message)}), [])
        session.release()
        self.assertEqual(list(endpoint.sent), messages + [cy(8, 0x2345678, 5)])
        session.release()
        self.assertEqual(len(endpoint.sent), 4)

    def test_only_companion_notes_make_release_responsibility(self):
        endpoint = MockMidi(echo=False)
        session = Session(endpoint, "token", "midi", "zaaggenz-native", authenticated=True)
        for message in [cy(1, 99, 0, (1,)), cy(5, 99, 1, (0, 127)), cy(7, 98, 2, (60, 100))]:
            session.handle({"type": "midi", "data": list(message)})
        session.release()
        self.assertEqual(len(endpoint.sent), 3)

    def test_sequence_wrap_and_stale_commands_do_not_regress_panic(self):
        endpoint = MockMidi(echo=False)
        session = Session(endpoint, "token", "midi", "zaaggenz-native", authenticated=True)
        for message in [cy(1, 99, 16380, (1,)), cy(7, 99, 16383, (60, 100)),
                        cy(5, 99, 16000, (0, 127))]:
            session.handle({"type": "midi", "data": list(message)})
        session.release()
        self.assertEqual(endpoint.sent[-1], cy(8, 99, 1))

    def test_duplicate_panic_and_malformed_envelope_do_not_retire_note_duty(self):
        endpoint = MockMidi(echo=False)
        session = Session(endpoint, "token", "midi", "zaaggenz-native", authenticated=True)
        messages = [cy(1, 99, 0, (1,)), cy(7, 99, 1, (60, 100)),
                    cy(8, 99, 1), cy(5, 99, 4000, (0,)), cy(127, 99, 5000)]
        for message in messages:
            self.assertEqual(session.handle({"type": "midi", "data": list(message)}), [])
        session.release()
        self.assertEqual(list(endpoint.sent), messages + [cy(8, 99, 2)])

    def test_new_panic_clears_duty_but_duplicate_note_cannot_restore_it(self):
        endpoint = MockMidi(echo=False)
        session = Session(endpoint, "token", "midi", "xenoresonator-native", authenticated=True)
        messages = [cy(1, 99, 0, (2,)), cy(7, 99, 1, (60, 100)),
                    cy(8, 99, 2), cy(7, 99, 2, (60, 100))]
        for message in messages:
            self.assertEqual(session.handle({"type": "midi", "data": list(message)}), [])
        session.release()
        self.assertEqual(list(endpoint.sent), messages)

    def test_replayed_discovery_cannot_erase_release_epoch(self):
        endpoint = MockMidi(echo=False)
        session = Session(endpoint, "token", "midi", "zaaggenz-native", authenticated=True)
        for message in [cy(1, 99, 0, (1,)), cy(7, 99, 1, (60, 100)), cy(5, 99, 50, (0, 127)),
                        cy(1, 99, 0, (1,)), cy(1, 100, 0, (2,))]:
            session.handle({"type": "midi", "data": list(message)})
        session.release()
        self.assertEqual(endpoint.sent[-1], cy(8, 99, 51))

    def test_recovery_reconnect_releases_each_transport_epoch(self):
        endpoint = MockMidi(echo=False)
        old = Session(endpoint, "token", "midi", "xenoresonator-native", authenticated=True)
        for message in [cy(1, 99, 0, (2,)), cy(7, 99, 1, (48, 110)), cy(1, 100, 0, (2,))]:
            old.handle({"type": "midi", "data": list(message)})
        # A competing discovery cannot transfer responsibility inside this
        # socket. The controller's retired-session Refresh reconnects instead.
        old.release()
        self.assertEqual(endpoint.sent[-1], cy(8, 99, 2))
        recovered = Session(endpoint, "token", "midi", "xenoresonator-native", authenticated=True)
        for message in [cy(1, 100, 0, (2,)), cy(7, 100, 1, (60, 110)), cy(1, 99, 0, (2,))]:
            recovered.handle({"type": "midi", "data": list(message)})
        recovered.release()
        self.assertEqual(endpoint.sent[-1], cy(8, 100, 2))
        count = len(endpoint.sent)
        old.release()
        recovered.release()
        self.assertEqual(len(endpoint.sent), count)


if __name__ == "__main__":
    unittest.main()
