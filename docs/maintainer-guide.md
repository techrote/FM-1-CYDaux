# Maintainer's handbook

## Design ownership

The stable foundation is shared C++17 with explicit platform boundaries. Preserve one implementation of musical actions, state provenance, renderer and touch hit testing. The browser is a presentation layer over the native core; it is not another controller. The ESP32 adapter owns hardware and radio work, and the PC bridge owns USB MIDI endpoints. See [architecture](architecture.md) and [integration contract](CONTRACT.md).

## Source map

| Location | Responsibility |
| --- | --- |
| [include/fm1/profile.hpp](../include/fm1/profile.hpp), [src/profiles.cpp](../src/profiles.cpp) | Profile schema, source versions, routing, per-track masks and capability flags; max eight tracks/eight controls |
| [include/fm1/model.hpp](../include/fm1/model.hpp), [src/controller.cpp](../src/controller.cpp) | UI model/actions, MIDI admission, origins, queue, session and editor request lifecycle |
| [src/midi.cpp](../src/midi.cpp), [src/sloop_protocol.cpp](../src/sloop_protocol.cpp) | Bounded MIDI/BLE validation and SLOOP-specific identity/state packets |
| [src/ui.cpp](../src/ui.cpp), [src/touch.cpp](../src/touch.cpp) | Shared 320×240 rendering, hit testing and touch transform |
| [firmware/main.cpp](../firmware/main.cpp), [firmware/board_config.hpp](../firmware/board_config.hpp) | Arduino loop, panel/touch pins, preferences, setup, strip rendering and platform actions |
| [firmware/transport.cpp](../firmware/transport.cpp), [firmware/transport.hpp](../firmware/transport.hpp) | Radio worker, connection generations, readiness epochs, bounded queues and completion events |
| [bridge](../bridge) | Python CLI, TCP framing/server, exact USB MIDI ports, mock and non-musical probe |
| [simulator](../simulator) | Native stdio/script process, HTTP/framebuffer frontend, input interaction and example capture |
| [tests](../tests) | Native behavioral, packet, gesture, bridge and package regressions; synthetic SLOOP fixture |
| [scripts/package.py](../scripts/package.py), [scripts/flash.py](../scripts/flash.py) | Build packaging, manifest and checked explicit-port flash |
| [platformio.ini](../platformio.ini), [partitions.csv](../partitions.csv), [CMakeLists.txt](../CMakeLists.txt) | Embedded toolchain, partition layout and native builds |
| [build workflow](../.github/workflows/build.yml) | Windows/Linux test matrix, firmware compile, integrity and artifact publication |

## Invariants to preserve

1. A profile never flashes the instrument. Default state is Demo; real connection is explicit. Never auto-choose an ambiguous MIDI endpoint or BLE instrument.
2. Unknown/Queued/Sent/Device/Demo must remain distinguishable. Transport completion is not device acceptance; mock feedback never becomes Device. No assumed initial patch state.
3. Reconnect, profile and readiness transitions reject stale state and commands. No performance replay on reconnect. Prioritize note release, avoid unrelated all-notes-off on control-only sessions, and distinguish reachable from severed links.
4. Keep radio operations out of the touch loop. Preserve finite queues, bounded messages and explicit error handling; verify physical runtime heap/stack before increasing buffers.
5. Preserve identity, layout, range and operation checks for SLOOP. At most one editor request in flight; retry reads rather than uncertain mutations. Never accept the shared FL header as proof of instrument identity.
6. Preserve deliberate security boundaries: explicit temporary setup, secret redaction, local default bridge bind, trusted LAN only for unencrypted token-authenticated TCP, and exact device selection.
7. The 320×240 resistive display is single-touch; ensure all hit targets remain reachable, gestures are captured until release/cancel, and layout changes do not create unrelated commands.

## Reproduce the initial build

For the historical candidate, checkout the SHA in [delivery-001.md](delivery-001.md). For development use the current reviewed branch and record git rev-parse HEAD and git status --porcelain.

Native toolchain: CMake 3.20+, C++17; MSVC on Windows, GCC/Clang where available on Linux. Python 3.10+ for simulator/bridge, Python 3.11 and Node 22 as used by CI. From the repository root:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release --parallel 2
    ctest --test-dir build -C Release --output-on-failure

Enable the two actual TCP/shared-core integration cases, rather than accepting skips:

Windows PowerShell:

    $env:FM1_CYD_SIMULATOR = "build\fm1-simulator.exe"
    python -m unittest discover -s tests/bridge -v
    python -m unittest discover -s simulator -p "test_*.py" -v
    node simulator/test_frontend.cjs build/fm1-simulator.exe
    python -m unittest discover -s tests/package -v
    python scripts/package.py simulator --platform windows

Linux shell:

    export FM1_CYD_SIMULATOR=build/fm1-simulator
    python -m unittest discover -s tests/bridge -v
    python -m unittest discover -s simulator -p "test_*.py" -v
    node simulator/test_frontend.cjs build/fm1-simulator
    python -m unittest discover -s tests/package -v
    python scripts/package.py simulator --platform linux

Embedded build:

    python -m pip install platformio==6.1.18
    pio run -e cyd -j 2
    python scripts/package.py firmware
    python dist/cydaux-esp32-2432s028r/flash.py --check

The checked-in config pins Espressif32 6.9.0, Arduino ESP32 package 3.20017.241212, TFT_eSPI 2.5.43, XPT2046 commit d57f64c8b5f2bc5b8d10d121550806eeff7b06d9, NimBLE-Arduino 1.4.3 and ArduinoJson 6.21.5. Real USB bridge requirements are compatible version ranges, not an exact lockfile; record installed versions for hardware acceptance. Build runners and transitive dependencies can change, so reproduce behavior and inspect manifests rather than promising bit-for-bit reproducibility.

Flash offsets are 0x1000 bootloader, 0x8000 partition table, 0x10000 application. The NVS partition starts at 0x9000 and the application is 0x300000 bytes. Never expand or change the partition layout casually. Package generation uses a source manifest with source_dirty; preserve original packages for a test rather than repackaging dirty work as qualified.

## Add or revise a profile

* Identify a real upstream release/commit, record full SHA and exact source files/blob pins in [upstream-sources.json](upstream-sources.json). Inspect executable handlers as well as prose; record discrepancies and licenses. Do not copy arbitrary upstream code into the repo.
* Audit routing, CC wire meanings, engine-specific applicability, Program Change bounds, clock and transport semantics, input/output direction, feedback configuration, actual BLE/editor support, version negotiation and explicit unsupported operations.
* Add the bounded profile data and track masks; use standard human channel numbers at the model boundary. A control name is not proof that the instrument implements the corresponding CC. A new protocol adapter must have explicit version/layout gates.
* Update profile-aware mock behavior and fixtures; synthetic responses must be marked synthetic and must not falsely echo unsupported controls. Retain separation of local Demo and mock peer.
* Add tests for exact outgoing bytes, route and capability enforcement, invalid messages, nonmatching identity, timeouts, state reset, note release, unknown values and non-acknowledgement. Exercise same shared UI on native and ESP32 builds.
* Update the audit, owner workflow, acceptance checklist and documentation index. A source-qualified profile becomes hardware-qualified only after a recorded device test.

## Qualification and change control

Run the [test inventory](testing.md) at the candidate SHA. Inspect logs for skips and compiler warnings, not just green status. Verify the real TCP tests and all platform builds ran. Inspect artifact identity, manifest hashes, package contents and verifier. No CI success can establish on-board response or audio. For hardware-affecting changes, repeat relevant checks from [hardware-validation.md](hardware-validation.md).

Record failures with an exact reproduction and evidence before modifying the system. Use a reviewed branch and small commits. Documentation-only commits can change artifact source identity without changing executable inputs; record the old qualified SHA rather than falsely transferring a prior test result to a new head. Never replace an immutable delivery record; append a new record for a new candidate.

The owner decides acceptance and merge. No firmware updating, arbitrary patch writing, pattern editing, full sync, sample transfer or new hardware target should be silently included in a maintenance change. See [roadmap](roadmap.md) for proposals.
