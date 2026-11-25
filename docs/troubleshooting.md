# Troubleshooting and fault isolation

The target is the original ESP32-2432S028R, not every board sold as a CYD. These are diagnostic procedures, not claims that a particular failure has been observed on physical hardware. Capture the exact build and reproduction before changing code.

## Identify the failing boundary

| Boundary | First non-destructive check | What that proves |
| --- | --- | --- |
| Desktop renderer | Launch the matching prebuilt simulator; try Play/Mix/XY/Link | Local shared UI behavior only |
| Firmware archive | Run python flash.py --check from the extracted package | Declared images, hashes and offsets; not PCB identity |
| CYD display/touch | Boot Demo, check landscape, corners, calibration and persistence | Particular board and panel behavior |
| PC bridge process | Start local mock and authenticate/ping with bridge.probe | Local TCP handshake only |
| CYD-to-PC network | Connect to LAN mock and confirm MOCK LINK | Actual board network route, not USB or FM-1 |
| MIDI endpoint | Inspect --list; select exact input/output names | Enumerated names, not musical response |
| Melodee radio | Visible radio, unique peer, service and subscription | Usable BLE connection, not CC interpretation |
| Instrument behavior | Known channel, note on/off, Level, one audible CC | Selected mapping on that actual patch |
| SLOOP live state | Valid INFO and initial LIVE, compare playing state to FM-1 | Supported state subset, not full patch sync |

## Simulator

* No browser opens: inspect the console and open its printed localhost URL. From the extracted folder use python run.py --no-open. Port busy: use python run.py --port 8782. Keep the console open; Ctrl+C stops it.
* Executable missing: extract the whole platform-specific ZIP. The Windows prebuilt does not need a compiler but does need Python 3.10+. The Linux executable needs execute permission and a compatible runtime. Use a local source build if the hosted binary cannot load.
* No sound or real Bluetooth devices: expected. The simulator runs the shared control UI, not an FM-1 sound engine or radio emulator.
* Setup does not advertise a network: expected in the simulator. Its setup screen is illustrative.

## Firmware installation and display

* Check failure: do not edit the manifest to make it pass. Re-extract the matching artifact and check source identity. Read [flash package instructions](flash-package.md).
* No serial port: verify the USB cable carries data, board is powered and the OS sees the serial adapter. Close another serial monitor using the port. Confirm the target is the CYD. On Linux check serial permissions. If automatic entry fails use the documented BOOT/RESET sequence. Try the supported slower baud rate 115200.
* Blank, inverted, mirrored or wrongly colored display: retain the serial boot log at 115200 and record actual board/panel revision. Stop treating the build as compatible until display driver/pin/orientation assumptions are investigated. TFT_eSPI's compile warning about TOUCH_CS is expected here because touch is on a separate XPT2046 driver; that warning alone does not prove an actual touch fault.
* Calibration fails: release between points, use a stable press, then the separate fourth check. Runtime BOOT hold of about two seconds requests recalibration; holding BOOT during reset enters the bootloader instead.
* Resets or freezes: obtain the serial output from boot and reproduction, including free heap, minimum heap/worker stack observations where reported, active page, route, and whether it happens without a radio. Do not infer runtime memory headroom from compile-time static RAM.

## Setup and Wi-Fi

* Cannot join setup network: explicitly open Link → Setup, copy the exact case-sensitive displayed credentials, stay associated with that temporary access point, and open http://192.168.4.1/. The setup network intentionally has no internet service. If the phone changes networks automatically, return to the CYD AP.
* Settings not taking effect: save the form, verify the temporary network closes, then select the intended mode and Connect. Blank saved password/token fields retain existing values; explicit open-Wi-Fi option clears Wi-Fi password. Channel overrides are session/profile-local, not saved as a global instrument routing change.
* CYD cannot connect to home Wi-Fi: verify 2.4 GHz availability and password. Ensure the computer is on a reachable trusted LAN.
* Connection refused/timeout: the bridge defaults to loopback. Restart it with explicit LAN bind; use the PC's LAN IPv4 address on the CYD, not 0.0.0.0 or 127.0.0.1. Check the port (45821 by default), private-network firewall and guest/client isolation. Do not expose the port to the internet.
* Authentication rejected: verify the current printed token. Restarting without a configured token creates a new one. Keep probe and CYD sessions separate because the bridge admits one controller. From the checkout root use python -m bridge.probe --host PC_LAN_IP --token TOKEN (substitute local values; don't commit them). A probe can pass even if the real MIDI backend is not ready.
* Link says MOCK: you selected the simulated backend. Use the exact real input/output selection to test hardware. A mock never produces a real DEVICE value.
* USB backend waiting: run --list again, check cable, port occupancy, input/output direction, and exact unique names. The bridge deliberately will not select another device automatically. If ALSA changed the client number, restart with the new exact name. Use output-only mode only if readback is not needed.

## Direct BLE (Melodee)

Enable BLE and VISIBLE on the intended audited firmware. Do not try to use BLE with SLOOP or Skiff in this implementation. Ensure only one peer is active; close competing clients. When several matching devices advertise, set the desired address in Setup. If no usable MIDI notification subscription is established, collect firmware version, discovery result and pairing observations instead of assuming a generic Bluetooth connection means MIDI is ready. The audited Melodee supports LE legacy Just Works; direct editor SysEx is not available in CYDaux.

## Musical controls and live state

| Symptom | Investigation |
| --- | --- |
| Notes sound on wrong track | Match human MIDI channels 1–16; inspect firmware routing and selected track fallback. |
| SENT but no audible effect | Check MIDI LEARN overrides, selected engine, MIDI input mode, level, routing and documented parameter availability. SENT is not an acknowledgement. |
| Physical knobs do not update CYD | Confirm a real MIDI input port or BLE notification path and enable the firmware's documented CC output. A firmware need not echo received CCs. |
| XY cursor missing | At least one axis may be UNKNOWN; this is intentional until values are established. Drum XY is disabled for SLOOP. |
| SLOOP transport disabled | Verify real bidirectional MIDI, correct source/version and valid INFO/LIVE. Older protocol and incompatible layouts intentionally remain controller-only. STATE WAIT is not confirmed playing state. |
| Melodee/Skiff Play unavailable | Expected: CYDaux does not generate clock; use local PLAY. |
| X0X controls missing for drums/mix | Expected: initial profile covers two 303 parts only. |
| Held note after loss | Use local FM-1 stop/release; if reachable use Panic. Do not expect a disconnected cable or radio to carry release messages. Log whether the link was actually reachable. |

The [firmware audit](firmwares.md) is the source of truth for mappings, timing and exceptions. Do not use repeated write commands to guess state or replay mutations on reconnect.

## Capture useful evidence

Use the [session record](hardware-session-template.md) and attach sanitized serial logs, MIDI hex frames when available, screen photographs, exact source manifest, firmware/engine/channel settings, observed vs expected result, and shortest repeatable action sequence. Specify whether this was desktop demo, PC mock, physical CYD-to-mock, direct BLE hardware, or real USB bridge hardware. Avoid publishing tokens, credentials or unrelated MIDI data. Record PASS/FAIL/PENDING per check; a workaround is not a pass of the original path.
