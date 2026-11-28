"""Regression for the extracted Chronomorph PNG gate and package inventory."""
import struct
import unittest
import zlib

from package_contract import require_audio_rates, require_frame


def make_png(width=320, height=240):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    pixels = (b"\x00" + b"\x00" * (width * 3)) * height
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))


class PackageContractTests(unittest.TestCase):
    def test_binary_png_not_literal_escapes(self):
        frame = make_png()
        require_frame(frame, "chronomorph-native")
        for invalid in (b"\\x89PNG\\r\\n\\x1a\\n" + frame[8:],
                        frame[:20], make_png(240, 320),
                        frame[:12] + b"IDAT" + frame[16:]):
            with self.assertRaises(RuntimeError):
                require_frame(invalid, "chronomorph-native")

    def test_manifest_requires_every_exercised_engine(self):
        receipt = [
            {"id": "xenoresonator-native", "rate": 24000},
            {"id": "zaaggenz-native", "rate": 44100},
            {"id": "grimoire-native", "rate": 24000},
            {"id": "chronomorph-native", "rate": 24000},
        ]
        rates = {item["id"]: item["rate"] for item in receipt}
        require_audio_rates({"audio_rates": rates}, receipt)
        incomplete = dict(rates)
        del incomplete["chronomorph-native"]
        with self.assertRaises(RuntimeError):
            require_audio_rates({"audio_rates": incomplete}, receipt)
        with self.assertRaises(RuntimeError):
            require_audio_rates({"audio_rates": dict(rates, **{"chronomorph-native": 44100})}, receipt)
        with self.assertRaises(RuntimeError):
            require_audio_rates({"audio_rates": rates}, receipt + receipt[:1])


if __name__ == "__main__":
    unittest.main()
