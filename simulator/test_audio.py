"""Independent actual-core audio, state and interruption tests via shipped server."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from run import Engine, ThreadingHTTPServer, find_binary, handler_factory


class ActualInstrumentAudioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary = find_binary()

    def setUp(self):
        self.engine = Engine(self.binary)

    def tearDown(self):
        self.engine.close()

    def connect(self, instrument="xenoresonator-native"):
        self.engine.command("profile " + instrument)
        self.engine.command("transport native")
        state = self.engine.command("connect")
        self.assertTrue(state["companionReady"])
        self.assertFalse(state["mockPeer"])
        self.assertEqual(state["audio"]["mode"], "native-engine")
        self.assertTrue(all(c["origin"] == "native-engine" for c in state["controls"] if c["enabled"]))
        return state

    def pcm(self, frames=4096):
        data = self.engine.audio(frames)
        magic, version, rate, generation, serial, count = struct.unpack("<4sIIIII", data[:24])
        self.assertEqual(magic, b"CYDA")
        self.assertEqual(version, 1)
        self.assertIn(rate, (24000, 44100))
        self.assertGreater(generation, 0)
        self.assertGreater(serial, 0)
        self.assertEqual(count, frames)
        self.assertEqual(len(data), 24 + frames * 2)
        return data[24:]

    def test_actual_preset_pcm_is_nonzero_and_distinct(self):
        for instrument, count in (("xenoresonator-native", 4), ("zaaggenz-native", 6)):
            self.connect(instrument)
            hashes = []
            for preset in range(count):
                state = self.engine.command(f"preset {preset}")
                self.assertEqual(state["presetIndex"], preset)
                self.engine.command("note 48 100")
                audio = self.pcm()
                values = struct.unpack("<4096h", audio)
                self.assertGreater(max(abs(n) for n in values), 100)
                self.assertLess(max(abs(n) for n in values), 32768)
                hashes.append(hashlib.sha256(audio).hexdigest())
                self.engine.command("panic")
            self.assertEqual(len(set(hashes)), count, instrument)

    def test_touch_changes_actual_audio_and_roundtrips_native_state(self):
        for instrument in ("xenoresonator-native", "zaaggenz-native"):
            self.connect(instrument)
            recordings = []
            for x in (15, 149):
                self.engine.command("preset 0")
                self.engine.command("page play")
                self.engine.command(f"down {x} 80")
                state = self.engine.command(f"up {x} 80")
                self.assertEqual(state["controls"][0]["origin"], "native-engine")
                self.engine.command("down 160 177")
                self.assertTrue(self.engine.state["noteHeld"])
                recordings.append(self.pcm())
                self.engine.command("up 500 500")
                self.assertFalse(self.engine.state["noteHeld"])
            self.assertNotEqual(recordings[0], recordings[1], instrument)

    def test_disconnection_and_panic_silence_the_core(self):
        for instrument in ("xenoresonator-native", "zaaggenz-native"):
            self.connect(instrument)
            self.engine.command("note 48 100")
            self.assertNotEqual(self.pcm(1024), bytes(2048))
            old_generation = self.engine.state["audio"]["generation"]
            self.engine.command("panic")
            self.assertGreater(self.engine.state["audio"]["generation"], old_generation)
            self.assertEqual(self.pcm(1024), bytes(2048))
            self.engine.command("note 48 100")
            self.engine.command("disconnect")
            self.assertFalse(self.engine.state["companionReady"])
            self.assertEqual(self.pcm(1024), bytes(2048))
            self.engine.command("connect")
            self.assertEqual(self.pcm(1024), bytes(2048), "reconnect must not replay note-on")

    def test_sample_clock_owns_sequence_timing_without_double_counting(self):
        self.connect()
        self.engine.command("audio-clock on")
        start = self.engine.state["milliseconds"]
        self.engine.command("tick 2000")
        self.assertEqual(self.engine.state["milliseconds"], start)
        self.pcm(4096)
        self.pcm(704)
        self.assertEqual(self.engine.state["milliseconds"] - start, 200)
        self.engine.command("sequence")
        audio = b"".join(self.pcm(1024) for _ in range(12))
        self.assertNotEqual(audio, bytes(len(audio)))
        self.engine.command("audio-clock off")
        self.assertFalse(self.engine.state["noteHeld"])
        self.assertEqual(self.pcm(1024), bytes(2048))

    def test_audio_consumption_lease_releases_abandoned_performance(self):
        self.connect("zaaggenz-native")
        self.engine.command("audio-clock on")
        self.engine.command("note 48 100")
        self.pcm(1024)
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline and self.engine.state["audio"]["clockRunning"]:
            time.sleep(0.05)
        state = self.engine.command("state")
        self.assertFalse(state["audio"]["clockRunning"])
        self.assertFalse(state["noteHeld"])
        self.assertEqual(self.pcm(1024), bytes(2048))

    def test_audio_paths_with_spaces_and_unicode(self):
        self.connect()
        with tempfile.TemporaryDirectory(prefix="CYD audio path ") as directory:
            self.engine.audio_path = Path(directory) / "native waveform \u00fc.pcm"
            self.engine.command("note 48 100")
            self.assertNotEqual(self.pcm(1024), bytes(2048))
            self.assertTrue(self.engine.audio_path.is_file())

    def test_audio_http_format_rejects_bad_inputs_and_foreign_host(self):
        self.connect()
        server = ThreadingHTTPServer(("127.0.0.1", 0), handler_factory(self.engine))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = f"http://127.0.0.1:{server.server_port}"
        try:
            for frames in (True, -1, 0, 4097, "1024", None):
                request = Request(base + "/api/audio", json.dumps({"frames": frames}).encode(), {"Content-Type": "application/json"})
                with self.assertRaises(HTTPError) as error:
                    urlopen(request, timeout=3)
                self.assertEqual(error.exception.code, 400)
            request = Request(base + "/api/audio", b'{"frames":1024}', {"Content-Type": "application/json"})
            with urlopen(request, timeout=3) as response:
                self.assertEqual(response.headers["Content-Type"], "application/octet-stream")
                self.assertEqual(response.read(4), b"CYDA")
            request = Request(base + "/api/state", headers={"Host": "attacker.invalid"})
            with self.assertRaises(HTTPError) as error:
                urlopen(request, timeout=3)
            self.assertEqual(error.exception.code, 403)
        finally:
            server.shutdown(); server.server_close(); thread.join(timeout=3)


if __name__ == "__main__":
    unittest.main()
