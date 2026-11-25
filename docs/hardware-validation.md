# Hardware acceptance: ESP32-2432S028R and FM-1

**Physical acceptance is pending.** The FM-1 has not arrived. Native controller,
renderer, protocol and socket tests establish software behavior; they do not
establish display wiring, touch accuracy, Bluetooth pairing, USB behavior,
musical response or latency on this board and instrument.

Use this checklist to turn the current prototype into a recorded hardware
result. Board-only checks and a Wi-Fi session with the PC mock can be completed
before the FM-1 arrives. Follow the repository's build/flash instructions for
the CYD and each firmware project's own installation instructions for the FM-1.
Selecting a CYDaux profile does not install or change the FM-1 firmware.

## Record the exact setup

Fill this out for each accepted firmware and connection route:

| Item | Observation |
| --- | --- |
| Date and tester | Pending |
| CYD board marking, revision and photo | ESP32-2432S028R; physical revision pending |
| CYDaux Git commit and binary filename | Pending |
| FM-1 firmware name, displayed version and source/build revision | Pending |
| FM-1 MIDI routing, channels, engine/patch and CC output setting | Pending |
| Route: direct BLE or CYD Wi-Fi → PC → USB MIDI | Pending |
| Computer OS, Python, MIDI backend and exact port names | Pending if using bridge |
| Display/touch, note release, control response and reconnect result | Pending |
| Captured MIDI, screen photos and exceptions | Pending |

Do not include Wi-Fi passwords or bridge tokens in a public test record.

## Board-only display and touch

The current wiring target is declared in
[board_config.hpp](../firmware/board_config.hpp), not auto-detected:

| Interface | Declared pins |
| --- | --- |
| ILI9341 display on HSPI | SCK 14, MOSI 13, MISO 12, CS 15, D/C 2, backlight 21 |
| XPT2046 resistive touch on a separate VSPI bus | SCK 25, MOSI 32, MISO 39, CS 33, IRQ 36 |
| Runtime recalibration button | BOOT, GPIO 0 |

1. Power the CYD through its normal USB connection with a known working cable.
   Confirm the expected serial port is available and retain the boot log at
   115200 baud. A startup message reports the free heap; record it for later
   comparison while connecting radios.
2. Confirm a landscape 320 × 240 display. The interface should match the
   [native screenshots](../simulator/README.md#actual-display-examples): dark
   background, mint highlights and amber DEMO badge. Check that every corner,
   footer and tab is visible without mirrored text or swapped colors.
3. On first use, press and hold the three calibration crosses, then the separate
   fourth check point. Release between points. The fourth point must pass the
   independent check before the main UI appears. This validates the particular
   panel; successful unit tests of the affine math do not replace this step.
4. Tap each bottom tab, the firmware selector, track selector and bank selector.
   Drag all four Play controls and the XY pad to each edge. Verify values cover
   0–127 and a drag never turns into a profile or page change.
5. Press NOTE, slide the finger away, then lift it. Repeat on Play and XY. The
   HELD indication must clear. Test a light press and a firmer press; record
   accidental releases or contacts that remain held after the finger is gone.
6. Reboot and confirm the saved calibration still aligns. To recalibrate while
   running, hold BOOT for about two seconds, release it when calibration starts,
   and repeat the crosses. Holding BOOT while powering up is a separate ESP32
   bootloader action; the recalibration gesture is performed after normal boot.

If the display is blank, mirrored or badly colored, stop this acceptance stage
and record the actual PCB/display revision. A successful compile does not prove
that a different CYD variant uses the declared controller and pins.

## Setup portal and local Wi-Fi mock

1. Open **Link → Setup**. The screen must show the board's `CYDaux-…` network,
   its exact case-sensitive password, `http://192.168.4.1`, and a large Back
   button. Join that network from a phone or computer and open the address.
2. Confirm only Back reacts on the CYD while setup is shown; touches where
   ordinary profile, parameter and tab controls normally sit must do nothing.
   Tap Back and confirm the setup network closes and the ordinary page returns.
3. Open Setup again. Save the 2.4 GHz network name, password, computer address,
   bridge port and token using [the bridge guide](bridge.md). A blank password
   or token field keeps its saved value. The form's explicit open-Wi-Fi checkbox
   clears the Wi-Fi password. Save and confirm the CYD exits setup.
4. Start the PC bridge in LAN demo mode, select the matching profile on the CYD,
   choose Wi-Fi and Connect. Confirm **MOCK LINK**, never WIFI LIVE, is shown.
   `0.0.0.0` is the bridge's listen address; the CYD must use the computer's
   reachable LAN address.
5. Exercise note, mixer, XY and connection controls for several minutes while
   watching touch responsiveness. Unlink, reopen Setup, save again and reconnect.
   The interface must remain usable while discovery or connection is pending.
6. For a SLOOP mock, verify INFO/LIVE negotiation and Play/Stop behavior. This is
   useful CYD/network integration evidence, but the FM-1 route remains pending.

The browser simulator's `CYDaux-DEMO` setup screen does not create a Wi-Fi
network. Only the physical CYD creates the setup access point.

## Direct BLE with Melodee

Use the [audited Melodee profile and routing notes](firmwares.md). Record the
actual installed version; a newer build needs its own acceptance record.

1. Enable Melodee's BLE radio and **VISIBLE** mode. Use the intended four-track
   MIDI routing and match the CYD channel shown for the selected track.
2. Choose Melodee, BLE and Connect on the CYD. Observe discovery, connection and
   MIDI notification subscription separately. Confirm the final BLE LIVE badge
   appears only after a usable MIDI connection is established.
3. Test with a single discoverable Melodee first. If several match, verify the
   CYD asks for an exact BLE address in Setup instead of selecting one silently.
   Record any pairing prompts, failures and whether a second connection works
   without resetting either device.
4. Hold and release NOTE on both Play and XY. Confirm the intended track sounds
   and stops. Repeat at least ten times, including sliding off the original
   button before release. Record audible delay or missed/stuck notes.
5. Change Level, Pan and a supported engine's Tone/Resonance. Observe the FM-1
   itself, since a SENT label proves transport acceptance only. Test another
   track and confirm the original track is not unintentionally changed.
6. Disconnect cleanly while holding a note. Then test an unexpected connection
   loss separately. Confirm the CYD clears HELD and invalidates old values.
   If the radio path has already failed, the CYD cannot deliver a release over
   that broken path; stop any remaining sound locally on the instrument and
   record the result. Reconnect, press PANIC while connected and verify silence.

Melodee BLE is a performance-MIDI route. Editor SysEx, backups and updates are
outside it. Melodee's Play control is disabled on a real connection because this
project does not generate the external clock required by its MIDI transport
path; use the FM-1's own PLAY button for that firmware.

## USB bridge with SLOOP, Skiff and X0X

Start with exact MIDI input and output names from `python -m bridge --list`,
following [bridge.md](bridge.md#connect-an-actual-fm-1-over-usb). A USB data cable
joins the FM-1 to the computer; the original CYD's USB connector is not the USB
host for this arrangement. Use a real bridge session, not `--demo`.

For each installed firmware, first verify WIFI LIVE, correct channel routing,
note on/off, Level and one audible engine parameter. Repeat a clean Unlink and a
USB cable removal/reconnection. The bridge must wait for the same explicitly
chosen ports, and old queued notes or control changes must not replay.

| Profile | Additional acceptance checks |
| --- | --- |
| SLOOP | Start with channels 1, 2, 3 and 10. Verify valid SLOOP INFO plus LIVE protocol 12 negotiation before transport is enabled. Confirm a returned playing state agrees with the FM-1, then request Play and Stop and check the physical result. Unsupported drum Resonance/Attack/Release and the drum XY pair must stay disabled. |
| Skiff | Check all six track channels and the second mixer bank. Enable the intended CC output behavior if testing physical-knob readback. Verify a received mapped CC changes only the matching track. Program Change is a request, not preset readback. Use the FM-1's local PLAY; remote button/knob emulation is not implemented. |
| X0X | Test 303 A on channel 2 and 303 B on channel 3. Check Cutoff, Resonance and the remaining native 303 CC bank. Enable CC OUT = KNOBS or ALL for physical-change feedback if desired. Test MIDI Start/Stop independently from parameter readback. Other X0X roles are outside this profile. |

For SLOOP, a supported identity alone does not establish current playing state.
**STATE WAIT** means a valid state reply is still needed; **STATE READY** means
the controller has current validated playing-state information. A timeout must
disable unconfirmed transport actions rather than invent a result.

## State origins, reconnect and soak checks

| Displayed origin | What to verify physically |
| --- | --- |
| UNKNOWN | Newly connected or invalidated values stay blank; XY hides its cursor while either axis is unknown. |
| QUEUED | A changed value has not yet been accepted by the actual transport. It must not be labelled SENT merely for entering a software queue. |
| SENT | A transport write succeeded. Confirm the effect separately on the FM-1; a firmware may ignore a control because of routing, engine or learn settings. |
| DEVICE | A mapped value arrived from a real peer. Change a physical control with appropriate CC output enabled and verify the CYD follows it. This does not imply all instrument state is synchronized. |
| DEMO | Local demo and mock-provided values remain visibly simulated. They must not become DEVICE merely because mock MIDI traversed the network. |

Test switching tracks and banks after receiving different values. XY always
targets the selected track's first two parameters; switching its displayed
control bank must not relabel or remap those axes. Changing firmware profile,
transport or connection invalidates stale state.

Finally, run each accepted route for at least 15 minutes with repeated value
drags, notes, page changes and reconnects. Record responsiveness, disconnects,
unexpected notes and free-heap observations if collecting serial logs. Choose a
repeatable note gesture and record perceived/measured latency with the test
method; no latency or throughput figure is currently claimed for physical
hardware.

## Acceptance ledger

| Check | Status | Evidence to attach |
| --- | --- | --- |
| Board power, display orientation and color | Pending | Board photo, UI photo, boot log |
| Touch calibration, persistence and edge controls | Pending | Calibration result and touch observations |
| Exact-case setup credentials and Back/Save flow | Pending | Successful network join and portal exit |
| CYD Wi-Fi to profile-aware PC mock | Pending | MOCK LINK photo and connection notes |
| Melodee discovery, pairing and subscription | Pending | Actual firmware/version and BLE observations |
| Melodee notes, controls and reconnect | Pending | Track/channel settings and physical behavior |
| SLOOP USB controls and validated LIVE | Pending | Version, port names and observed state/transport result |
| Skiff USB controls and optional CC feedback | Pending | Routing and physical-change observations |
| X0X 303 controls and optional CC feedback | Pending | 303 channels, CC OUT setting and observations |
| Held-note loss, panic and no stale replay | Pending | Result for each route and failure type |
| Fifteen-minute interaction/connection soak | Pending | Duration, failures and timing/heap notes |

Mark only the exact firmware revision and route that passed. Leave untested
routes pending; a mock or one firmware's success does not qualify another.
