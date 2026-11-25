#!/usr/bin/env python3
"""Create self-describing build artifacts without publishing or flashing them."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def provenance() -> dict:
    commit = os.environ.get("GITHUB_SHA") or subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True))
    return {"schema": 1, "source_commit": commit, "source_dirty": dirty,
            "source_repository": "https://github.com/techrote/FM-1-CYDaux",
            "qualification": "Compiled and software-tested; physical hardware acceptance pending"}


def checksum(path: Path) -> dict:
    data = path.read_bytes()
    return {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=("firmware", "simulator"))
    parser.add_argument("--platform", choices=("windows", "linux"), default="linux")
    args = parser.parse_args()
    destination = ROOT / "dist" / ("cydaux-esp32-2432s028r" if args.kind == "firmware"
                                     else f"cydaux-simulator-{args.platform}")
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    manifest = provenance()
    if args.kind == "firmware":
        from flash import PARTS, verify_package
        manifest.update(board="ESP32-2432S028R", chip="esp32", files={})
        for name, offset in PARTS:
            shutil.copy2(ROOT / ".pio" / "build" / "cyd" / name, destination / name)
            manifest["files"][name] = {**checksum(destination / name), "offset": offset}
        shutil.copy2(ROOT / "scripts" / "flash.py", destination / "flash.py")
        shutil.copy2(ROOT / "docs" / "flash-package.md", destination / "README.md")
        (destination / "requirements-flash.txt").write_text("esptool==4.5.1\n", encoding="utf-8")
        # Include the exact linked ELF for diagnosis and a map for memory inspection.
        for name in ("firmware.elf", "firmware.map"):
            path = ROOT / ".pio" / "build" / "cyd" / name
            if path.exists():
                shutil.copy2(path, destination / name)
    else:
        executable = "fm1-simulator.exe" if args.platform == "windows" else "fm1-simulator"
        for name in ("run.py", "index.html", "Open-Simulator.cmd"):
            shutil.copy2(ROOT / "simulator" / name, destination / name)
        shutil.copy2(ROOT / "simulator" / "PACKAGE-README.md", destination / "README.md")
        shutil.copy2(ROOT / "build" / executable, destination / executable)
        manifest.update(platform=args.platform, files={executable: checksum(destination / executable)})
    shutil.copy2(ROOT / "LICENSE", destination / "LICENSE")
    shutil.copy2(ROOT / "THIRD_PARTY.md", destination / "THIRD_PARTY.md")
    shutil.copytree(ROOT / "licenses", destination / "licenses")
    (destination / "docs").mkdir()
    shutil.copy2(ROOT / "docs" / "upstream-sources.json",
                 destination / "docs" / "upstream-sources.json")
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if args.kind == "firmware":
        verify_package(destination)
    print(destination)


if __name__ == "__main__":
    main()
