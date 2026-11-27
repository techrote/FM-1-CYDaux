"""Tests for the framebuffer adapter and the actual compiled UI/controller."""
import json
from pathlib import Path
import struct
import tempfile
import threading
import unittest
from urllib.request import Request, urlopen
import zlib

from run import Engine, ThreadingHTTPServer, command_for_request, find_binary, handler_factory, png_from_ppm


class FrameAndRequestTests(unittest.TestCase):
    def test_png_preserves_exact_native_pixels(self):
        pixel = bytes((42, 196, 129))
        png = png_from_ppm(b"P6\n320 240\n255\n" + pixel * (320 * 240))
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        offset = 8
        compressed = b""
        while offset < len(png):
            size = struct.unpack(">I", png[offset:offset + 4])[0]
            kind = png[offset + 4:offset + 8]
            payload = png[offset + 8:offset + 8 + size]
            checksum = struct.unpack(">I", png[offset + 8 + size:offset + 12 + size])[0]
            self.assertEqual(checksum, zlib.crc32(kind + payload) & 0xFFFFFFFF)
            if kind == b"IHDR":
                self.assertEqual(struct.unpack(">II", payload[:8]), (320, 240))
            if kind == b"IDAT":
                compressed += payload
            offset += size + 12
        self.assertEqual(zlib.decompress(compressed), (b"\x00" + pixel * 320) * 240)

    def test_bad_frame_rejected(self):
        for data in (b"", b"P6\n320 240\n255\n", b"P6\n1 1\n255\nabc"):
            with self.assertRaises(ValueError):
                png_from_ppm(data)

    def test_http_input_cannot_inject_native_commands(self):
        self.assertEqual(command_for_request("/api/touch", {"phase": "up", "x": 500, "y": 500}), "up 500 500")
        for path, body in (
            ("/api/control", {"name": "profile", "value": "sloop\nquit"}),
            ("/api/control", {"name": "frame", "value": "anything"}),
            ("/api/touch", {"phase": "down", "x": True, "y": 0}),
            ("/api/touch", {"phase": "down", "x": 5000, "y": 0}),
            ("/api/touch", []),
        ):
            with self.assertRaises(ValueError):
                command_for_request(path, body)


class NativeIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary = find_binary()

    def setUp(self):
        self.engine = Engine(self.binary)

    def tearDown(self):
        self.engine.close()

    def mock_connection(self, echo="off"):
        self.engine.command("profile melodee")
        self.engine.command("transport wifi")
        self.engine.command("connect")
        return self.engine.command("echo " + echo)

    def test_demo_and_live_origins_are_distinct(self):
        demo = self.engine.command("state")
        self.assertTrue(demo["simulated"])
        self.assertEqual(demo["transport"], 0)
        unknown = self.mock_connection()
        self.assertTrue(unknown["mockPeer"])
        self.assertTrue(all(control["origin"] == "unknown" for control in unknown["controls"] if control["enabled"]))
        changed = self.engine.command("down 60 80")
        self.engine.command("up 60 80")
        self.assertEqual(changed["controls"][0]["origin"], "sent")
        self.assertGreater(changed["tx"], 0)
        self.engine.command("echo on")
        returned = self.engine.command("down 120 80")
        self.engine.command("up 120 80")
        self.assertEqual(returned["controls"][0]["origin"], "demo")
        self.assertGreater(returned["rx"], 0)

    def test_native_pointer_capture_releases_note_outside_screen(self):
        self.mock_connection()
        pressed = self.engine.command("down 160 177")
        self.assertTrue(pressed["noteHeld"])
        released = self.engine.command("up 500 500")
        self.assertFalse(released["noteHeld"])
        self.assertTrue(any((message[0] & 0xF0) == 0x90 and message[2] > 0 for message in released["midi"] if len(message) == 3))
        self.assertTrue(any((message[0] & 0xF0) == 0x80 or ((message[0] & 0xF0) == 0x90 and message[2] == 0) for message in released["midi"] if len(message) == 3))

    def test_all_pages_and_setup_generate_real_frames(self):
        frames = []
        for page in ("play", "mix", "xy", "link"):
            self.engine.command("page " + page)
            frames.append(self.engine.frame())
        self.assertEqual(len(set(frames)), 4)
        opened = self.engine.command("tap 235 140")
        self.assertTrue(opened["setupActive"])
        setup = self.engine.frame()
        self.assertNotIn(setup, frames)
        old_profile = opened["profile"]
        blocked = self.engine.command("tap 30 40")
        self.assertEqual(blocked["profile"], old_profile)
        closed = self.engine.command("tap 150 217")
        self.assertFalse(closed["setupActive"])

    def test_http_serves_native_frame_and_touch(self):
        server = ThreadingHTTPServer(("127.0.0.1", 0), handler_factory(self.engine))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = "http://127.0.0.1:" + str(server.server_port)
        try:
            with urlopen(base + "/", timeout=3) as response:
                self.assertIn(b"Hardware-free simulator", response.read())
            with urlopen(base + "/api/frame.png", timeout=3) as response:
                self.assertEqual(response.read(8), b"\x89PNG\r\n\x1a\n")
            request = Request(base + "/api/touch", json.dumps({"phase": "tap", "x": 120, "y": 220}).encode(), {"Content-Type": "application/json"})
            with urlopen(request, timeout=3) as response:
                self.assertEqual(json.load(response)["page"], 1)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=3)

    def test_native_frame_path_supports_spaces_and_unicode(self):
        with tempfile.TemporaryDirectory(prefix="cyd path ") as directory:
            self.engine.frame_path = Path(directory) / "display with spaces \u00fc.ppm"
            self.assertEqual(self.engine.frame()[:8], b"\x89PNG\r\n\x1a\n")
            self.assertTrue(self.engine.frame_path.read_bytes().startswith(b"P6\n320 240\n255\n"))


if __name__ == "__main__":
    unittest.main()
