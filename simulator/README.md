# CYDaux Companion Studio — source workflow

The studio connects the **same C++ controller, renderer, touchscreen hit testing,
profile registry and session protocol** used by the CYD firmware to actual
source-pinned portable ZaaggenZ and Xenoresonator engines. The browser displays
the native 320×240 framebuffer and plays native-generated PCM. It contains no
second implementation of the instrument controls or synthesis.

For the downloadable Windows/Linux package, follow
[the package quickstart](PACKAGE-README.md). The Windows package's
`CYDaux-Evaluator.exe` includes its Python runtime; the Linux launcher requires
Python 3.10+.

## Build and run

Use CMake 3.20+, a C11/C++17 compiler, and Python 3.10+:

```console
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
python simulator/run.py
```

On Linux, `python3` may be the Python command. Source startup first finds a
prebuilt native binary, otherwise it builds with CMake or a GCC/Clang C/C++
compiler pair. The packaged evaluator does not need a compiler.

The server binds to loopback only, at `http://127.0.0.1:8781/`. Use `--binary
PATH` for another native build, `--port 8782` to choose a free port, and `--no-open`
to leave browser opening to you. The complete server uses only Python's
standard library. It refuses foreign Host/Origin headers and bounded input
validation excludes native-command/path injection.

Select a native instrument from the dropdown and click **Enable sound**. Hold
Space with the display focused to play a note on any companion page. Drag Play
controls, use the gate-enabled XY surface, edit the sixteen-step companion
sequence and select presets. Local pattern generation remains in the common
controller; it does not advertise instrument-side pattern persistence.

## Actual engines and provenance

| Engine | Frozen source | Mode in this evaluator | Sample rate |
|---|---|---|---|
| ZaaggenZ-FM1 | `ac21d089e7afce6bc7adc555deb0ce90b56a39cd` | Actual native procedural core; source-image and hybrid assets excluded | 44,100 Hz mono PCM16 |
| VIBEWARE Xenoresonator | `fa27a317bb116f2afb62a710876f96314756f814` | Actual six-mode material instrument from PR #12 | 24,000 Hz mono PCM16 |

`third_party/*/UPSTREAM.json` contains every copied source hash. The original
engine files remain byte-identical; the ZaaggenZ build supplies a local empty
image bank and the adapter enforces `ZG_NATIVE`. Six presets select upstream
indices 2, 7, 14, 19, 24 and 31. Index 24 is explicitly adapted to native mode.
No image assets with unresolved reference licensing are included.

ZaaggenZ's original implementation license is retained. Xenoresonator's
owner-provided unspecified license identity remains separate; integration into
the owner's requested companion does not invent a third-party redistribution
grant. See `THIRD_PARTY.md` and the retained source notices.

## Transport and timing

`VirtualInstrument` implements the same bounded CY1 identity/state/parameter/
preset/note session envelope that the controller understands. Discovery checks
the requested instrument, protocol/profile version and declared capabilities.
A new connection uses a fresh session. Old sessions, duplicate/out-of-order
mutations, unsupported controls and invalid ranges are rejected. Reconnection
never replays a note or mutation. The local raw-MIDI adapter is available only
after negotiation and is not evidence for unmodified physical firmware support.

The native audio process renders at each engine's actual rate. The server
returns a 24-byte little-endian `CYDA` version-1 header followed by PCM16:
magic, version, sample rate, engine generation, block serial, frame count.
Requests accept 1–4096 frames; the browser normally asks for 1024.

While **Enable sound** is active, rendered samples own the controller clock.
Control processing occurs between chunks of at most 128 samples. Wall-clock
HTTP ticks do not also advance the sequencer. This keeps musical sequence time
independent of the display polling rate. Web Audio converts the source rate to
the selected host audio output rate. The browser keeps a bounded scheduling
horizon, normally about 45–153 ms, and reports observed underruns. This is a
software evaluation transport, not a physical latency qualification.

Each instrument/profile/preset/panic generation invalidates older browser
buffers. Stop, blur, visibility and page-close handlers release performance.
A server-side 750 ms audio-consumption lease stops abandoned playback and
panics even if the browser cannot send a final release. The engine reply queue
has eight entries and never allocates or waits for a consumer in the audio
render path. The audio render method refuses requests beyond its fixed buffer.

Telemetry is actual native state. ZaaggenZ reports the five rendered role peaks
and output peak; Xenoresonator reports six modal `|re|+|im|` proxies. Separate
output amplitude uses actual rendered PCM. Maxima are retained between
successfully queued state replies so a short transient is visible at the
controller's 100 ms state cadence. There is no fabricated animation data or
claim of physical-device telemetry.

## Legacy profiles and simulation labels

Melodee, SLOOP, Skiff and X0X retain their previous Play/Mix/XY/Link behavior,
mock connections, CC-return option, calibration handling and session safeguards.
Their local demo/mock connections generate no audio. **NATIVE** identifies
actual portable-engine readback. **DEVICE** is reserved for a non-mock device
connection and does not by itself prove an endpoint's hardware identity.
**SENT** records transport acceptance, rather than applied instrument state.

The simulator's Link → Setup screen does not create an access point. The studio
opens no physical BLE or MIDI interface and does not automatically connect to
the separate LAN bridge. No native command flashes either board.

## Script the actual controller and engines

The executable accepts one command per line and returns one JSON state per
command. `--script FILE` runs a saved session. For example:

```text
profile zaaggenz-native
transport native
connect
audio-clock on
page play
preset 0
note 48 100
parameter 0 110
audio 4096 first-block.pcm
note 48 0
page xy
frame native-xy.ppm
panic
audio-clock off
quit
```

`parameter INDEX VALUE` addresses a visible bank control (index 0–3, normalized
value 0–127); `bank` changes banks. `preset N` uses a native preset index.
`sequence` toggles the common local pattern. `down`, `move`, `up`, `tap`, and
`cancel` use exactly the shared C++ UI touch dispatch. `tick N` advances the
clock by 0–60,000 ms only when the audio sample clock is inactive. `rx` injects
MIDI for adversarial tests. `frame` and `audio` filenames can contain spaces and
Unicode. The loopback HTTP API never exposes arbitrary file path commands.

## Reproduce qualification

```console
python -m unittest discover -s simulator -p "test_*.py" -v
node simulator/test_frontend.cjs build/fm1-simulator
python simulator/qualify_extracted.py dist/cydaux-simulator-linux --output build/extracted.json
```

CMake's `virtual_test` independently exercises the actual cores, distinct preset
PCM, session spoof/stale rejection, discovery replay, sequence wrap, queue
saturation and immediate panic. Python tests exercise actual touch-to-sound,
state readback, ten presets, audio framing, paths with spaces/Unicode, lost
consumption, disconnection and sample-clock ownership.

The Node frontend test uses a DOM shim and is labelled accordingly. Actual
browser execution is separate:

```console
python -m pip install playwright==1.58.0
python simulator/qualify_browser.py --binary build/fm1-simulator --chromium /usr/bin/google-chrome --output build/browser-evidence
```

That test launches an actual headless browser, observes nonzero finite PCM
copied into real Web Audio buffers, exercises touch/keyboard/sequence and buffer
flush behavior, and captures desktop/mobile screenshots. It establishes browser
software execution, not subjective listening or physical speaker output. Its
JSON receipt records the actual browser version and scheduling counters.

## CPU-emulator boundary

The primary studio above is **native-engine execution**, not an FM-1 CPU
emulator. Separately, `fm1-emu` was inspected at
`69cb1ef46d86a2776112cc9279773ded3ffa293b`, and FM1-MHAC's raw-XIP host was
inspected at `d820ad2408a2deb43aeb4c1e2e98696a72d3fe39` (PR #10), using emulator
qualification pin `736aded65c8e2e9d652cf595fca248734483a877`.

That host can boot qualified raw images and inject DIN/USB MIDI. Its existing
MHAC instruments expose panel controls; the native companion endpoint is not
automatically present in those firmwares. A real firmware-side endpoint and
independent execution evidence are required for a CY1 emulator integration.
Use the mission's coordinated downstream evidence for any subsequently
qualified target endpoint. The studio includes no `.fwsc`, `.kpl`, stock image
or physical FM-1 installer and makes no physical qualification claim.
