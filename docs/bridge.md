# PC bridge and simulated FM-1

The bridge lets the CYD use Wi-Fi to reach an FM-1 connected to a Windows or
Linux computer by USB. The FM-1 itself does not need Wi-Fi firmware. The same
program can simulate a peer while the instrument has not arrived.

Run all commands below **from the repository root**, using Python 3.10 or newer.
On Windows, use `py` instead of `python` if that is your installed Python launcher.
The commands here are the same on both platforms unless a difference is shown.
The bridge is included in the source checkout; the downloadable CYD firmware
and desktop simulator packages are separate artifacts.

## Start without an instrument

```console
python -m bridge --demo --profile sloop
```

Demo mode uses only the Python standard library. It does not import Mido, open
MIDI ports, generate audio, or require an FM-1. Its initial reply explicitly says
`"mode":"mock"` and `"name":"Demo FM-1"`. Simulated responses must remain labelled
as mock in the CYD; a successful mock session is not physical compatibility proof.

The bridge prints the connection token and listens on `127.0.0.1:45821`, which is
reachable only from the same computer. In another terminal, check the connection
using the printed token:

```console
python -m bridge.probe --token PASTE_THE_PRINTED_TOKEN
```

The probe authenticates and pings. It sends **no MIDI commands**. It exits after
the check so that the CYD can connect; the bridge accepts one active controller.

To let the CYD connect over your local Wi-Fi network, restart the bridge with an
explicit LAN listening address:

```console
python -m bridge --demo --profile sloop --bind 0.0.0.0
```

Set the CYD's bridge host to the computer's LAN IPv4 address, port to `45821`, and
token to the value printed by that run. `0.0.0.0` is a listening address, not the
address to enter on the CYD. On Windows, `ipconfig` shows the active adapter's IPv4
address. Allow this Python application through Windows Firewall for your private
network if prompted. Both devices need a network route to one another; Wi-Fi
client isolation can prevent that even when both are on the same SSID.

The token changes at every start unless `--token` or the `FM1_CYD_TOKEN`
environment variable supplies one. It controls local access; this TCP protocol
does not encrypt traffic. Keep LAN use on a network you trust.

## Connect an actual FM-1 over USB

Install the optional MIDI libraries:

```console
python -m pip install -r bridge/requirements.txt
python -m bridge --list
```

`--list` prints two lists. **MIDI output** means the port to which the bridge sends
commands; **MIDI input** means the port from which it receives the instrument's
responses. Copy the exact names displayed on your computer:

```console
python -m bridge --output "EXACT MIDI OUTPUT NAME" --input "EXACT MIDI INPUT NAME" --bind 0.0.0.0
```

You can omit `--input` for control only. SLOOP INFO/LIVE readback needs the input
port. The names are deliberately placeholders here: no FM-1 has been attached in
this development environment, so an actual Windows/Linux port name is not claimed.

The bridge never chooses the first available device. Duplicate exact port names
are rejected as ambiguous. If a named port disappears,
it reports `ready:false` and checks every two seconds for **the same exact name**.
Commands attempted while the port is unavailable produce an error and are
discarded. They are not replayed on reconnect. Each USB input-port lifetime
has a separate bounded callback queue; late callbacks from a retired port
cannot appear as readback from a reopened port. CY1 emergency release tracking
ignores duplicate/older sequences and malformed operation envelopes rather
than assuming their mutations took effect. On Linux, MIDI names can include
ALSA client numbers that change across sessions; rerun `--list` and restart with
the new exact name if necessary.

Close another application if it holds the required MIDI port exclusively. The
bridge creates no virtual MIDI port, which avoids requiring a Windows virtual-port
driver. Incoming device messages go to the network client; they are never
automatically written back to MIDI.

## Profiles and honest state

`--profile sloop|skiff|melodee|x0x` chooses the mock profile. The optional network
`profile` frame changes mock behavior; in real MIDI mode it is metadata only.
Neither operation changes the firmware installed on an FM-1.

The profile-aware mock accepts the qualified musical controls but does not echo
CCs as fake device confirmations. Unsupported mock CCs and program changes
produce an error. SLOOP answers the audited INFO request and 56-byte LIVE replies
with simulated transport and playhead state. It identifies its source fixture as
`FELUCCA SLOOP 2.8.5`, with editor protocol 12, while the enclosing connection is
always labelled `mock`. INFO is not a real-device identity claim. Meters remain
zero because the mock generates no audio. Other profiles remain controller-only.
The SLOOP drum track ignores synth resonance/envelope controls and program values
outside its 46-kit range, matching the pinned source rather than inventing results.

The bridge does not infer that an instrument accepted a command because a USB
write succeeded. It sends no success acknowledgement for a MIDI frame. Real
readback comes only from the selected MIDI input port, and the CYD must validate
the firmware-specific response. This permits controller-only use when a firmware
has no supported readback protocol.

## Connection behavior and bounds

The shared protocol is in [CONTRACT.md](CONTRACT.md). The implementation uses:

- One controller session, with an authenticated protocol-1 hello before MIDI.
- TCP port `45821` and LF-delimited UTF-8 JSON objects, at most 2048 bytes per line.
- One complete MIDI message per frame, at most 256 bytes, including both SysEx
  delimiters. Running status and split SysEx are not accepted at this boundary.
- A 128-message MIDI input queue and a 16 KiB network output buffer. A controller
  that stops reading is disconnected when the output buffer fills.
- Six seconds without a complete client frame before disconnect. The CYD should
  ping every two seconds; responses echo the numeric ping ID.
- Sustain-off and all-notes-off on channels where the ending session sent a
  nonzero-velocity Note On, when the MIDI endpoint is still reachable. A control-only
  session does not silence another keyboard's notes. Disconnects cannot reliably
  send anything through an already-unplugged device.

No outgoing performance queue survives a disconnect. A new connection starts with
fresh authentication; the application must reacquire state. Press Ctrl+C to stop
the bridge and release the session's channels.

The bridge relays valid bounded MIDI including SysEx. There is **no firmware
flashing, recovery, backup, bulk sample-transfer or updater workflow** in this
application. Large SysEx transfers outside the 256-byte message limit are rejected.
This is an editor/control transport, not a general unlimited SysEx librarian.

## Verification and platform limits

```console
python -m unittest discover -s tests/bridge -v
```

The tests run without optional MIDI libraries and include actual localhost TCP
connections, authentication failure, partial frames, bad MIDI, backend loss,
reconnection without command replay, timeout release, and the single-controller
rule. USB backend behavior is exercised with a fake Mido module, not claimed as
hardware acceptance. The CI workflow enables these checks on Linux and Windows.
An actual FM-1 on USB and the CYD's radios still require physical testing.

After building the native simulator, enable the additional integration tests to
relay its actual shared C++ controller messages through a real localhost bridge:

```console
# Linux / bash, from the repository root
FM1_CYD_SIMULATOR=build/fm1-simulator python -m unittest discover -s tests/bridge -v
```

```powershell
# Windows PowerShell, from the repository root
$env:FM1_CYD_SIMULATOR = "build\fm1-simulator.exe"
py -m unittest discover -s tests/bridge -v
```

These additional cases verify INFO negotiation, LIVE request/reply, actual UI
Play/Stop actions, protocol-11 fallback, and the distinction between a sent
control and readback. Their peers remain simulated. Without the environment
variable those two cases are explicitly skipped, and the ordinary bridge tests
still run.

See [the software qualification matrix](testing.md) for the complete 38-case
inventory and the boundaries covered by the native-controller TCP tests.

Dependency references:

- [Mido port behavior](https://github.com/mido/mido/blob/main/docs/ports/index.rst)
- [Mido RtMidi backend](https://github.com/mido/mido/blob/main/docs/backends/rtmidi.rst)
- [RtMidi input implementation and SysEx handling](https://github.com/mido/mido/blob/main/mido/backends/rtmidi.py)
