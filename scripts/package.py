#!/usr/bin/env python3
"""Create self-describing build artifacts without publishing or flashing them."""
from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def provenance() -> dict:
    # GitHub PR jobs may expose a synthetic merge SHA in the environment. The
    # manifest records the actual checked-out source, which the job builds.
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    tree = subprocess.check_output(["git", "rev-parse", "HEAD^{tree}"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True))
    return {"schema": 1, "source_commit": commit, "source_tree": tree, "source_dirty": dirty,
            "source_repository": "https://github.com/techrote/FM-1-CYDaux",
            "qualification": "Compiled and software-tested; physical hardware acceptance pending"}


def checksum(path: Path) -> dict:
    data = path.read_bytes()
    return {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=("firmware", "simulator"))
    parser.add_argument("--platform", choices=("windows", "linux"), default="linux")
    parser.add_argument("--launcher", type=Path, help="Standalone evaluator executable to include")
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
        for name in ("run.py", "index.html", "audio.js", "Open-Simulator.cmd"):
            shutil.copy2(ROOT / "simulator" / name, destination / name)
        shutil.copy2(ROOT / "simulator" / "PACKAGE-README.md", destination / "README.md")
        shutil.copy2(ROOT / "build" / executable, destination / executable)
        if args.launcher:
            shutil.copy2(args.launcher, destination / args.launcher.name)
            runtime_notices=destination / "runtime-notices"
            runtime_notices.mkdir()
            python_license=Path(sys.base_prefix) / "LICENSE.txt"
            if not python_license.is_file():
                raise RuntimeError("Standalone packaging requires the Python runtime LICENSE.txt")
            shutil.copy2(python_license,runtime_notices / "Python-LICENSE.txt")
            distribution=importlib.metadata.distribution("pyinstaller")
            copied=False
            for entry in distribution.files or ():
                if "COPYING" in entry.name.upper() or "LICENSE" in entry.name.upper():
                    source=Path(distribution.locate_file(entry))
                    if source.is_file():
                        shutil.copy2(source,runtime_notices / ("PyInstaller-"+entry.name)); copied=True
            if not copied:
                raise RuntimeError("Standalone packaging requires PyInstaller's license/bootloader exception")
            (runtime_notices / "versions.json").write_text(json.dumps({
                "python":sys.version,"pyinstaller":distribution.version,
                "launcher":args.launcher.name},indent=2)+"\n",encoding="utf-8")
        manifest.update(platform=args.platform, files={},
                        audio_source="Actual portable native engines; not FM-1 CPU emulation",
                        standalone_launcher=args.launcher.name if args.launcher else None,
                        audio_rates={"zaaggenz-native": 44100, "xenoresonator-native": 24000, "grimoire-native": 24000,
                                     "chronomorph-native": 24000, "ecophon-native": 24000})
    shutil.copy2(ROOT / "LICENSE", destination / "LICENSE")
    shutil.copy2(ROOT / "THIRD_PARTY.md", destination / "THIRD_PARTY.md")
    shutil.copytree(ROOT / "licenses", destination / "licenses")
    # Keep linked owner procedures, source pins, screenshots and evidence
    # together; a three-file extract left the physical-session and virtual
    # evidence links dangling in otherwise self-contained packages.
    shutil.copytree(ROOT / "docs", destination / "docs")
    if (ROOT / "AGENTS.md").is_file():
        shutil.copy2(ROOT / "AGENTS.md", destination / "AGENTS.md")
    (destination / "SOURCE.md").write_text(
        "# Exact source\n\n" + manifest["source_repository"] + "/tree/" +
        manifest["source_commit"] + "\n\nSource tree: `" +
        manifest["source_tree"] + "`. See manifest.json for every shipped file hash.\n",
        encoding="utf-8")
    if args.kind == "simulator":
        # Preserve exact engine sources and their original notices. An original
        # application's license is not a blanket grant for these components.
        shutil.copytree(ROOT / "third_party", destination / "third_party")
        (destination / "simulator").mkdir()
        for name in ("README.md", "PACKAGE-README.md"):
            shutil.copy2(ROOT / "simulator" / name, destination / "simulator" / name)
        if (ROOT / "docs" / "media").is_dir():
            shutil.copytree(ROOT / "docs" / "media", destination / "media")
        manifest["dependencies"] = [json.loads(path.read_text(encoding="utf-8"))
            for path in sorted((ROOT / "third_party").glob("*/UPSTREAM.json"))]
    # Every shipped file is covered, including launchers, JavaScript and notices.
    # Keep fixed flash offsets on firmware files for the independent flasher.
    for path in sorted(destination.rglob("*")):
        if path.is_file():
            relative=path.relative_to(destination).as_posix()
            manifest["files"][relative]={**manifest["files"].get(relative,{}),**checksum(path)}
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if args.kind == "firmware":
        verify_package(destination)
    print(destination)


if __name__ == "__main__":
    main()
