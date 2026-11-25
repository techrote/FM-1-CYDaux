# FM-1 CYDaux desktop simulator

This package runs the same C++ controller, 320 × 240 renderer and touch handling
as the CYD firmware. It lets you try the interface before the FM-1 arrives.
**It produces no sound. All instrument and radio connections in this package
are simulated.** No CYD, FM-1, MIDI driver or compiler is needed.

Extract the **whole ZIP** to a folder before running it. Keep `run.py`,
`index.html` and the native executable together. Python **3.10 or later** is
required; the Python frontend uses only its standard library.

## Windows

Double-click **Open-Simulator.cmd** in this folder. It starts the native
simulator and opens your browser at `http://127.0.0.1:8781/`.

To start it from a terminal opened in this folder:

```console
py -3 run.py
```

Use `python run.py` instead if your Python installation provides `python` but
not `py`. Keep the console window open while using the simulator. Press Ctrl+C
there to stop it. Closing the browser tab alone does not stop the local server.

## Linux

Open a terminal in the extracted folder. Restore the native executable's
permission if the download/extraction did not preserve it, then start Python:

```console
chmod +x fm1-simulator
python3 run.py
```

The Linux executable is built on the project's CI runner. If your Linux system
cannot load that binary or its runtime libraries, use the matching source
checkout and build the native simulator for your system. The repository guide
documents that separate source workflow.

## Try the interface

- Click or drag the controls inside the simulated display. Its pixels and hit
  targets come from the C++ UI, rather than a separate browser mockup.
- Use Play, Mix, XY and Link, or press keys 1–4 with the display focused.
- Hold NOTE, or hold Space on Play/XY with the display focused, then release it.
  Releasing outside the display also ends a held note.
- Tap the firmware name, track or bank selector in the display header to cycle
  it. The firmware dropdown outside the display offers direct selection.
- Choose a mock connection and Connect mock to inspect outgoing MIDI bytes.
  Mock Bluetooth is offered for Melodee; mock Wi-Fi is available for all profiles.
- Use Link → Setup to preview the device setup screen. Its `CYDaux-DEMO`
  credentials are an example: this simulator does not create an access point.

Selecting a firmware profile changes the control map. It never changes firmware
on an attached instrument. This browser simulator opens no real MIDI ports or
Bluetooth connection and does not connect to the separate LAN bridge program.

## Read the state labels

Local values are labelled DEMO. On a mock connection, values begin UNKNOWN.
With **Mock device returns control values** unchecked, a successful simulated
write becomes SENT. With it checked, the injected mock reply becomes DEMO,
because it is simulated data. MOCK LINK remains visible in both cases.

DEVICE is reserved by the shared controller for MIDI values received from a
connection that is not marked as a mock. A SENT label never proves that an
instrument applied the command. The XY cursor stays hidden while either axis
is unknown, and it follows the selected track's first two parameters regardless
of the selected control bank.

The browser simulator's simple CC-return option is separate from the
profile-aware PC bridge mock, which can exercise SLOOP INFO/LIVE negotiation.
See the repository for that network integration workflow.

## If the browser does not open

Open the URL printed in the console yourself. You can also start without
automatic browser opening:

```console
python run.py --no-open
```

If port 8781 is already in use, select another local port:

```console
python run.py --port 8782
```

On Linux use `python3`; on Windows `py -3` is also accepted. A missing executable
usually means the ZIP was not fully extracted or the wrong platform package was
downloaded. This package has no C++ source tree, so use its included executable.

## Package identity and development status

`manifest.json` records the source commit, platform and native executable hash.
`LICENSE` and `THIRD_PARTY.md` record project and dependency notices. The original
application is MIT licensed. Physical CYD/FM-1 acceptance is still pending;
successful simulation does not establish pairing, musical response or hardware
latency.

Source, build instructions, firmware profiles, USB bridge and hardware checklist:
<https://github.com/techrote/FM-1-CYDaux>
