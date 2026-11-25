"""Behaviour tests without installed MIDI libraries or connected hardware."""

import unittest

from bridge.midi_io import (
    EndpointUnavailable, MAX_INPUT_MESSAGES, MockMidi, UsbMidi, validate_midi_bytes,
)


class FakeMessage:
    def __init__(self, data):
        self.data = data

    def bytes(self):
        return self.data

    @classmethod
    def from_bytes(cls, data):
        return cls(data)


class FakePort:
    def __init__(self):
        self.closed = False
        self.sent = []
        self.fail = False

    def close(self):
        self.closed = True

    def send(self, message):
        if self.fail:
            raise OSError("unplugged")
        self.sent.append(message.bytes())


class FakeMido:
    Message = FakeMessage

    def __init__(self):
        self.outputs = ["FM-1 OUT", "Other synth"]
        self.inputs = ["FM-1 IN"]
        self.output_opened = []
        self.input_opened = []
        self.output = None
        self.callback = None

    def get_output_names(self):
        return self.outputs

    def get_input_names(self):
        return self.inputs

    def open_output(self, name):
        self.output_opened.append(name)
        self.output = FakePort()
        return self.output

    def open_input(self, name, callback):
        self.input_opened.append(name)
        self.callback = callback
        return FakePort()


class MidiValidationTests(unittest.TestCase):
    def test_accepts_complete_channel_and_bounded_sysex_messages(self):
        for data in ([0xB0, 7, 100], [0xC2, 17], [0xF8], [0xF0, 0x7D, 1, 0xF7]):
            self.assertEqual(validate_midi_bytes(data), tuple(data))

    def test_rejects_ambiguous_truncated_and_out_of_range_messages(self):
        for data in ([], [7, 100], [0xB0, 7], [0xB0, 7, 128], [0xC0, 1, 2],
                     [0xF0, 1], [0xF0, 255, 0xF7], [0xF9], [0x90, True, 1],
                     [0xF0] + [1] * 255 + [0xF7]):
            with self.subTest(data=data[:5]), self.assertRaises(ValueError):
                validate_midi_bytes(data)


class UsbMidiTests(unittest.TestCase):
    def setUp(self):
        self.mido = FakeMido()
        self.endpoint = UsbMidi("FM-1 OUT", "FM-1 IN", mido_loader=lambda: self.mido)

    def test_missing_selected_port_never_falls_back_to_another_synth(self):
        self.mido.outputs = ["Other synth"]
        self.endpoint.tick(0)
        self.assertFalse(self.endpoint.connected)
        self.assertEqual(self.mido.output_opened, [])
        with self.assertRaises(EndpointUnavailable):
            self.endpoint.send([0xB0, 7, 20])

    def test_duplicate_exact_names_do_not_silently_pick_the_first_device(self):
        self.mido.outputs = ["FM-1 OUT", "FM-1 OUT"]
        self.endpoint.tick(0)
        self.assertFalse(self.endpoint.connected)
        self.assertEqual(self.mido.output_opened, [])
        self.assertIn("ambiguous", self.endpoint.detail)

    def test_reconnects_only_to_same_port_without_replaying_failed_command(self):
        self.endpoint.tick(0)
        old_port = self.mido.output
        old_port.fail = True
        with self.assertRaises(EndpointUnavailable):
            self.endpoint.send([0xB0, 7, 20])
        self.endpoint.tick(2)
        self.assertTrue(self.endpoint.connected)
        self.assertEqual(self.mido.output_opened, ["FM-1 OUT", "FM-1 OUT"])
        self.assertEqual(self.mido.output.sent, [])
        self.assertTrue(old_port.closed)

    def test_unplug_is_detected_and_return_traffic_is_not_written_back(self):
        self.endpoint.tick(0)
        self.mido.callback(FakeMessage([0xB0, 7, 90]))
        self.assertEqual(self.endpoint.receive(), [(0xB0, 7, 90)])
        self.assertEqual(self.mido.output.sent, [])
        self.mido.outputs = []
        self.endpoint.tick(2)
        self.assertFalse(self.endpoint.connected)
        self.assertTrue(self.mido.output.closed)

    def test_callback_queue_is_bounded(self):
        self.endpoint.tick(0)
        for _ in range(MAX_INPUT_MESSAGES + 20):
            self.mido.callback(FakeMessage([0xB0, 7, 90]))
        self.assertEqual(self.endpoint.dropped_input, 20)
        self.assertEqual(len(self.endpoint.receive()), MAX_INPUT_MESSAGES)

    def test_output_only_does_not_require_an_input_port(self):
        endpoint = UsbMidi("FM-1 OUT", mido_loader=lambda: self.mido)
        self.mido.inputs = []
        endpoint.tick(0)
        endpoint.send([0xC0, 12])
        self.assertEqual(self.mido.input_opened, [])
        self.assertEqual(self.mido.output.sent, [[0xC0, 12]])

    def test_close_disables_reconnect(self):
        self.endpoint.tick(0)
        self.endpoint.close()
        self.endpoint.tick(10)
        self.assertFalse(self.endpoint.connected)
        self.assertEqual(len(self.mido.output_opened), 1)


class MockMidiTests(unittest.TestCase):
    def test_simulated_echo_is_bounded_and_does_not_generate_more_midi(self):
        endpoint = MockMidi()
        for value in range(MAX_INPUT_MESSAGES + 10):
            endpoint.send([0xB0, 7, value % 128])
        self.assertEqual(len(endpoint.receive()), MAX_INPUT_MESSAGES)
        self.assertEqual(endpoint.dropped_input, 10)
        self.assertEqual(len(endpoint.sent), MAX_INPUT_MESSAGES)
        self.assertEqual(endpoint.receive(), [])


if __name__ == "__main__":
    unittest.main()
