# Playable evaluator and actual firmware execution

The downloadable Companion Studio plays the real portable ZaaggenZ and
Xenoresonator engines through the same CYDaux C++ controller, renderer and touch
handler used by the ESP32 firmware. A separate VIBEWARE integration executes
actual Xenoresonator firmware instructions in `fm1-emu` and qualifies its USB
companion endpoint. These are complementary software evidence classes.

## Choose the appropriate environment

| Environment | What actually runs | Owner interaction and evidence |
| --- | --- | --- |
| **Companion Studio: native engine** | Shared CYDaux controller/UI, CY1 session and original portable C sound engines compiled for the computer | Browser touchscreen, Space note, gated XY, presets, local sequence and actual PCM playback. Primary playable evaluator. No FM-1 CPU or radio emulation. |
| **Companion Studio: demo/mock** | Shared controller/UI and explicitly simulated legacy connection feedback | Preserved Melodee, SLOOP, Skiff and X0X interface/protocol evaluation. These mock profiles produce no instrument audio. |
| **VIBEWARE target qualification** | Actual pi32v2 Xenoresonator/Dinghy raw-XIP firmware under the pinned `fm1-emu` CPU, SoC and USB host model | Scripted USB identity, parameters, presets, notes, telemetry, PCM, panic and reconnect. This is currently a qualification harness, not a selectable Studio audio backend. |
| **Physical FM-1 and CYD** | Real boards, touchscreen, radio/USB and audio output | Pending owner acceptance. Software receipts do not establish physical latency, touch accuracy, heap/stack margins or acoustic output. |

## First sound

Extract the entire Windows evaluator and open **CYDaux-Evaluator.exe**. The
standalone launcher bundles Python. `Open-Simulator.cmd` selects that executable
when present; its source-only fallback requires Python 3.10+. On Linux, use
`python3 run.py` from the extracted package.

Select **Xenoresonator** or **ZaaggenZ**, wait for the matched native endpoint,
then press **Enable sound**. Hold Space to audition, drag the Play controls, or
use the XY pad with Gate enabled. **Steps** edits and starts the shared local
sequence; **Presets** recalls the engine's real presets. **Panic** releases the
performance; **Stop sound** also flushes scheduled browser audio. The window
must remain focused during performance. Start with a comfortable listening
level.

The native source revisions are pinned in `third_party/*/UPSTREAM.json`:
ZaaggenZ `ac21d089e7afce6bc7adc555deb0ce90b56a39cd` and Xenoresonator
`fa27a317bb116f2afb62a710876f96314756f814`. ZaaggenZ uses its procedural/native
mode and six curated presets. Its owner-provided source-image bank is excluded.
Xenoresonator preserves its original license status; this integration creates
no additional third-party license grant.

## Reproduce native and browser qualification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
python simulator/run.py --binary build/fm1-simulator
```

Use `build/fm1-simulator.exe` on Windows. The
[simulator guide](../simulator/README.md) documents the command/HTTP interfaces,
source builds, automated tests and extracted-package checks. CI runs actual
Chromium with the shared framebuffer and real Web Audio PCM:

```sh
python -m pip install playwright==1.62.0
python simulator/qualify_browser.py --binary build/fm1-simulator --chromium /usr/bin/google-chrome --output build/browser-evidence
python simulator/render_audio_examples.py --binary build/fm1-simulator --output build/companion-media --video
```

Omit `--chromium` to use a separately installed Playwright browser. The video
step requires `ffmpeg`; omit `--video` for WAV recordings. Browser qualification
records finite nonzero PCM delivered to Web Audio, actual touch/state changes,
note release, sequence operation and bounded scheduling. Headless execution
does not establish subjective listening or sound from a physical speaker.
Recordings and videos come from actual engine PCM and shared-renderer frames.

## Reproduce actual FM-1 target execution

The coordinated [VIBEWARE Draft/HOLD PR #15](https://github.com/techrote/FM-1-VIBEWARE/pull/15)
adds `platform/companion/` on the PLATFORM PR #13 base. It preserves the original
Xenoresonator PR #12 engine and does not take over either active branch. In an
exact checkout of that integration, on an Ubuntu host with the listed tools:

```sh
CYDAUX_REF=<exact-qualified-40-character-CYDaux-commit> bash platform/companion/ci.sh
```

The script pins Xenoresonator, Dinghy, the pi32v2 toolchain archive and
`fm1-emu` at `736aded65c8e2e9d652cf595fca248734483a877`. It first checks the
freestanding C endpoint against the independently authored C++ companion codec,
then compiles actual target instructions and runs USB requests through the
emulator's host controller. Its instruction/JIT and strict timing modes produce
separate logs, actual emulated PCM, target counters, pins and hashes. Strict
timing remains a model of selected hardware costs.

An [independent fresh execution receipt](evidence/companion-emulator-independent.json)
records replay of the locally built 57,852-byte target image, SHA-256
`11af725902a08eab29df2655e857adb80d7a626908b36b8a125be3af1d9fa93e`, in both
modes. Both runs passed USB discovery/readback, actual audio, preset/parameter
changes, stale-session and range rejection, panic, lease expiry and recovery.
Independent inspection also found identical repeated-strike PCM, different PCM
after changing coupling, identical individual strike WAVs across the two modes,
and nonzero actual PCM/modal telemetry. The strict run reported zero USB audio
underruns and a maximum **655.875 emulated microseconds** per 256-frame callback
against a **5,804.989-microsecond modeled block period**. These are emulator
observations, not physical FM-1 timings or a worst-case hardware guarantee.

That receipt identifies the exact copied inputs and its local source state; it
does not claim an independent rebuild or replace final-revision hosted CI.
For delivery acceptance, use the successful workflow for the selected PR head,
its source pins and its artifact hashes. An earlier PR #15 run at `8c22f91d`
failed USB enumeration before the harness used the emulator's complete plug
operation; it is not a passing emulator receipt.

## Delivery boundaries

The Windows standalone package is tested after extraction into a path with
spaces and Unicode, with Python/compiler directories removed from the launched
process's PATH. Linux packages and Windows Python fallback are also exercised
through actual touchscreen-to-PCM requests and immediate panic silence.
Each package records source identity, dependency provenance and file hashes.

The CYD firmware contains no native sound engine. It controls an external
instrument through supported transports. Native profiles do not grant CY1
support to unmodified Fune or another firmware. The target endpoint's enhanced
route is USB SysEx; TRS MIDI does not carry its richer protocol.

No physical flash has been performed. The Studio contains no FM-1 installer.
Downstream CI publishes source/test evidence and audio, withholding raw XIP,
ELF and emulator executables while its board, firmware-format, distribution and
recovery gates remain. Follow the [owner guide](companion-owner-guide.md) for
CYD installation and the separate physical acceptance session.
