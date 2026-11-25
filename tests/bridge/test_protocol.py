import unittest

from bridge.protocol import LineDecoder, ProtocolError, encode_frame


class LineProtocolTests(unittest.TestCase):
    def test_tcp_fragmentation_and_multiple_frames_are_lossless(self):
        decoder = LineDecoder()
        first = {"type": "ping", "id": 42}
        second = {"type": "midi", "data": [176, 74, 64]}
        wire = encode_frame(first) + encode_frame(second)
        result = []
        for position in range(0, len(wire), 3):
            result.extend(decoder.feed(wire[position:position + 3]))
        self.assertEqual(result, [first, second])

    def test_overlong_incomplete_line_is_rejected_without_waiting_for_lf(self):
        decoder = LineDecoder()
        decoder.feed(b" " * 2048)
        with self.assertRaises(ProtocolError):
            decoder.feed(b" ")

    def test_rejects_invalid_encoding_nonobjects_duplicate_keys_and_nonfinite(self):
        for payload in (b"\xff\n", b"[1,2]\n", b"\n", b'{"id":1,"id":2}\n',
                        b'{"id":NaN}\n', b'{"id":Infinity}\n'):
            with self.subTest(payload=payload), self.assertRaises(ProtocolError):
                LineDecoder().feed(payload)

    def test_several_valid_lines_can_exceed_one_line_limit_together(self):
        frame = {"type": "anything", "value": "x" * 1500}
        self.assertEqual(LineDecoder().feed(encode_frame(frame) * 2), [frame, frame])


if __name__ == "__main__":
    unittest.main()
