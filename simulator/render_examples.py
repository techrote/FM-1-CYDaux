#!/usr/bin/env python3
"""Capture actual native-renderer screenshots, without a physical instrument."""
import argparse
from run import Engine, ROOT, find_binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", help="Use a specific compiled simulator")
    args = parser.parse_args()
    target = ROOT / "docs" / "images"
    target.mkdir(parents=True, exist_ok=True)
    engine = Engine(find_binary(args.binary))
    try:
        engine.command("profile melodee")
        for page in ("play", "mix", "xy", "link"):
            engine.command("page " + page)
            if page == "xy":
                engine.command("down 160 106")
                engine.command("up 160 106")
            path = target / ("demo-" + page + ".png")
            path.write_bytes(engine.frame())
            print(path)
        engine.command("tap 235 140")
        (target / "demo-setup.png").write_bytes(engine.frame())
        engine.command("tap 150 217")
        engine.command("page xy")
        engine.command("transport wifi")
        engine.command("connect")
        engine.command("echo off")
        (target / "mock-xy-unknown.png").write_bytes(engine.frame())
        engine.command("down 160 106")
        engine.command("up 160 106")
        (target / "mock-xy-sent.png").write_bytes(engine.frame())
        engine.command("rx 0xb0 74 90")
        engine.command("rx 0xb0 71 86")
        (target / "mock-xy-readback.png").write_bytes(engine.frame())
    finally:
        engine.close()


if __name__ == "__main__":
    main()
