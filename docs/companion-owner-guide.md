# Evaluate the common companion

## Start on Windows

Download the Windows evaluator artifact from the mission PR's successful build.
Extract the entire ZIP. Double-click `CYDaux-Evaluator.exe` or
`Open-Simulator.cmd`. Keep its console open; your default browser opens the local
interface. The standalone Windows package includes its Python runtime and needs
no separate Python installation, C++ compiler, CYD or FM-1.

Choose **Xenoresonator** or **ZaaggenZ** in the instrument selector, wait for
**Native engine**, then click **Enable sound**. Start at a comfortable output
volume. The sound comes from the actual source-pinned native engine. This is
not an emulated FM-1 CPU or a physical-device measurement.

## A useful first performance

- On **PLAY**, drag a parameter card, then hold **NOTE** to audition it. ZaaggenZ's
  **Grit**, **Vowel**, **Motion** and **Character** change its procedural voice;
  bank 2 holds attack, release, body and sub.
- On **XY**, leave **GATE ON** and touch/drag the pad. One finger starts the
  note, controls both axes and releases it on lift. Use **EXPAND** for the large
  performance surface. **BACK** and **PANIC** remain available.
- On **SEQ**, start the local loop. Tap a step to select/toggle it; use the
  pitch, velocity and gate controls to edit the selected step. Change tempo or
  length and hear the result. This pattern runs in CYDaux, not inside a remote
  FM-1 sequencer.
- On **BANK**, audition the six native ZaaggenZ presets or four Xenoresonator
  materials. Xenoresonator is percussive: each new touch or sequence step strikes
  its resonant bodies. Tap again or run **LOOP** for repeated excitation. Holding
  the pad changes excitation and damping; **Coupling** is a separate PLAY
  control. Moving the pad does not continuously retrigger the strike. **HELD**
  describes the input gate, so a fully decayed resonator can be silent while
  that label remains visible.
- On **STATE**, inspect engine-derived peaks. The source label tells you where
  the state came from; a state plot is not a measurement of a physical board.
- Press **PANIC** during a held note or loop. Audio and sequence should stop.
  Switch instruments, reconnect, and confirm an old note does not return.

The browser keyboard offers Space for held audition when the screen is focused.
Pointer release outside the screen, hiding the browser tab and loss of focus
release the performance. After returning to the page, click **Enable sound**
again. A stopped/abandoned audio consumer also triggers a server watchdog.

## Switch profiles on the shared touchscreen

On **LINK**, tap the firmware name to open the profile list. Tap Back or the
current profile to keep the existing link; opening the menu releases held notes
and stops the companion sequence. Select a different profile to retire the old
link. Explicitly reconnect to the matching instrument and transport before
playing a fresh note. Choosing a profile never installs firmware or implies
physical compatibility. Chronomorph and Ecophon are separate native-only
editors in the desktop evaluator, not CY1 device profiles.

## What the labels mean

**NATIVE** is readback from the actual native audio engine. **DEMO** is a silent
interface preview. **UNKNOWN** means no value has been reported. **QUEUED** and
**SENT** describe delivery progress; neither proves instrument state. Legacy
Bluetooth/Wi-Fi links in the browser are mocks for interface testing. They do
not open a real radio or MIDI port.

The four original firmware profiles remain available. Their controls and
supported routes are unchanged. Unmodified Fune does not implement the new CY1
endpoint: choosing a new profile does not add controls to that firmware.

If the endpoint reports a lost or incompatible session, performance controls
are disabled and the loop stops. Use **Refresh** on Link to reconnect the whole
transport and discover a fresh session. Previously held notes and edits are
not replayed. If the endpoint is still incompatible, the controls remain disabled.

## Linux or source use

The Linux package needs Python 3.10+ and its included native executable:

```sh
chmod +x fm1-simulator
python3 run.py
```

For another local port, add `--port 8782`; to open the URL yourself, add
`--no-open`. Close the console with Ctrl+C when finished. See
`simulator/README.md` for source builds and exact automated checks.

## CYD installation and first physical session

The separate firmware package is for the original **ESP32-2432S028R** only.
It includes bootloader, partition table, application, fixed offsets, manifest,
hash verifier and the existing explicit-port flashing script. Read its included
README and run `python flash.py --check` first. Flash only the CYD you identify;
do not use the package for another CYD variant or for the FM-1.

After owner-controlled installation, calibrate the resistive touch screen and
check all four corners. Reboot and confirm calibration persists. Use the setup
portal to configure the existing BLE or authenticated Wi-Fi/USB bridge route.
The device's Native option is disabled: it does not contain a local synthesizer.

Record the first hardware session using `hardware-session-template.md`. Check
touch geometry, single-finger XY release, panic, disconnect during a note/loop,
reconnect without replay, power/reboot recovery and the serial heap/stack/timing
diagnostics. Test basic legacy MIDI separately from a negotiated extended
endpoint. Do not mark FM-1 audio, radio latency, runtime headroom or endpoint
compatibility as passed until measured on the actual pairing.

No hardware was flashed or physically qualified by this mission. FM-1 target
research/evaluator artifacts have separate distribution and recovery gates;
the CYD firmware package does not authorize flashing an FM-1 image.
