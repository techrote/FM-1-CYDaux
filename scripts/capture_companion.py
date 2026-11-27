#!/usr/bin/env python3
"""Capture actual shared-renderer/native-engine frames, never hardware evidence.

The command sequence and hashes are recorded alongside each 320x240 PNG. Audio
is rendered to advance real engine state, not synthesized by this Python tool.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "simulator"))
from run import png_from_ppm  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build" / "fm1-simulator")
    parser.add_argument("--output", type=Path, default=ROOT / "docs" / "images")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    frames: list[dict] = []
    commands: list[str] = []
    process = subprocess.Popen([str(args.binary.resolve()), "--stdio"], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, text=True, encoding="utf-8")
    try:
        with tempfile.TemporaryDirectory(prefix="cydaux native capture ") as scratch:
            temporary = Path(scratch)

            def command(text: str) -> dict:
                assert process.stdin and process.stdout
                process.stdin.write(text + "\n")
                process.stdin.flush()
                result = json.loads(process.stdout.readline())
                if not result.get("ok"):
                    raise RuntimeError(result)
                # The reproducible sequence does not depend on the temp path.
                commands.append(text.replace(scratch, "<temporary>"))
                return result

            def sound(blocks: int = 4) -> None:
                for _ in range(blocks):
                    command(f"audio 4096 {temporary / 'instrument.pcm'}")

            def capture(name: str) -> None:
                state = command(f"frame {temporary / 'frame.ppm'}")
                data = png_from_ppm((temporary / "frame.ppm").read_bytes())
                path = args.output / name
                path.write_bytes(data)
                frames.append({"file": name, "width": 320, "height": 240,
                               "sha256": hashlib.sha256(data).hexdigest(),
                               "profile": state["profile"], "source_version": state["sourceVersion"],
                               "audio": state.get("audio"), "state": state,
                               "commands": list(commands)})
                commands.clear()
                print(path)

            for profile, prefix in (("zaaggenz-native", "zaaggenz"),
                                    ("xenoresonator-native", "xenoresonator")):
                command("profile " + profile)
                command("transport native")
                state = command("connect")
                if not state.get("companionReady"):
                    raise RuntimeError("Native endpoint did not negotiate; refusing mock captures")
                command("audio-clock on")
                command("page play")
                command("preset 2")
                command("parameter 0 90")
                command("parameter 1 74")
                command("note 60 105")
                sound()
                capture(f"companion-{prefix}-play.png")
                command("page presets")
                sound(1)
                capture(f"companion-{prefix}-presets.png")
                command("page sequence")
                command("sequence")
                sound(9)
                capture(f"companion-{prefix}-sequence.png")
                command("sequence")
                command("page xy")
                command("down 155 120")
                sound()
                capture(f"companion-{prefix}-xy.png")
                command("up 155 120")
                command("tap 265 116")
                command("down 190 108")
                sound()
                capture(f"companion-{prefix}-fullscreen.png")
                command("cancel")
                command("page mix")
                command("note 55 100")
                sound()
                capture(f"companion-{prefix}-state.png")
                command("panic")
                command("disconnect")
            command("quit")
        if process.wait(timeout=10) != 0:
            raise RuntimeError("Native capture process failed")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
    manifest = {"schema": 1, "evidence": "actual shared C++ framebuffer and native engine state",
                "physical_hardware": False, "images": frames}
    (args.output / "companion-captures.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
