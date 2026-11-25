# Owner's field guide

## What the project is (and is not)

CYDaux makes the original 2.8-inch resistive CYD a larger auxiliary control surface for an FM-1. The FM-1 retains responsibility for sound, sequencing, patches and projects. This is not a firmware replacement for the instrument or an audio streaming system. One selected firmware profile and one connection are active at a time.

The first build is intentionally a performance controller rather than a complete editor. It offers two banks of four controls, a mixer, single-touch XY, a momentary note, and connection settings. Some controls cannot be offered without an audited protocol, and displayed values may be unknown until the device reports them.

## Choose your firmware

| Order | Firmware and audited revision | Why try it | First connection |
| --- | --- | --- | --- |
| 1 | [Melodee 1.6.1](https://github.com/keremimo/melodee) | Direct wireless notes, expressive controls and XY. Four tracks. | BLE MIDI |
| 2 | [SLOOP 2.8.5](https://github.com/isod89/sloop-fm1) | Groovebox-style controls and validated LIVE transport feedback. Three synth tracks plus drums. | Wi-Fi to PC, then USB MIDI |
| 3 | [Skiff 2.7.1](https://github.com/illuminus5/skiff) | Six-track performance and mixer workflow. | Wi-Fi to PC, then USB MIDI |
| 4 | [X0X](https://github.com/charlesvestal/fm1-x0x) | Acid-oriented interaction with two 303 parts. | Wi-Fi to PC, then USB MIDI |

This order is a recommendation, not a popularity ranking or claim of hardware success. [Firmware audit](firmwares.md) gives the precise revision pins, channels, native CC meanings, engine exceptions and supported feedback. Read the selected upstream installation and recovery instructions before changing FM-1 firmware; keep its original firmware/recovery material and projects backed up where supported. CYDaux does not install or recover FM-1 firmware.

## Stage A — no FM-1 or CYD required

Download the Windows or Linux simulator from the [delivery record](delivery-001.md), extract the entire ZIP and install Python 3.10+ if needed. On Windows double-click Open-Simulator.cmd; on Linux run the native package as described in the [package instructions](../simulator/PACKAGE-README.md). The simulator displays the shared native C++ renderer in a local browser; no physical MIDI or BLE is opened and there is no sound.

Try the following:

1. Cycle firmware names and tracks; change each control bank. Look for the different labels and channel assignments.
2. Drag the four Play controls, switch to Mix, and select tracks beyond the first mixer bank in Skiff.
3. Hold NOTE or Space and release outside the display. Check that HELD clears.
4. On XY, drag across both axes. The first two controls of the selected track are the axes, independent of the currently displayed Play bank.
5. Choose a mock connection. Compare unknown values, sent commands and simulated returned values. The mock link must stay labelled as such.
6. Open Link → Setup to inspect the screen. The desktop simulator does not actually create a setup access point.

The desktop mock-return option and the separate profile-aware PC bridge mock are different mechanisms. Neither is evidence of audible behavior.

## Stage B — CYD alone

Confirm the board marking is ESP32-2432S028R, not an S3 or a different CYD panel. Before flashing, save any existing CYD firmware and configuration you need to recover; installation replaces the application and partition table. The provided script is not a backup tool. Keep the old recovery procedure available.

Use the [flash guide](flash-package.md) and its package integrity check before selecting the CYD's actual COM port. Do not select the FM-1's port. On first boot complete three calibration crosses and the independent fourth check. Inspect screen orientation and colors, use every tab, and reboot to confirm saved calibration. The app begins in DEMO and does not silently connect.

## Stage C — CYD and PC, no instrument

Use the matching implementation checkout (the initial frozen SHA is in the [delivery record](delivery-001.md)). Run the PC mock from the checkout root:

    python -m bridge --demo --profile sloop --bind 0.0.0.0

Only use this LAN listening mode on a trusted private network. Note the printed token locally; do not share it. Find the PC's active LAN IPv4 address (Windows: ipconfig). On CYD Link → Setup, join the temporary network using its displayed credentials, browse to http://192.168.4.1/, enter your 2.4 GHz Wi-Fi settings, PC address, port 45821 and token, and save. Select SLOOP → Wi-Fi → Connect. Look for MOCK LINK, not WIFI LIVE. Confirm the validated state and simulated Play/Stop. Stop the bridge with Ctrl+C when finished.

A successful test qualifies only the particular CYD-to-PC mock route exercised, not the FM-1 or an actual USB MIDI endpoint.

## Stage D — instrument arrives

For Melodee, enable its BLE radio and VISIBLE setting. Match its routing with CYD channels 1–4. Select the Melodee profile, Link → BLE → Connect, wait for the usable link indication, then test momentary notes and Level before trying Tone or Resonance. If several matching instruments advertise, use the exact intended address in Setup. Use the local FM-1 PLAY button; CYDaux does not generate external MIDI clock.

For SLOOP, Skiff and X0X, connect the instrument with a USB data cable to the PC. In the matching source checkout:

    python -m pip install -r bridge/requirements.txt
    python -m bridge --list
    python -m bridge --output "EXACT MIDI OUTPUT NAME" --input "EXACT MIDI INPUT NAME" --bind 0.0.0.0

The input name is required for feedback; use the exact names from --list. Select the intended profile before connecting. SLOOP defaults to channels 1/2/3/10, Skiff to 1–6 and X0X's implemented 303 A/B to 2/3. Match the instrument routing and enable documented CC output when testing physical-control feedback. Ensure learned mappings have not changed intended CC behavior. Start with conservative levels and note tests. Do not assume a SENT value changed the sound.

For SLOOP, the INFO response identifies the firmware and validates its live capability. The initial valid LIVE state must arrive before remote Play/Stop is enabled. Read [hardware validation](hardware-validation.md) and log each profile/route individually with the [session template](hardware-session-template.md).

## Understanding the interface

DEMO is simulated; UNKNOWN has no value; QUEUED means admitted to the controller; SENT means the selected transport accepted the write (not necessarily instrument acceptance); DEVICE means received MIDI from the real peer. Unsupported parameters are disabled. SLOOP drum resonance, attack and release are disabled, and X0X's other roles are outside the initial implementation. A received MIDI value does not mean every patch parameter is synchronized.

Use Panic if a note hangs while a link remains reachable, then Unlink and stop the instrument locally if necessary. A radio or USB link that is already broken cannot deliver a note-off. Start any reconnect with fresh state and no replay of stale performance commands.

See [troubleshooting](troubleshooting.md) for failures. The final acceptance decision remains with the physical tester.
