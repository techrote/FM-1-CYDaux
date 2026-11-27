# Common performance companion platform

This implementation extends PR #1 at
`6540bac817c78618f2d3a666e70d684b7f6216a9`. The legacy controller is retained;
the new profile/session layer and musical pages use the same renderer and touch
capture on ESP32 and desktop. The native evaluator controls actual portable
ZaaggenZ, Xenoresonator and Grimoire engines and plays their PCM.

## Implemented architecture

| Component | Authoritative source | Responsibility |
| --- | --- | --- |
| Legacy profile maps | `src/profiles.cpp` | Four audited firmware maps, plus endpoint-required extended profiles |
| Versioned metadata and CY1 | `include/fm1/companion.hpp`, `src/companion.cpp` | Identity, semantic ranges, enums, presets, capability negotiation, bounded framing and stale-reply rejection |
| Common controller | `src/controller.cpp` | State origins, command ordering/coalescing, gates, presets, note ownership, polling and reconnect |
| Local sequencer | `src/performance.cpp` | Sixteenth-note timing, fractional milliseconds, 16 editable steps, release retry and no catch-up bursts |
| Common UI | `src/ui.cpp` | Six companion pages, four unchanged legacy pages, single-touch gestures, themes and source-labeled visualization |
| Device adapters | `firmware/main.cpp`, `firmware/transport.cpp` | Touch/calibration, six 40-row strips, radio task, authenticated bridge and independent release tracking |
| Actual native instruments | `simulator/virtual_instrument.cpp`, `third_party/` | Source-pinned audio cores, bounded endpoint replies and actual engine-state telemetry |
| Browser delivery | `simulator/run.py`, `index.html`, `audio.js` | Loopback native-process host, exact framebuffer, bounded native PCM playback and abandonment lease |

No engine is linked into the CYD firmware. The desktop evaluator has a 150 KiB
framebuffer; the device uses a 25,600-byte RGB565 strip. All six strips and the
full-frame renderer are compared byte for byte in `strip_renderer_test`.

## Capability and evidence matrix

| Profile | Working path in this repository | State and controls | Boundary |
| --- | --- | --- | --- |
| Melodee 1.6.1 | Existing BLE-MIDI central and USB bridge | Audited notes/CCs; device feedback when supplied | Software/target qualified; physical pairing pending |
| SLOOP 2.8.5 | Existing USB-MIDI bridge | Audited notes/CCs/programs, INFO/LIVE protocol 12+ | LIVE enabled only after identity/protocol checks; hardware pending |
| Skiff 2.7.1 | Existing USB-MIDI bridge | Audited six-track control map | Uses instrument transport controls; hardware pending |
| X0X | Existing USB-MIDI bridge | Two 303 parts, controls and supported transport | Feedback requires the instrument's CC-out configuration |
| `zaaggenz-native` | CY1 endpoint around actual procedural ZaaggenZ engine | Eight parameters, six native factory presets, notes, XY, five stem peaks and PCM peak | Native host execution; source-image/hybrid modes excluded; not unmodified Fune support |
| `xenoresonator-native` | CY1 endpoint around actual VIBEWARE Xenoresonator | Excitation, damping, coupling, four materials/presets, notes, six modal-state peaks and PCM peak | Native host execution; separate target integration must carry its own emulator/hardware receipt |

| `grimoire-native` | CY1 endpoint around actual Orbit Gate core | Pitch/material XY, three presets, two envelope/energy peaks, PCM and labelled local orbit model | Native host execution only; no FM-1 CY1 endpoint or physical proof |

Tier 1 is the verified MIDI subset of each legacy profile. Tier 2 is actual
negotiated identity, parameter/preset mutations and state reconciliation in the
new endpoints. Tier 3 is partially demonstrated by actual engine telemetry,
gated XY and common local sequencing. No remote pattern editor, remote preset
write/storage, generic patch transfer or automatic third-party firmware loading
is claimed.

## Profile contract

The registry is deliberately bounded: eight parameter slots, eight preset names
maximum, six telemetry channels and 16 local steps. Parameters have stable IDs,
labels, type, range/default, unit, optional enum choices, write permission and a
documented native-adapter CC mapping. The wire carries normalized integers
0..127. `companionParameterValue` and `companionParameterNormalized` provide the
semantic conversion; discrete values are quantized and displayed as names.

`validateCompanionProfile` checks versions, bounds, strings, duplicate parameter
IDs/CCs, enum shape, note range and read/write consistency. A profile's declared
capabilities are an upper bound. Controls require the intersection with a
matching endpoint's negotiated capabilities. Wrong identity, incompatible
version, malformed data or no reply leaves the controls disabled.

These are compiled profiles, not downloaded executable UI definitions. Multiple
implemented instruments work without reflashing; adding a completely new
identity currently requires a registry/firmware update. Dynamic bounded metadata
loading is a follow-up, not an implemented discovery claim. Identity discovery
confirms the selected family; it does not scan and arbitrarily switch connected
firmware. The `-native` IDs identify the first demonstrated profile revisions;
endpoint source provenance is carried separately.

## CY1 wire contract

CY1 is an experimental, small SysEx extension implemented by the supplied
endpoints. It is not a published universal MIDI standard and is not sent to
legacy profiles. Every byte between F0/F7 is seven-bit data.

| Offset | Field |
| --- | --- |
| 0..3 | `F0 7D 43 59` (non-commercial ID, `CY`) |
| 4..6 | Protocol major, minor, operation |
| 7..10 | Nonzero 28-bit session, low seven bits first |
| 11..12 | 14-bit sequence, low seven bits first |
| 13 | Payload length, bounded to 48 |
| 14 onward | Operation-specific payload |
| Final | `F7` |

Operations are Discover=1, Identity=2, GetState=3, State=4, SetParameter=5,
SetPreset=6, Note=7, Panic=8 and Error=9. Framing overhead is 15 bytes. Identity
payload is six bytes: instrument, profile major/minor, two capability bytes and
source class. State payload is 23 bytes: two-byte valid mask, eight normalized
values, preset, note, gate/playing flags, meter count, six meters, separate PCM
output peak and two-byte state revision. Exact serialization is tested against
the native endpoint; independent target implementations must pass the same
byte-level interoperability tests.

| State payload offsets | Field |
| --- | --- |
| 0..1 | Valid parameter mask, low seven bits first |
| 2..9 | Eight normalized parameter values |
| 10, 11 | Preset index and note |
| 12, 13 | Gate/playing flags and meter count |
| 14..19 | Six interval meter maxima |
| 20 | Actual PCM output peak |
| 21..22 | State revision, low seven bits first |

Instrument IDs 1, 2 and 3 identify ZaaggenZ, Xenoresonator and Grimoire respectively. Source classes are
NativeEngine=1, EmulatedFirmware=2 and PhysicalDevice=3. A source declaration
does not prove the environment; qualification evidence establishes that.
Capability bits are notes, parameters, presets, XY, telemetry, transport and
pattern. The current native engines advertise the first five, not transport or
remote pattern state. Their sequencer belongs to CYDaux.

The controller issues one outstanding read at a time. Read timeout is 1,000 ms
from actual transport acceptance, state freshness is 1,500 ms, and polling is
limited to 10 Hz. Mutations are never automatically retried. Replies must match
the current session and the most recently issued sequence; old replies cannot
snap a newly edited value back. Firmware seeds sessions once from `esp_random`;
each connection uses a new session. Physical adapters retain their existing
transport generation/readiness epochs as a separate guard.

A current Session/Version error retires controls, telemetry, queued mutations,
the local sequence and captured gestures. Explicit **Refresh** then requests a
full transport reconnect. The old connection's independent note-release guard
runs before it is reset, and only the new connection starts a fresh CY1 session.
This maintains one wire session per transport epoch; a replayed or competing
Discover cannot steal the emergency-release responsibility. No note or parameter
mutation is replayed during recovery.

An authenticated enhanced-profile TCP connection is also retired on USB
readiness loss, including a send failure within a batch of TCP frames. The
bridge discards remaining frames and performs best-effort release on the
retired port only; a replacement USB endpoint requires a new TCP
authentication and CY1 discovery. Initial waiting for USB is still supported.
The CYD transport also closes an enhanced connection when an older bridge
reports ready-to-unavailable status. Legacy profile waiting/status behaviour
is unchanged. These are software lifecycle guards, not a guarantee of note
release after physical cable removal; the instrument-side receive lease
remains necessary.

The 40-message controller queue coalesces only the trailing uninterrupted run of
parameter mutations. It preserves the admitted packet's sequence, so discarded
unsent touch samples do not exhaust the sequence window. Notes, reads and preset
changes are ordering barriers. Emergency release/panic can clear queued musical
work; it must never preserve a stale attack for the next connection.

## Audio, control and telemetry

Native ZaaggenZ runs at 44,100 Hz; Xenoresonator at 24,000 Hz. The browser receives
bounded signed 16-bit PCM blocks through a loopback HTTP service. Web Audio
performs output-rate conversion. The native sample clock owns sequencing while
audio runs; browser polling must not advance it a second time. Scheduling is
bounded to approximately 153 ms at 24 kHz, with 12 nodes as a hard ceiling. These
are desktop transport settings, not BLE/USB hardware latency measurements.

If PCM consumption ceases for 750 ms, the server stops the audio clock and
panics the instrument. Browser blur, visibility loss, audio stop, profile changes
and panic retire scheduled audio. Session and audio-generation guards reject
late replies/blocks. The audio player tests include cancellation and malformed
PCM; real-browser qualification separately exercises actual Web Audio buffers.

Telemetry is actual interval maxima since the last reported state. ZaaggenZ's
first five channels are rendered stem peaks; Xenoresonator's six channels are
modal-state magnitudes, not a calibrated spectrum. PCM peak is a distinct field.
The graphical channel display may use a visibly labeled square-root scale;
raw numeric values and the output peak remain unchanged. No random animation
is presented as instrument state.

## Adding an instrument

Start from an actual engine and its independent tests. Identify controls and
supported routes in source before writing a profile. Keep optional companion
control separate from standalone panel performance. Add bounded metadata and a
legacy-profile entry with unavailable capabilities disabled. Use a different
major version when semantics are incompatible.

Adapt `VirtualInstrument` around the real engine: initialize, set normalized
parameter/preset, play/release, panic, render bounded PCM and report meaningful
state. Use the existing codec/session and renderer; do not create another
browser synth or per-instrument CYD firmware. Add tests showing touchscreen
changes alter actual output, invalid commands leave state unchanged, saturation
cannot strand notes and reconnect does not replay attacks. Keep source pins,
hashes, license notices and any excluded assets explicit.

For a real FM-1 endpoint, coordinate with VIBEWARE PLATFORM. Service bounded
USB input outside audio; transfer commands with a bounded snapshot/queue and
apply them at a block boundary. Do not call a blocking USB transmitter in an
audio ISR. Qualify actual target code in the emulator when supported and keep
physical acceptance separate. The legacy bridge passes SysEx but cannot create
an endpoint that the instrument firmware lacks.

## Reusable family patterns

| Family | Useful common pieces now | Integration still required |
| --- | --- | --- |
| Xenoresonator | Actual material preset grid, excitation/damping XY, modal peaks, local note pattern | Physical USB/radio acceptance and richer material authoring |
| Chronomorph | Editable 16-step local pattern, transport/tempo layout and gated audition | Actual clock/polymeter state, remote pattern semantics and the current engine adapter |
| Ecophon | Parameter pages, six source-labeled state channels, intervention XY | Agent/resource telemetry and actual ecological control semantics |
| Grimoire | Fullscreen gated XY, palettes, presets and compact state view | Actual rites/gestures/telemetry mapping and downstream endpoint |
| FM-1-MHAC | Versioned state, typed discrete controls and modular pages | Grammar/dialogue semantic adapter and a verified firmware route |

These rows are reusable interface patterns, not completed additional instruments.
