# Firmware profiles and protocol audit

Audit date: **25 November 2026 (UTC)**. These profiles are **source qualified**.
The FM-1 has not arrived, so no physical CYD-to-FM-1 connection, pairing,
latency, or sound change has been qualified. The implementation and fixtures
are original interoperability work; upstream firmware code is not vendored.

## Source pins

| Profile | Audited revision | Scope in CYDaux |
| --- | --- | --- |
| Melodee 1.6.1 | [165d177a][mel-pin] | Four routed tracks; performance MIDI over BLE or USB bridge |
| SLOOP 2.8.5 | [bac9c05e][sloop-pin] | Three synth tracks and drums; USB bridge; small LIVE subset |
| Skiff 2.7.1 | [5eab2136][skiff-pin] | Six tracks; USB bridge |
| X0X | [6515e77d][x0x-pin] | The two 303 parts; USB bridge |

The X0X label is a source pin, not an inferred release number. Full commit
and blob hashes, immutable source URLs, and implemented mappings are in
[upstream-sources.json](upstream-sources.json). A future firmware release is
not automatically a new qualification result.

## Connections and state

| Firmware | Direct BLE-MIDI | Wi-Fi route | Feedback boundary |
| --- | --- | --- | --- |
| Melodee | Implemented upstream for performance MIDI | CYD → PC bridge → FM-1 USB MIDI | BLE does not carry editor SysEx; a sent CC is not an acknowledgement |
| SLOOP | Not supported by the audited upstream | CYD → PC bridge → FM-1 USB MIDI | Validated INFO and LIVE replies provide transport and live state |
| Skiff | Not supported by the audited upstream | CYD → PC bridge → FM-1 USB MIDI | Configurable CC output can report changes; received CCs are not echoed |
| X0X | Not established in the audited upstream | CYD → PC bridge → FM-1 USB MIDI | CC OUT = KNOBS/ALL reports mapped changes; it is off by default |

Wi-Fi belongs to the CYD-to-PC link. These profiles do not imply that the
FM-1 exposes a Wi-Fi API. Use explicitly selected **USB MIDI input and output**
ports for the bridge, since a one-way input path cannot return state.

The UI treats demo values, queued values, successfully sent values, and
device-reported values separately. Sending a MIDI CC does not prove that the
instrument applied it: engine selection, routing, MIDI LEARN, or an unsupported
parameter can change the outcome. Connection and profile changes invalidate
old values. A mock bridge is always identified as simulation.

Sources: [Melodee BLE documentation][mel-ble], [SLOOP protocol][sloop-proto],
[Skiff CC dispatch][skiff-cc], [X0X CC implementation][x0x-cc].

## Implemented control banks

All CC values are MIDI's 0–127 wire values. Slider starting positions are
**demo seeds**, not claims about a connected patch. Mix level uses CC7
independently of the eight-parameter bank.

| Slot | Melodee | SLOOP / Skiff | X0X 303 A/B |
| --- | --- | --- | --- |
| 1 | Tone, CC74 | Tone, CC74 | Cutoff, CC16 |
| 2 | Resonance, CC71 | Resonance, CC71 | Resonance, CC17 |
| 3 | Attack, CC73 | Attack, CC73 | Envelope amount, CC18 |
| 4 | Release, CC72 | Release, CC72 | Decay, CC19 |
| 5 | Level, CC7 | Level, CC7 | Accent, CC20 |
| 6 | Pan, CC10 | Pan, CC10 | Wave, CC21 |
| 7 | Decay, CC75 | Reverb send, CC91 | Drive, CC22 |
| 8 | Glide, CC5 | Delay send, CC94 | Slide, CC23 |

Tone and resonance are engine-dependent controls. Melodee and Skiff ignore
the engine control where the selected engine has no suitable mapping.
SLOOP maps CC74 to an engine control where available; otherwise it uses its
track filter, whose centre value 64 means off, lower values low-pass and
higher values high-pass. Envelope mapping can also follow an engine's
native envelope. Pan 64 is centre.

On SLOOP's drum track, the implemented bank's useful controls are **Tone,
Level, Pan, Reverb, and Delay**. Resonance, Attack, and Release are disabled in CYDaux because the drum handler ignores them. Other engine-specific controls cannot be confirmed
without knowing the selected engine.

Melodee does not implement SLOOP's CC91/93/94 send mapping in its fixed
parameter handler. X0X's CC93 means **delay**, while SLOOP and Skiff use
CC93 for chorus. X0X's alternate 303 inputs CC74/71/75 are received but not
emitted as feedback, so CYDaux uses the native CC16/17/19 map.

Sources: [Melodee controls][mel-cc], [SLOOP MIDI handler][sloop-seq],
[Skiff controls][skiff-cc], [X0X map][x0x-cc] and [manual][x0x-manual].

## Channel setup and firmware differences

Channel numbers in the UI are **1–16**. MIDI status bytes encode them as
channel minus one. Match the instrument's current settings before playing.

**Melodee:** default CH1-4 routing maps channels 1–4 to the first four tracks.
Alternatives CH5-8, CH9-12, CH13-16 and SEL change that mapping. DRUM and MPC
settings may also change note routing. Its learned CC mappings take precedence;
the contextual panel-knob map is not a permanent parameter bank. The audited
profile does not claim Program Change support. For direct BLE, enable the
radio and VISIBLE mode. Upstream supports one BLE link, LE legacy pairing,
and Just Works for a peer without a keyboard. The central must not demand
LE Secure Connections. Complete the MIDI notification subscription before
sending performance messages. Editor requests, imports and updater traffic
are excluded from that BLE path. [Routing][mel-seq], [MIDI LEARN][mel-knobs],
[BLE behavior][mel-ble].

**SLOOP:** default channels are 1, 2, 3 and 10 for the three synth tracks and
drums. OTHER = SEL adds a selected-track fallback; OTHER = OFF disables it.
IN = CLOCK ignores ordinary notes and CCs, and IN = CH n changes routing to
the selected track. MIDI LEARN can override fixed CCs. Program Change
0–127 selects a sound; on the drum track, values 0–45 select one of the 46
kit slots at this source pin, and higher values are ignored. The existing
Bank Select latch affects synth Program Change.
CYDaux does not reset that latch or claim preset readback. [Routing][sloop-seq],
[Program Change handling][sloop-ui], [drum kit definitions][sloop-drums],
[generated kit list][sloop-kits].

**Skiff:** defaults are channels 1–6, with configurable per-track routing,
MIDI IN = SEL and AUTO CHANNEL options. Channel 10 is not reserved as a drum
role. Program Change support is enabled; the resulting preset is not
acknowledged by a socket send. REMOTE is a separate channel and defaults
off. Its CC102–115 emulate held panel buttons; CC112 is PLAY and CC113 REC.
Since 2.5, CC116–119 and CC85/86/87 provide relative knob/algorithm/preset/select
moves. Those actions require a known configured remote channel and context,
so this profile does not send them. [Control and Program Change dispatch][skiff-cc],
[routing][skiff-seq].

**X0X:** CYDaux exposes **303 A on channel 2 and 303 B on channel 3**. The
firmware also has 909, 808, Break and Mix roles with different CC meanings;
those roles are deliberately outside this profile. CC OUT = KNOBS emits
physical edits; ALL includes automation. Incoming CCs are never echoed as
acknowledgements. CC120/123 release MIDI-held bass/break notes; sustain CC64
has no mapped meaning. [Manual][x0x-manual], [engine handler][x0x-engine].

## Transport behavior

CYDaux does not generate MIDI clock pulses. In the audited **Melodee and
Skiff**, incoming MIDI Start/Stop is accepted only from the selected
external clock source. INT ignores it; an external Start without clock
pulses cannot advance normally and reaches the lost-clock timeout after
500 ms. Their generic transport capability is therefore disabled. Use
the FM-1's PLAY button. [Melodee clock routing and timeout][mel-seq],
[Skiff clock routing and timeout][skiff-seq].

**SLOOP** uses its validated LIVE command for remote transport. An explicit
play or stop request goes through the instrument's own transport path and
the next state reply establishes the result. **X0X** handles MIDI Start,
Continue and Stop directly, and its sequencer falls back to the internal
clock when external pulses cease. The profile records that capability;
a sent realtime command alone still does not establish playing-state
readback. [SLOOP LIVE source][sloop-live], [X0X realtime handler][x0x-engine],
[X0X clock explanation][x0x-manual].

## SLOOP INFO and LIVE contract

This is a SLOOP extension. The shared Felucca-family `FL` header and a USB
port name such as “Felucca” are not sufficient identity. X0X retains an
inherited editor protocol document; executable handlers take precedence.

Every message is a complete bounded SysEx frame:
`F0 7D 46 4C command arguments F7`. Interior bytes must be seven-bit.
MIDI realtime bytes interleaved in a byte stream belong to the MIDI stream
parser and are not inserted into the SysEx body.

| Action | Complete request, hex |
| --- | --- |
| INFO | `F0 7D 46 4C 01 F7` |
| Read LIVE | `F0 7D 46 4C 4D 00 F7` |
| Explicit stop | `F0 7D 46 4C 4D 01 00 F7` |
| Explicit play | `F0 7D 46 4C 4D 01 01 F7` |

The pinned build's INFO reply contains:

- ASCII `FELUCCA SLOOP 2.8.5`, terminated by zero.
- Five bytes: engine count 12, parameter count 71, global count 50,
  step count 64, first engine-parameter index 63.
- Twelve zero-terminated engine names: ANALOG, DIGITAL, PHASE, LOFI,
  SAMPLE, VOICE, TRIO, WHEEL, GRAIN, FM6, PHYS, NOISE.
- Track count 4, editor protocol version 12, then `F7`.

The complete current INFO fixture is 104 bytes. Validate the envelope,
bounded printable strings, stated engine count, parameter layout and exact
tail shape. A SLOOP identity needs a well-formed release number. LIVE requires
SLOOP 2.7 or newer, reported protocol 12 or newer, four tracks, and the
audited 64-step layout. A missing protocol byte, other firmware identity,
malformed reply, or unsupported layout must leave LIVE unavailable. Custom
build names that remove the SLOOP marker remain controller-only.

A LIVE reply is **56 bytes total**: the five-byte header through command77,
the operation byte, **49 state bytes**, then `F7`. The parser supports
operations 0 and 1 and this current layout. Early development layouts and
unrecognized extensions are rejected.

Offsets below start **after the operation byte**, at the first state byte:

| State offset | Meaning |
| --- | --- |
| 0 | Flags: bit0 playing; bit1 transport requested; bit2 song mode; bit3 song playing; bit4 fill now; bit5 fill next; bit6 song record |
| 1 | Flags2: bit0 record waiting; bit1 undo; bit2 redo; bit3 free take |
| 2–3 | BPM, little-endian seven-bit chunks, biased by 8192 |
| 4–5 | Playing/requested section +1; zero means none, 1–4 mean A–D |
| 6–7 | Chain entry count and current index |
| 8–10 | Mute, solo and recording track masks |
| 11 | Selected track, 0–3 |
| 12–13 | Punch effect +1 and CPU meter |
| 14–17 | Song entry, entry count, bar and entry bars |
| 18 | Stored-section bitmask |
| 19–30 | Four step/length/meter triplets |
| 31–32 | Master override and throw-effect mask |
| 33–48 | Sixteen packed drum tune/decay settings |

BPM is `(low | (high << 7)) - 8192`, validated to 40–240. Step values
are 0–63, pattern lengths 1–64, and meters 0–127. The stored section keeps the
wire convention 0=none, 1–4=A–D. The parser's recording flag includes armed
recording and a free take. Invalid frames do not partially overwrite state.

Only one editor request may be outstanding: upstream stores one incoming
SysEx request. Accept identity/state replies only for the current connection,
profile and pending request. Polls may recover after a timeout; do not replay
a play/stop request automatically. Reconnect clears pending requests and
capabilities. Do not equate a request being sent with its effect being
confirmed.

Two upstream documentation differences are recorded in the implementation:

- The protocol document says “50 bytes after op”. The serializer emits 49;
  its own `ui_pages_test.c` asserts 50 **including** the operation byte.
- REC is documented without arguments, but the mutation dispatcher requires
  at least two argument bytes. REC and other nonessential mutations are
  outside this implementation.

Sources: [protocol][sloop-proto], [INFO serializer][sloop-editor],
[LIVE serializer][sloop-live], [upstream LIVE assertions][sloop-tests].

## Fixtures and first hardware checks

[The JSON fixture](../tests/fixtures/sloop_v12.json) records a source-shaped
INFO and LIVE packet. It is **synthetic**, not a captured device response.
The C++ profile/protocol tests exercise exact mappings, capability flags,
identity spoofing, truncation, wrong envelopes, field bounds, tempo boundaries,
unsupported operations and preservation of prior state on invalid input.

When the instrument arrives, record the firmware version and channel/routing
settings. Verify level, pan, a supported tone control, and note-on/note-off
against the FM-1 itself. Test connection loss while a note is held, reconnect,
and panic. For Melodee, record discovery, pairing, subscription and delivery
separately. For SLOOP, compare reported BPM/playing state with the FM-1, request
stop, and check whether changes made on the instrument reach the CYD.
Those physical observations are required before calling a profile hardware
qualified.

[mel-pin]: https://github.com/keremimo/melodee/commit/165d177a425944f3d30b0b669255a6ed0050c3a5
[mel-cc]: https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/midi_control.c
[mel-knobs]: https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/midi_knobs.c
[mel-seq]: https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/firmware/src/seq.c
[mel-ble]: https://github.com/keremimo/melodee/blob/165d177a425944f3d30b0b669255a6ed0050c3a5/docs/BLE_MIDI.md
[sloop-pin]: https://github.com/isod89/sloop-fm1/commit/bac9c05ea7627449405aa10e48da21d75b6c8c56
[sloop-seq]: https://github.com/isod89/sloop-fm1/blob/bac9c05ea7627449405aa10e48da21d75b6c8c56/firmware/src/seq.c
[sloop-ui]: https://github.com/isod89/sloop-fm1/blob/bac9c05ea7627449405aa10e48da21d75b6c8c56/firmware/src/ui.c
[sloop-drums]: https://github.com/isod89/sloop-fm1/blob/bac9c05ea7627449405aa10e48da21d75b6c8c56/firmware/src/drums.c
[sloop-kits]: https://github.com/isod89/sloop-fm1/blob/bac9c05ea7627449405aa10e48da21d75b6c8c56/tools/gen_drumkits.py
[sloop-proto]: https://github.com/isod89/sloop-fm1/blob/bac9c05ea7627449405aa10e48da21d75b6c8c56/web/EDITOR_PROTOCOL.md
[sloop-editor]: https://github.com/isod89/sloop-fm1/blob/bac9c05ea7627449405aa10e48da21d75b6c8c56/firmware/src/editor.c
[sloop-live]: https://github.com/isod89/sloop-fm1/blob/bac9c05ea7627449405aa10e48da21d75b6c8c56/firmware/src/editor_live.c
[sloop-tests]: https://github.com/isod89/sloop-fm1/blob/bac9c05ea7627449405aa10e48da21d75b6c8c56/tests/ui_pages_test.c
[skiff-pin]: https://github.com/illuminus5/skiff/commit/5eab213620248b3af99eeeea30900fc6266128c4
[skiff-cc]: https://github.com/illuminus5/skiff/blob/5eab213620248b3af99eeeea30900fc6266128c4/firmware/src/midi_control.c
[skiff-seq]: https://github.com/illuminus5/skiff/blob/5eab213620248b3af99eeeea30900fc6266128c4/firmware/src/seq.c
[x0x-pin]: https://github.com/charlesvestal/fm1-x0x/commit/6515e77d978c23414c80162558532f0b4312dd4f
[x0x-cc]: https://github.com/charlesvestal/fm1-x0x/blob/6515e77d978c23414c80162558532f0b4312dd4f/firmware/src/app/midicc.c
[x0x-engine]: https://github.com/charlesvestal/fm1-x0x/blob/6515e77d978c23414c80162558532f0b4312dd4f/firmware/src/app/engine.c
[x0x-manual]: https://github.com/charlesvestal/fm1-x0x/blob/6515e77d978c23414c80162558532f0b4312dd4f/docs/MANUAL.md
