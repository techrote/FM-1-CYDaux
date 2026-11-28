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

    def test_chronomorph_native_editor_pcm_and_lifecycle(self):
        def tap(x, y):
            return self.engine.command(f"tap {x} {y}")
        opened = self.engine.command("chrono on")
        self.assertTrue(opened["chrono"]["active"])
        self.assertEqual(opened["audio"]["sampleRate"], 24000)
        self.assertEqual(opened["chrono"]["origin"], "native-engine")
        before = self.engine.frame()
        # Grow lane 0 to 16, then edit its highest bit without seven-bit truncation.
        for _ in range(9):
            tap(290, 30)
        edited = tap(8 + 19 * 15 + 8, 48)
        self.assertEqual(edited["chrono"]["lanes"][0]["length"], 16)
        self.assertTrue(edited["chrono"]["lanes"][0]["mask"] & 0x8000)
        self.assertNotEqual(self.engine.frame(), before)
        # Cancelled/cross-cell gestures never mutate the pattern.
        mask = edited["chrono"]["lanes"][0]["mask"]
        self.engine.command("down 15 48")
        self.engine.command("up 34 48")
        self.assertEqual(self.engine.state["chrono"]["lanes"][0]["mask"], mask)
        self.assertFalse(tap(45, 220)["chrono"]["playing"], "no audio lease")
        self.engine.command("audio-clock on")
        running = tap(45, 220)
        self.assertTrue(running["chrono"]["playing"])
        pcm = b"".join(self.engine.audio(4096)[24:] for _ in range(16))
        self.assertNotEqual(pcm, bytes(len(pcm)))
        # Metrics describe the actual most recent native PCM block.
        samples = struct.unpack("<4096h", pcm[-8192:])
        self.assertEqual(self.engine.state["audio"]["peak"], max(abs(x) for x in samples))
        self.assertAlmostEqual(self.engine.state["audio"]["rms"], (sum(x*x for x in samples)/len(samples))**0.5/32768, places=5)
        live = self.engine.state["chrono"]
        self.assertGreater(live["lanes"][0]["ticks"], 0)
        self.assertNotEqual(live["lanes"][0]["ticks"], live["lanes"][1]["ticks"])
        generation = self.engine.state["audio"]["generation"]
        self.engine.command("panic")
        self.assertGreater(self.engine.state["audio"]["generation"], generation)
        self.assertEqual(self.engine.audio(1024)[24:], bytes(2048))
        tap(45, 220)
        self.engine.command("audio-clock off")
        self.assertFalse(self.engine.state["chrono"]["playing"])
        self.assertEqual(self.engine.audio(1024)[24:], bytes(2048))
        self.engine.command("chrono off")
        self.assertFalse(self.engine.state["chrono"]["active"])
        self.engine.command("profile melodee")
        self.assertEqual(self.engine.state["profile"], "melodee")

    def test_native_frame_path_supports_spaces_and_unicode(self):
        with tempfile.TemporaryDirectory(prefix="cyd path ") as directory:
            self.engine.frame_path = Path(directory) / "display with spaces \u00fc.ppm"
            self.assertEqual(self.engine.frame()[:8], b"\x89PNG\r\n\x1a\n")
            self.assertTrue(self.engine.frame_path.read_bytes().startswith(b"P6\n320 240\n255\n"))


if __name__ == "__main__":
    unittest.main()
