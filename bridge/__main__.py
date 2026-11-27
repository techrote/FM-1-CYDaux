"""Run from the repository root with: python -m bridge --demo."""

from __future__ import annotations

import argparse
import logging
import os
import secrets

from .midi_io import EndpointUnavailable, UsbMidi, load_mido
from .mock import DemoMidi
from .protocol import DEFAULT_PORT
from .server import BridgeServer, PROFILES


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="FM-1 CYD Wi-Fi to USB MIDI bridge")
    parser.add_argument("--demo", "--mock", action="store_true", help="simulate an FM-1; requires no MIDI libraries")
    parser.add_argument("--list", action="store_true", help="list MIDI ports and exit")
    parser.add_argument("--output", metavar="PORT", help="exact MIDI output name, from --list")
    parser.add_argument("--input", metavar="PORT", help="optional exact MIDI input name for device readback")
    parser.add_argument("--bind", default="127.0.0.1", help="listening IPv4 address (default: local computer only)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--token", default=os.environ.get("FM1_CYD_TOKEN"), help="connection token; defaults to a freshly generated token")
    parser.add_argument("--profile", choices=sorted(PROFILES), default="sloop", help="mock behavior / initial profile metadata")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO, format="%(levelname)s: %(message)s")
    endpoint = None
    server = None
    try:
        if args.list:
            mido = load_mido()
            print("MIDI outputs (bridge -> instrument):")
            for name in mido.get_output_names():
                print(f"  {name}")
            print("MIDI inputs (instrument -> bridge):")
            for name in mido.get_input_names():
                print(f"  {name}")
            return 0
        if not 1 <= args.port <= 65535:
            parser.error("--port must be in 1..65535")
        if args.demo and (args.output or args.input):
            parser.error("--demo cannot also select real MIDI ports")
        if not args.demo and not args.output:
            parser.error("select --demo or an explicit --output port (use --list)")
        token = args.token or secrets.token_urlsafe(24)
        endpoint = DemoMidi(args.profile) if args.demo else UsbMidi(args.output, args.input)
        server = BridgeServer(endpoint, token, bind=args.bind, port=args.port,
                              mode="mock" if args.demo else "midi", profile=args.profile)
        mode = "MOCK FM-1 (simulated responses, no instrument attached)" if args.demo else "USB MIDI"
        print(f"FM-1 CYD bridge: {mode}", flush=True)
        print(f"Listening on {server.address[0]}:{server.address[1]}", flush=True)
        print(f"Connection token: {token}", flush=True)
        print("Press Ctrl+C to stop. No queued musical commands are replayed.", flush=True)
        server.serve()
    except KeyboardInterrupt:
        print("\nBridge stopped.")
    except (EndpointUnavailable, OSError, ValueError) as error:
        print(f"Bridge could not start: {error}")
        return 1
    finally:
        if server is not None:
            server.close()
        if endpoint is not None:
            endpoint.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
