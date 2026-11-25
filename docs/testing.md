# Software qualification and physical acceptance

## Bridge test inventory

The bridge suite currently contains **38 test cases** across the six files below.
All 38 are software checks. None connects to an actual FM-1, opens a physical
MIDI port, or runs on an ESP32. Parameterized `subTest` variants are included in
their enclosing test case, rather than counted as additional cases.

| Test file | Cases | What is exercised | Boundary used |
| --- | ---: | --- | --- |
| [test_midi_io.py](../tests/bridge/test_midi_io.py) | 10 | Complete MIDI and bounded SysEx validation; invalid messages; missing and duplicate port names; exact-name reconnect without replay; unplug and return direction; bounded callback queue; output-only mode; closing; bounded test echo | Python MIDI adapter with fake Mido ports; no installed MIDI backend required |
| [test_mock.py](../tests/bridge/test_mock.py) | 8 | Qualified controls without fake acknowledgements; unsupported controls; exact SLOOP INFO and LIVE fixtures; transport/playhead behavior; profile isolation; X0X sustain handling; SLOOP drum CC exceptions and 46-kit program range | Python simulated instrument, with no sound generation |
| [test_protocol.py](../tests/bridge/test_protocol.py) | 4 | Fragmented and combined frames; oversized incomplete frames; invalid UTF-8, JSON shapes, duplicate keys and nonfinite values; independent bounds for multiple lines | In-memory framing and validation |
| [test_server.py](../tests/bridge/test_server.py) | 12 | Authentication, no feedback loop, rejection before hello, forged Unicode token, partial MIDI frames, invalid MIDI recovery, timeout release/no replay, single-controller ownership, backend status/reconnect, oversized frame release, harmless profile metadata, and CC-only disconnect behavior | Actual localhost TCP sockets connected to a fake MIDI endpoint |
| [test_native_integration.py](../tests/bridge/test_native_integration.py) | 2 | SLOOP INFO/LIVE negotiation, UI Play/Stop, sent-versus-returned state, retained mock identity, and protocol-11 fallback with no LIVE polling | Actual compiled shared C++ controller/UI process plus actual localhost TCP bridge and Python simulated instrument |
| [test_probe.py](../tests/bridge/test_probe.py) | 2 | Authenticated diagnostic with an unavailable MIDI backend and no musical commands; rejection of malformed hello fields | One actual localhost TCP test; one scripted socket test for malformed peer replies |
| **Total** | **38** | **15 cases use actual TCP sockets; all instrument endpoints are simulated** | **Software only** |

The two native integration cases need `FM1_CYD_SIMULATOR` to identify the built
executable. Without it, unittest reports those two as skipped and runs the other
36. The repository's CI workflow sets it on both Linux and Windows; a configured
job is not evidence that a particular run passed. Use that run's logs and source
commit when reporting results.

## What the shared-core TCP tests establish

The test harness starts `fm1-simulator --stdio` and drives the actual shared C++
controller and UI hit testing. It relays the controller's emitted MIDI bytes to
the bridge through a real TCP connection, then injects returned messages into the
same controller. This covers INFO negotiation, the qualified LIVE request/reply
format, Play/Stop actions, the lack of a fabricated CC acknowledgement, and
controller-only behavior for an older protocol reply.

The harness handles the handoff between the native process and the socket. It
does **not** execute the ESP32 `WiFiClient` or NimBLE adapter, reproduce radio
timing, test USB driver behavior, or prove that hardware accepts the commands.
Synthetic fixtures are source-derived examples, not captured hardware responses.

## Running and interpreting the checks

Follow the build and platform-specific commands in [the bridge guide](bridge.md).
The native C++ behavioral suites, simulator HTTP/frontend checks, and flash-package
integrity tests are separate from the 38-case bridge inventory above.

Physical acceptance remains a separate process: actual display and touch,
note release during radio/link failures, FM-1 firmware behavior, real USB MIDI
ports on Windows/Linux, and available ESP32 memory under use. Record those
observations against [the hardware validation checklist](hardware-validation.md).
Neither a mock session nor a successful firmware build substitutes for them.
