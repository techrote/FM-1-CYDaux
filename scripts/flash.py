#!/usr/bin/env python3
"""Flash the original ESP32-2432S028R using a verified CYDaux build package."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

PARTS = (("bootloader.bin", "0x1000"), ("partitions.bin", "0x8000"),
         ("firmware.bin", "0x10000"))


def verify_package(directory: Path) -> dict:
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("board") != "ESP32-2432S028R" or manifest.get("chip") != "esp32":
        raise ValueError("This is not a package for the original ESP32-2432S028R")
    if manifest.get("schema") != 1:
        raise ValueError("Unsupported package manifest")
    for name, offset in PARTS:
        expected = manifest.get("files", {}).get(name, {})
        payload = (directory / name).read_bytes()
        if (expected.get("offset") != offset or expected.get("size") != len(payload)
                or expected.get("sha256") != hashlib.sha256(payload).hexdigest()):
            raise ValueError(f"Package validation failed: {name}. Download/extract it again.")
        if not payload:
            raise ValueError(f"Empty flash image: {name}")
    # Partition boundaries are fixed by this board's checked-in partition table.
    if (directory / "bootloader.bin").stat().st_size > 0x7000:
        raise ValueError("Bootloader overlaps the partition table")
    if (directory / "partitions.bin").stat().st_size > 0x1000:
        raise ValueError("Partition table overlaps NVS")
    if (directory / "firmware.bin").stat().st_size > 0x300000:
        raise ValueError("Application exceeds its 3 MiB partition")
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="CYD serial port, e.g. COM5 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, choices=(115200, 460800), default=460800)
    parser.add_argument("--check", action="store_true", help="Verify package without opening a port")
    args = parser.parse_args(argv)
    directory = Path(__file__).resolve().parent
    try:
        manifest = verify_package(directory)
    except (OSError, ValueError, TypeError) as error:
        print(f"Package error: {error}", file=sys.stderr)
        return 2
    print(f"Verified ESP32-2432S028R package, source {manifest.get('source_commit', 'unknown')}")
    if args.check:
        return 0
    if not args.port:
        parser.error("Specify the CYD's --port. This script does not choose a serial device.")
    try:
        import esptool
    except ImportError:
        print("Install the flasher first: python -m pip install -r requirements-flash.txt", file=sys.stderr)
        return 2
    if esptool.__version__ != "4.5.1":
        print("This package uses esptool 4.5.1. Run python -m pip install -r requirements-flash.txt", file=sys.stderr)
        return 2
    command = ["--chip", "esp32", "--port", args.port, "--baud", str(args.baud),
               "--before", "default_reset", "--after", "hard_reset", "write_flash",
               "--flash_mode", "dio", "--flash_freq", "40m", "--flash_size", "4MB"]
    for name, offset in PARTS:
        command += [offset, str(directory / name)]
    print(f"Writing the CYD on {args.port}. Keep its USB connection attached.")
    esptool.main(command)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
