"""Native shared-controller / actual TCP bridge integration.

Set FM1_CYD_SIMULATOR to the built native simulator executable. The test controls
the shared C++ UI/controller over stdio and relays its emitted MIDI through real
localhost sockets. ESP32 radios and an actual FM-1 are not involved.
"""

import json
import os
from pathlib import Path
from queue import Empty, Queue
import socket
import subprocess
import threading
import time
import unittest

from bridge.mock import DemoMidi, SYSEX_PREFIX, sloop_info
from bridge.protocol import LineDecoder, encode_frame
from bridge.server import BridgeServer


class NativeCore:
    def __init__(self, binary):
        self.process = subprocess.Popen(
            [str(binary), "--stdio"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            text=True, encoding="utf-8", bufsize=1,
        )
        self.responses = Queue(maxsize=32)
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        self.transmitted = 0
        self.outgoing = []
        self.state = self.command("state")

    def _read(self):
        try:
            for line in self.process.stdout:
                self.responses.put(json.loads(line), timeout=2)
        except Exception as error:
            self.responses.put({"ok": False, "error": str(error)}, timeout=2)

    def command(self, command):
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()
        try:
            result = self.responses.get(timeout=3)
        except Empty as error:
            raise AssertionError("Native simulator did not answer") from error
        if not result.get("ok"):
            raise AssertionError(result)
        delta = result.get("tx", self.transmitted) - self.transmitted
        if delta < 0 or delta > 24:
            raise AssertionError("Native history overflow would hide transmitted commands")
        if delta:
            self.outgoing.extend(result["midi"][-delta:])
        self.transmitted = result.get("tx", self.transmitted)
        self.state = result
        return result

    def close(self):
        try:
            if self.process.poll() is None:
                self.command("quit")
                self.process.wait(timeout=2)
        except (OSError, AssertionError, subprocess.TimeoutExpired):
            self.process.kill()
            self.process.wait(timeout=2)
        finally:
            self.process.stdin.close()
            self.process.stdout.close()
            self.reader.join(timeout=2)


class OldProtocolMock(DemoMidi):
    def send(self, data):
        if tuple(data) == (*SYSEX_PREFIX, 1, 0xF7):
            reply = list(sloop_info())
            reply[-2] = 11
            self.inject(reply)
            return
        super().send(data)


class Harness:
    def __init__(self, binary, endpoint=None):
        self.endpoint = endpoint or DemoMidi("sloop")
        self.server = BridgeServer(self.endpoint, "local-integration-token", port=0)
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.server.serve, args=(self.stop,), daemon=True)
        self.thread.start()
        self.socket = socket.create_connection(self.server.address, timeout=2)
        self.socket.settimeout(2)
        self.decoder = LineDecoder()
        self.frames = []
        self.socket.sendall(encode_frame({"type": "hello", "protocol": 1, "token": "local-integration-token"}))
        hello = self.read()
        if hello.get("mode") != "mock" or not hello.get("ready"):
            raise AssertionError(hello)
        self.native = NativeCore(binary)
        self.ping = 0

    def read(self):
        while not self.frames:
            data = self.socket.recv(4096)
            if not data:
                raise AssertionError("Bridge closed during integration")
            self.frames.extend(self.decoder.feed(data))
        return self.frames.pop(0)

    def exchange(self):
        """Pump generated requests and returned replies to a bounded quiescence."""
        for _ in range(8):
            messages, self.native.outgoing = self.native.outgoing, []
            if not messages:
                return self.native.state
            # The exercised subset is INFO and LIVE; each has one reply.
            replies_expected = sum(message[0] == 0xF0 for message in messages)
            for message in messages:
                self.socket.sendall(encode_frame({"type": "midi", "data": message}))
            self.ping += 1
            self.socket.sendall(encode_frame({"type": "ping", "id": self.ping}))
            pong = False
            replies = 0
            deadline = time.monotonic() + 3
            while not pong or replies < replies_expected:
                if time.monotonic() >= deadline:
                    raise AssertionError("Bridge did not complete request exchange")
                frame = self.read()
                if frame["type"] == "error":
                    raise AssertionError(frame)
                if frame["type"] == "pong" and frame["id"] == self.ping:
                    pong = True
                elif frame["type"] == "midi":
                    replies += 1
                    self.native.command("rx " + " ".join(map(str, frame["data"])))
        raise AssertionError("Request/reply loop did not settle")

    def connect(self):
        for command in ("echo off", "profile sloop", "transport wifi", "connect"):
            self.native.command(command)
        return self.native.state

    def close(self):
        self.native.close()
        self.socket.close()
        self.stop.set()
        self.thread.join(timeout=2)
        self.endpoint.close()
        if self.thread.is_alive():
            raise AssertionError("Bridge thread failed to stop")


@unittest.skipUnless(os.environ.get("FM1_CYD_SIMULATOR"),
                     "Set FM1_CYD_SIMULATOR to exercise the built native UI/controller")
class NativeBridgeIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.binary = Path(os.environ["FM1_CYD_SIMULATOR"]).resolve()
        self.assertTrue(self.binary.is_file(), f"Missing native simulator: {self.binary}")
        self.harness = None

    def tearDown(self):
        if self.harness:
            self.harness.close()

    def test_real_tcp_info_live_and_control_state_remain_honest(self):
        self.harness = harness = Harness(self.binary)
        before = harness.connect()
        self.assertTrue(before["mockPeer"])
        self.assertFalse(before["transportSupported"])
        self.assertFalse(before["playingKnown"])
        self.assertEqual(harness.native.outgoing[0], [240, 125, 70, 76, 1, 247])
        confirmed = harness.exchange()
        self.assertTrue(confirmed["transportSupported"])
        harness.native.command("tick 300")
        stopped = harness.exchange()
        self.assertTrue(stopped["playingKnown"])
        self.assertFalse(stopped["playing"])
        harness.native.command("tap 50 178")  # actual UI's Play button
        self.assertTrue(harness.exchange()["playing"])
        harness.native.command("tap 50 178")
        self.assertFalse(harness.exchange()["playing"])
        harness.native.command("tap 100 80")  # actual UI's first parameter
        state = harness.exchange()
        self.assertEqual(state["controls"][0]["origin"], "sent")
        self.assertTrue(state["mockPeer"])

    def test_protocol_11_reply_keeps_controller_only_and_never_polls_live(self):
        self.harness = harness = Harness(self.binary, OldProtocolMock("sloop"))
        harness.connect()
        state = harness.exchange()
        self.assertFalse(state["transportSupported"])
        self.assertFalse(state["playingKnown"])
        harness.native.command("tick 1000")
        self.assertEqual(harness.native.outgoing, [])


if __name__ == "__main__":
    unittest.main()
