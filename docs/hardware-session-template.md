# Hardware session record template

Copy this into a new dated test record (or an issue/PR comment) per exact firmware and route. Do not overwrite historical evidence or turn untested cells into PASS. Remove sensitive values and use sanitized evidence links. This is a template, not an actual session.

## Setup

| Field | Observation |
| --- | --- |
| UTC date / tester | Pending |
| CYD marking, PCB revision, panel/controller | Pending |
| CYDaux commit, artifact ID, manifest hash / dirty flag | Pending |
| FM-1 model, firmware version and upstream revision | Pending |
| Route | Desktop Demo / PC mock / physical CYD to mock / direct BLE / Wi-Fi to real USB (choose one) |
| Host OS, Python and MIDI backend versions if applicable | Pending |
| Exact MIDI port names (sanitize unrelated devices) | Pending |
| Routing, channels, selected patch/engine, MIDI LEARN and CC OUT | Pending |
| Test duration, serial baud and evidence directory/link | Pending |

## Results

Use PASS, FAIL, BLOCKED, NOT APPLICABLE or PENDING, with actual observation and supporting evidence. A pass only applies to the setup above. Follow the full [acceptance checklist](hardware-validation.md).

| ID | Check | Status | Observation / evidence |
| --- | --- | --- | --- |
| CYD-01 | Power, boot log, orientation/color | PENDING | |
| CYD-02 | Three-point calibration, fourth-point check, all edges and saved calibration | PENDING | |
| CYD-03 | All tabs, profile/track/bank selection, gesture and note release | PENDING | |
| NET-01 | Explicit setup credentials, save/back, AP closure | PENDING | |
| NET-02 | LAN mock connect, identity and simulated origin | PENDING | |
| BLE-01 | Melodee discovery, intended peer and pairing | PENDING | |
| BLE-02 | Service/notification readiness and reconnect | PENDING | |
| USB-01 | Exact intended port input/output and readiness | PENDING | |
| MIDI-01 | Correct channel, momentary note-on/off | PENDING | |
| MIDI-02 | Level and one audible engine parameter | PENDING | |
| MIDI-03 | Track/bank-specific controls, Program Change if supported | PENDING | |
| STATE-01 | Unknown/Queued/Sent/Device/Demo provenance | PENDING | |
| STATE-02 | Physical knob/CC feedback, where implemented | PENDING | |
| LIVE-01 | SLOOP INFO identity, validated LIVE state, explicit Play/Stop | PENDING | |
| LIVE-02 | Timeout/old-version fallback or X0X realtime transport as applicable | PENDING | |
| SAFE-01 | Panic while link reachable, clean Unlink | PENDING | |
| SAFE-02 | Abrupt radio/USB loss, local release, no stale replay on reconnect | PENDING | |
| PERF-01 | At least 15 minute interaction soak, no unresolved freezes/resets | PENDING | |
| PERF-02 | Actual free/minimum heap and stack evidence under active route | PENDING | |

## Measurements

Record the method, units and conditions; do not substitute the compile-time RAM percentage for actual heap measurements.

| Metric | Start / idle | Active or minimum | Method / notes |
| --- | --- | --- | --- |
| Free heap (bytes) | | | |
| Minimum heap (bytes), if available | | | |
| Worker stack headroom (bytes), if available | | | |
| Note latency (ms), if measured | | | |
| Touch/control responsiveness | | | |
| Reconnect time (s), if measured | | | |

## Defects and acceptance

For each defect: ID, severity, exact reproduction steps, expected vs actual, route, exact versions, smallest sanitized logs or MIDI capture, whether it reproduces in Demo/mock, and workaround (if any). Note whether the device actually remained reachable when a release command was attempted.

**Disposition:** PENDING. State precisely what is accepted, rejected, blocked or not tested, and what remains before a merge/release decision. Never claim another firmware or board passed because this route passed.
