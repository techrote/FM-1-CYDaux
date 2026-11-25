# FM-1 CYDaux handbook

**Target:** original ESP32-2432S028R Cheap Yellow Display, with ILI9341 TFT and XPT2046 resistive touch. **Instrument:** M-VAVE FM-1 running a separately installed supported firmware. **Status:** first software-qualified candidate; physical acceptance pending.

This directory is the durable project knowledge base. Start with the task you are trying to accomplish rather than reading every document sequentially.

## Owner and musician

| Task | Guide |
| --- | --- |
| Understand the project and see actual renderer output | [Project README](../README.md) |
| Choose an FM-1 firmware and connect safely | [Owner's field guide](owner-guide.md) |
| Try the 320×240 interface without hardware | [Simulator guide](../simulator/README.md) |
| Install the standalone prebuilt simulator | [Simulator package README](../simulator/PACKAGE-README.md) |
| Verify and flash the original CYD | [Flash package and first boot](flash-package.md) |
| Configure the Windows/Linux bridge or mock | [Bridge operation](bridge.md) |
| Diagnose an installation, radio or musical problem | [Troubleshooting](troubleshooting.md) |
| Run acceptance on the actual boards | [Hardware validation](hardware-validation.md) |
| Record the exact result for a session | [Hardware session record template](hardware-session-template.md) |

## Developer and reviewer

| Topic | Authority |
| --- | --- |
| Component boundaries, concurrency, state and transport | [Architecture](architecture.md) |
| Bridge wire framing and interoperability | [Integration contract](CONTRACT.md), [bridge behavior](bridge.md) |
| Exact firmware revisions, CCs, routing, transport and SLOOP bytes | [Firmware audit](firmwares.md), [machine-readable source pins](upstream-sources.json) |
| Build, maintain, extend or qualify changes | [Maintainer's handbook](maintainer-guide.md) |
| Automated test scope and exclusions | [Testing inventory](testing.md) |
| Initial exact-source build record and artifact hashes | [Delivery record](delivery-001.md) |
| Proposed staged development after first hardware acceptance | [Roadmap](roadmap.md) |
| Licensing and redistribution | [Project license](../LICENSE), [third-party notices](../THIRD_PARTY.md) |

## Authority and evidence rules

* The checked-in source and immutable upstream pins define implemented behavior. The firmware audit explains the reasoning and source discrepancies. A future upstream release is not implicitly qualified.
* The project README and owner guide explain operation. The protocol contract and source are authoritative for interoperating clients; do not invent mappings from generic MIDI conventions.
* The delivery record describes a specific historical implementation SHA, not whatever HEAD happens to be today. Inspect the latest workflow runs and package manifest for subsequent candidates.
* Hardware records supersede pending status only for the exact device, firmware revision, CYDaux revision and connection route actually exercised. A simulator, a mock reply, a successful compilation and real hardware acceptance are distinct evidence classes.
* Keep secrets out of public logs, commits, screenshots and hardware records. Do not store local network configuration or actual bridge tokens in this repository.
* New features belong in reviewed changes with matching source audit, tests, documentation and a stated qualification boundary. Avoid silently broadening claims of compatibility.

## Quick glossary

**CYD:** Cheap Yellow Display, here specifically the classic ESP32-2432S028R. **BLE MIDI:** MIDI over the standard Bluetooth LE service; the CYD is the central for Melodee. **Bridge:** PC-side TCP to USB MIDI relay. **CC:** MIDI Control Change, wire value 0–127. **SysEx:** System Exclusive message; this project accepts only bounded complete messages on the bridge. **INFO/LIVE:** audited SLOOP editor requests. **Mock:** simulated instrument endpoint with explicit provenance; no audio. **Profile:** control and routing description, never an installer. **Qualified:** a claim with an identified evidence class and source revision.
