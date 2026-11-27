#!/usr/bin/env python3
"""Run the real C++ CYD UI in a loopback browser simulator (Python stdlib only)."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit
import webbrowser
import zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
WIDTH, HEIGHT = 320, 240


def find_binary(explicit: str | None = None, build: bool = True) -> Path:
    if explicit:
        result = Path(explicit).expanduser().resolve()
        if not result.is_file():
            raise RuntimeError(f"Simulator binary not found: {result}")
        return result
    suffix = ".exe" if os.name == "nt" else ""
    candidates = [
        *([Path(sys.executable).resolve().parent / f"fm1-simulator{suffix}"] if getattr(sys, "frozen", False) else []),
        HERE / f"fm1-simulator{suffix}",
        ROOT / "build" / f"fm1-simulator{suffix}",
        ROOT / "build" / "Release" / f"fm1-simulator{suffix}",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    if not build:
        raise RuntimeError("Native simulator has not been built")
    cmake = shutil.which("cmake")
    if cmake and (ROOT / "CMakeLists.txt").is_file():
        subprocess.run([cmake, "-S", str(ROOT), "-B", str(ROOT / "build"), "-DCMAKE_BUILD_TYPE=Release"], check=True)
        subprocess.run([cmake, "--build", str(ROOT / "build"), "--config", "Release", "--target", "fm1-simulator", "--parallel", "2"], check=True)
        return ROOT / "build" / f"fm1-simulator{suffix}"
    compiler = shutil.which("g++") or shutil.which("clang++")
    c_compiler = shutil.which("gcc") or shutil.which("clang")
    if not compiler or not c_compiler:
        raise RuntimeError(
            "Use the prebuilt simulator package, or install a C++17 compiler "
            "(g++ or clang++) and run this launcher again."
        )
    output = ROOT / "build" / f"fm1-simulator{suffix}"
    output.parent.mkdir(parents=True, exist_ok=True)
    sources = sorted((ROOT / "src").glob("*.cpp")) + [HERE / "main.cpp", HERE / "virtual_instrument.cpp"]
    c_sources = [ROOT / "third_party" / "zaaggenz" / "src" / name for name in (
        "dsp.c", "engine.c", "presets.c", "source_bank.c"
    )] + [ROOT / "third_party" / "xenoresonator" / "xeno.c", ROOT / "third_party" / "grimoire" / "orbit.c"]
    if not all(source.is_file() for source in sources + c_sources):
        raise RuntimeError("The simulator sources are incomplete; extract the whole source package")
    print("Building shared CYD controller and actual native instrument cores...", flush=True)
    includes = ["-I", str(ROOT / "include"), "-I", str(HERE),
                "-I", str(ROOT / "third_party" / "zaaggenz" / "include"),
                "-I", str(ROOT / "third_party" / "xenoresonator"), "-I", str(ROOT / "third_party" / "grimoire")]
    objects = []
    for source in c_sources:
        obj = output.parent / (source.stem + "-native.o")
        subprocess.run([c_compiler, "-std=c11", "-O2", *includes, "-c", str(source), "-o", str(obj)], check=True)
        objects.append(obj)
    subprocess.run(
        [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wpedantic", *includes,
         *(str(source) for source in sources), *(str(obj) for obj in objects), "-o", str(output)], check=True)
    return output


def png_from_ppm(ppm: bytes) -> bytes:
    header = b"P6\n320 240\n255\n"
    if not ppm.startswith(header) or len(ppm) != len(header) + WIDTH * HEIGHT * 3:
        raise ValueError("Invalid native framebuffer")
    rgb = ppm[len(header):]
    scanlines = b"".join(
        b"\x00" + rgb[row * WIDTH * 3:(row + 1) * WIDTH * 3]
        for row in range(HEIGHT)
    )
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(
            ">I", zlib.crc32(kind + data) & 0xFFFFFFFF
        )
    return b"\x89PNG\r\n\x1a\n" + chunk(
        b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0)
    ) + chunk(b"IDAT", zlib.compress(scanlines, 3)) + chunk(b"IEND", b"")


class Engine:
    def __init__(self, binary: Path):
        self._temp = tempfile.TemporaryDirectory(prefix="fm1-cydaux-")
        self.frame_path = Path(self._temp.name) / "display.ppm"
        self.audio_path = Path(self._temp.name) / "audio.pcm"
        self.audio_serial = 0
        self.last_audio = time.monotonic()
        self._stop_watchdog = threading.Event()
        self.process = subprocess.Popen(
            [str(binary), "--stdio"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            text=True, encoding="utf-8", bufsize=1,
        )
        self.lock = threading.RLock()
        self.last_tick = time.monotonic()
        self.cached_revision = None
        self.cached_png = b""
        self.state = self._request("state")
        self._watchdog = threading.Thread(target=self._watch_audio, name="cydaux-audio-watchdog", daemon=True)
        self._watchdog.start()

    def _watch_audio(self):
        # A closed/crashed browser must not retain a held note or running
        # sequence. The lease is renewed only by actual PCM consumption.
        while not self._stop_watchdog.wait(0.2):
            with self.lock:
                if (self.state.get("audio", {}).get("clockRunning") and
                        time.monotonic() - self.last_audio > 0.75):
                    try:
                        self._request("audio-clock off")
                    except (OSError, RuntimeError, ValueError):
                        return

    def _request(self, command: str) -> dict:
        if "\n" in command or "\r" in command:
            raise ValueError("A command must be a single line")
        if self.process.poll() is not None:
            raise RuntimeError("Native simulator stopped")
        assert self.process.stdin and self.process.stdout
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()
        result = self.process.stdout.readline()
        if not result:
            raise RuntimeError("Native simulator closed its output")
        state = json.loads(result)
        if not state.get("ok"):
            raise ValueError(state.get("error", "Native command failed"))
        self.state = state
        return state

    def _advance(self):
        now = time.monotonic()
        elapsed = max(0, min(60000, int((now - self.last_tick) * 1000)))
        self.last_tick = now
        if elapsed:
            self._request(f"tick {elapsed}")

    def command(self, command: str) -> dict:
        with self.lock:
            self._advance()
            if command == "audio-clock on":
                self.last_audio = time.monotonic()
            return self._request(command)

    def audio(self, frames: int = 1024) -> bytes:
        if type(frames) is not int or not 1 <= frames <= 4096:
            raise ValueError("Audio block must contain 1..4096 frames")
        with self.lock:
            self._advance()
            self.last_audio = time.monotonic()
            state = self._request(f"audio {frames} {self.audio_path}")
            data = self.audio_path.read_bytes()
            if len(data) != frames * 2:
                raise RuntimeError("Native audio block has an invalid size")
            self.audio_serial = (self.audio_serial + 1) & 0xffffffff
            audio = state["audio"]
            return struct.pack("<4sIIIII", b"CYDA", 1, audio["sampleRate"],
                               audio["generation"], self.audio_serial, frames) + data

    def frame(self) -> bytes:
        with self.lock:
            self._advance()
            revision = self.state.get("revision")
            if revision != self.cached_revision or not self.cached_png:
                self._request(f"frame {self.frame_path}")
                self.cached_png = png_from_ppm(self.frame_path.read_bytes())
                self.cached_revision = self.state.get("revision")
            return self.cached_png

    def close(self):
        self._stop_watchdog.set()
        self._watchdog.join(timeout=2)
        with self.lock:
            if self.process.poll() is None:
                try:
                    self._request("cancel")
                    self._request("quit")
                    self.process.wait(timeout=2)
                except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired):
                    self.process.kill()
                    self.process.wait(timeout=2)
            if self.process.stdin:
                self.process.stdin.close()
            if self.process.stdout:
                self.process.stdout.close()
            self._temp.cleanup()


def command_for_request(path: str, data: dict) -> str:
    if not isinstance(data, dict):
        raise ValueError("Expected a JSON object")
    if path == "/api/note":
        note, velocity = data.get("note"), data.get("velocity")
        if type(note) is not int or type(velocity) is not int or not 0 <= note <= 127 or not 0 <= velocity <= 127:
            raise ValueError("Note and velocity must be integers in 0..127")
        return f"note {note} {velocity}"
    if path == "/api/touch":
        phase = data.get("phase")
        if phase == "cancel":
            return "cancel"
        if phase not in ("down", "move", "up", "tap"):
            raise ValueError("Unknown touch phase")
        x, y = data.get("x"), data.get("y")
        if type(x) is not int or type(y) is not int or not (-4096 <= x <= 4096 and -4096 <= y <= 4096):
            raise ValueError("Touch coordinates must be bounded integers")
        return f"{phase} {x} {y}"
    if path == "/api/control":
        name, value = data.get("name"), data.get("value", "")
        if name in ("connect", "disconnect", "panic", "sequence", "bank"):
            return name
        if name not in ("profile", "page", "transport", "echo", "audio-clock", "preset"):
            raise ValueError("Unknown control")
        if not isinstance(value, str) or not re.fullmatch(r"[a-z0-9_-]{1,32}", value):
            raise ValueError("Invalid control value")
        return f"{name} {value}"
    raise ValueError("Unknown endpoint")


def handler_factory(engine: Engine):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, _format, *args):
            pass

        def send_bytes(self, status: int, content: bytes, mime: str):
            self.send_response(status)
            self.send_header("Content-Type", mime)
            self.send_header("Content-Length", str(len(content)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            self.wfile.write(content)

        def send_json(self, status: int, body: dict):
            self.send_bytes(status, json.dumps(body, allow_nan=False).encode(), "application/json")

        def local_request(self) -> bool:
            host = self.headers.get("Host", "").lower()
            allowed = {f"127.0.0.1:{self.server.server_port}", f"localhost:{self.server.server_port}"}
            if host not in allowed:
                self.send_json(403, {"ok": False, "error": "Use the loopback simulator address"})
                return False
            return True

        def do_GET(self):
            if not self.local_request():
                return
            try:
                path = urlsplit(self.path).path
                if path in ("/", "/index.html"):
                    self.send_bytes(200, (HERE / "index.html").read_bytes(), "text/html; charset=utf-8")
                elif path == "/audio.js":
                    self.send_bytes(200, (HERE / "audio.js").read_bytes(), "text/javascript; charset=utf-8")
                elif path == "/api/state":
                    self.send_json(200, engine.command("state"))
                elif path == "/api/frame.png":
                    self.send_bytes(200, engine.frame(), "image/png")
                elif path == "/favicon.ico":
                    self.send_bytes(204, b"", "image/x-icon")
                else:
                    self.send_json(404, {"ok": False, "error": "Not found"})
            except (OSError, RuntimeError, ValueError) as error:
                self.send_json(500, {"ok": False, "error": str(error)})

        def do_POST(self):
            if not self.local_request():
                return
            try:
                origin = self.headers.get("Origin")
                if origin and urlsplit(origin).netloc != self.headers.get("Host"):
                    self.send_json(403, {"ok": False, "error": "Use this simulator's own page"})
                    return
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 2048:
                    raise ValueError("Invalid request length")
                if self.headers.get_content_type() != "application/json":
                    raise ValueError("Expected application/json")
                data = json.loads(self.rfile.read(length))
                path = urlsplit(self.path).path
                if path == "/api/audio":
                    if not isinstance(data, dict):
                        raise ValueError("Expected an audio request object")
                    self.send_bytes(200, engine.audio(data.get("frames", 1024)), "application/octet-stream")
                else:
                    command = command_for_request(path, data)
                    self.send_json(200, engine.command(command))
            except (ValueError, UnicodeError) as error:
                self.send_json(400, {"ok": False, "error": str(error)})
            except (OSError, RuntimeError) as error:
                self.send_json(500, {"ok": False, "error": str(error)})
    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", help="Use a specific compiled simulator")
    parser.add_argument("--port", type=int, default=8781, help="Loopback HTTP port (default: 8781)")
    parser.add_argument("--no-open", action="store_true", help="Do not open a browser")
    args = parser.parse_args()
    if not 0 <= args.port <= 65535:
        parser.error("port must be in 0..65535")
    engine = None
    server = None
    try:
        engine = Engine(find_binary(args.binary))
        server = ThreadingHTTPServer(("127.0.0.1", args.port), handler_factory(engine))
        server.daemon_threads = True
        url = f"http://127.0.0.1:{server.server_port}/"
        print(f"FM-1 CYDaux simulator: {url}", flush=True)
        print("Native-engine mode generates actual instrument PCM; other connections are explicit mocks. No physical FM-1 qualification. Ctrl+C to stop.", flush=True)
        if not args.no_open:
            webbrowser.open(url)
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Simulator: {error}", file=sys.stderr)
        return 1
    finally:
        if server:
            server.server_close()
        if engine:
            engine.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
