# CYDaux Companion Studio

This evaluator runs the actual shared C++ CYD controller, renderer and touch
handling together with two real portable instrument engines. You can select
an instrument, play notes, change its sound, draw an XY gesture, edit a local
step sequence and hear the resulting native-engine audio in your browser.

**Extract the entire ZIP before launching.** Keep the executable, frontend,
source notices and manifest together. No CYD, FM-1, MIDI driver or compiler is
needed for this evaluator.

## Windows

Double-click **CYDaux-Evaluator.exe**. It opens the studio at
`http://127.0.0.1:8781/` in your default browser. The standalone launcher bundles
its Python runtime, so no separate Python installation is required. Leave its
console window open while playing; press Ctrl+C there when finished.

The package also includes `Open-Simulator.cmd`, which first launches the bundled
evaluator. If that executable is absent, it falls back to `run.py` using an
existing Python 3.10+ installation. `fm1-simulator.exe` is the native rendering
and instrument process; launching it by itself opens the scripting interface.

If the browser does not open, use the address printed in the console. To choose
another port, run `CYDaux-Evaluator.exe --port 8782` from a terminal in this folder.

## Linux

The Linux package uses Python 3.10+ and its standard library:

```console
chmod +x fm1-simulator
python3 run.py
```

If the runner-built binary cannot load on your Linux system, build the matching
source checkout for your system. The package contains pinned instrument source
and notices; the repository contains the complete companion build.

## First sound

1. Select **Xenoresonator** or **ZaaggenZ** in the instrument
   dropdown. The studio selects and connects its native endpoint.
2. Click **Enable sound**. Browser policy requires this user gesture before
   audio can start. Begin at a comfortable listening level.
3. Focus the display and hold **Space**, or hold its **NOTE** control. Release
   the key or pointer to release the note. Xenoresonator makes short percussive
   sounds; ZaaggenZ supports sustained notes.
4. Drag a control on **Play**. The displayed returned values have a **NATIVE**
   origin because they come from the actual portable engine.
5. Open **XY**, hold or sweep the pad, then try **Steps** and **Presets**.
   **Panic** silences the engine and clears scheduled browser sound immediately.

The six navigation buttons correspond to keyboard digits 1–6 when the display
is focused. The local step sequencer sends ordinary note performance through
the common controller. Pattern editing is companion-owned; it does not imply
that an instrument has returned a hardware sequencer pattern.

Changing instruments or presets clears previously queued audio. Stopping sound,
losing browser focus, hiding the tab and closing the page release active
performance. If the browser disappears without sending a release, the server
stops the audio clock and panics after its 750 ms consumption lease expires.
Re-enable sound after returning to the page.

## What is generating the sound?

- **ZaaggenZ Native:** the pinned ZaaggenZ-FM1 portable procedural synthesis core,
  44.1 kHz mono. Six character presets use its actual source, envelopes, layered
  synthesis and effects. The source-image/hybrid assets from the separate
  ZaaggenZ delivery are excluded here. Crushed Teeth Native is a native-mode
  adaptation of its factory patch; this evaluator does not claim source-image
  equivalence.
- **Xenoresonator Native:** the pinned VIBEWARE six-mode modal material engine,
  24 kHz mono, with Ceramic, Woodglass, Alien metal and Membrane presets.
  Excitation, damping, coupling and material changes reach the actual core.

The browser plays PCM produced by these C engines. It contains no substitute
JavaScript synthesizer. It performs playback-rate conversion through Web Audio.
The scheduling display shows this desktop player's current buffered audio and
underruns, not BLE, USB, physical FM-1 latency or an ESP32 measurement.

The on-screen state views use actual native-engine observations. Their six
channels are ZaaggenZ role peaks or Xenoresonator modal magnitude proxies.
Transient maxima are retained between state replies; these are not calibrated
acoustic measurements. No physical device is providing these values.

## Existing firmware profiles

Melodee, SLOOP, Skiff and X0X remain available. **Local demo** and **Mock
Bluetooth / Mock Wi-Fi** let you examine their existing interfaces and message
semantics. These mock modes produce no sound and open no real radio or MIDI port.
Their values remain labelled **DEMO**, **UNKNOWN**, **QUEUED** or **SENT** as
appropriate. A SENT value only means the transport accepted a command.

Selecting a profile never replaces firmware on an attached instrument. This
studio does not flash the CYD or FM-1. The separate physical CYD firmware and
USB bridge have their own installation and acceptance procedures.

## Evidence and identity

`manifest.json` identifies the exact source revision and hashes the delivered
files. `THIRD_PARTY.md`, `licenses/` and `third_party/` retain the engine source,
license identities and exact upstream pins. Original ZaaggenZ code is MIT;
Xenoresonator retains its owner-provided unspecified license identity. No new
permissive grant is asserted for that engine.

Passing this evaluator establishes software behavior for the named build. It
does not establish physical CYD display/touch behavior, FM-1 installation,
wireless pairing, real USB ports, audio-driver playback, target timing, power,
stack/heap margins or physical recovery. The principal repository PR remains
Draft/HOLD pending owner review.

Source and complete development instructions:
<https://github.com/techrote/FM-1-CYDaux>
