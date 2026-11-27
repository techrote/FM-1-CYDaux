# Playing the common CYD interface

The firmware and evaluator use the same `fm1::Ui` renderer, hit testing and
`fm1::Controller`. The display is 320 × 240 pixels. The browser presents the
actual C++ framebuffer and forwards pointer events; it does not implement a
parallel instrument UI in JavaScript.

Legacy Melodee, SLOOP, Skiff and X0X profiles retain their four tabs: **Play,
Mix, XY, Link**. A companion profile adds **Sequence** and **Bank** and uses the
former Mix position for **State**. The underlying legacy page numbers stay
unchanged, so existing scripts and hardware settings keep their meaning.

## Controls that are useful with one finger

The original resistive CYD accepts one contact at a time. Companion XY pages
therefore start with **Gate on**: touching the pad starts a note, moving the
same finger changes both assigned controls, and lifting releases the note.
This is the normal way to play a changing timbre without holding a second
button. **Gate off** turns the pad into a control surface for an external
keyboard or another note source.

Xenoresonator is percussive: each new touch strikes its resonant bodies. Tap
again or start the local sequence for repeated excitation. A **Held** label
describes the input gate; it does not promise sustained acoustic output after
a resonator has decayed.

The initial XY values are admitted before the note attack. This matters for
percussive instruments: Xenoresonator chooses its excitation-burst length at
the strike, so the new touch position must be in effect at that instant. An
integration test compares the real generated PCM with that intended ordering.

The smaller **Note** buttons on Play and State are momentary audition
controls. They release when the finger leaves the screen and lifts, when a
pointer is cancelled, or when a profile, page, track, bank, connection or
fullscreen context changes. Dragging a captured control never becomes a
navigation action. The final release coordinate is processed, including when
the host coalesced intermediate motion samples.

The controller also gives each interaction context an epoch. A disconnect and
reconnect can both finish between two touch polls, leaving the visible profile
and connection state unchanged. The epoch still retires the captured gesture,
so an old Move/Up cannot alter the newly connected instrument. Preset reloads
and panic retire captures as well. An actual-engine integration test repeats
1,000 held-touch and sequence transitions across profiles, modes, same-profile
reconnections and same-preset reloads.

The common interface deliberately does not emulate multitouch. Playing a
manual note stops the local monophonic sequence before the new note is
admitted. This preserves one clear owner for note release on these engines.
Panic remains reachable on the normal and fullscreen performance pages.

## Pages

| Page | Implemented interaction | State interpretation |
| --- | --- | --- |
| Play | Four parameter cards; drag horizontally across a card. The upper right bank selector exposes the next four parameters. Knob rings and typed values share the same normalized control value. **Loop** starts/stops the local sequence; **Note** auditions the selected pitch. | Enum names and units come from the selected versioned profile. Disabled parameters remain disabled. A profile does not prove that its endpoint is connected. |
| State | Up to six endpoint channels, the actual PCM peak, momentary audition and panic. | Native engine channels are actual role/stem or resonator-state measurements. The native endpoint accumulates interval maxima until a state reply is queued, allowing the 100 ms polling interval to show short percussion. These are not a spectrum analyser or measurements of physical hardware. |
| XY | Two assigned parameter axes, optional touch gating, three colour themes, panic, and **Expand**. | The cursor maps control positions. The underlying bars and connecting line use fresh endpoint telemetry when available. Without telemetry, the explicitly labelled **Local control map** is derived only from the control coordinates. |
| Fullscreen XY | The pad fills the display below a 36-pixel toolbar. **Back**, gate mode and panic stay visible. | Same controls and telemetry as the normal XY view; no independent or decorative animation engine. |
| Sequence | Sixteen local note/gate cells, run/stop, tempo, length, selected-note adjustment, velocity and gate sliders. | A local MIDI performance aid. It is not readback or editing of a device's internal sequencer. |
| Bank | Up to eight named presets, each recalled through the common controller. Long names wrap inside the card. | Recall requires the negotiated preset capability. Disconnected names remain visible with **Connect to recall**; demo mode is explicitly a name preview, not generated audio. |
| Link | Profile-supported transports, connect/disconnect, setup, **Refresh** and audition pitch. | **Native** is available only when the host explicitly installs a real audio engine. **Refresh** is available while connected; a retired endpoint lease first reconnects the transport so independent note-release tracking also starts fresh. The ESP32 firmware cannot select an imaginary local audio engine. |

State-channel bar lengths and the XY plot use an explicitly labelled square
root scale to make quiet modes visible. The raw 0–127 channel numbers remain
unchanged, and the actual PCM peak meter remains linear. This is a causal
visualisation of native ZaaggenZ stem peaks or Xenoresonator mode-state
magnitudes, not a calibrated spectrum or a frequency-response plot. The
endpoint retains genuine maxima since the preceding queued state reply; the
UI does not invent a visual decay or animate absent sound.

The six companion tabs are 53–54 pixels wide and 34 pixels high. Parameter
cards are 148 × 44 pixels. The sequence cells are 36 × 27 pixels; these compact
cells and the narrow increment buttons should receive particular attention in
the first physical resistive-touch session. No physical touch accuracy or
comfort claim is made by the host tests.

## Local sequence semantics

`PerformanceSequencer` owns a fixed array of 16 steps. Each step has a MIDI
note, velocity, enabled state and gate duration from 1–100 percent of a
sixteenth note. A zero velocity produces a rest. Length is 1–16 and tempo is
30–300 BPM. The controller bounds note editing to the selected instrument's
supported pitch range.

- Tap a cell to select it and toggle its gate. Drag vertically from a cell to
  change that captured step's pitch; four vertical pixels correspond to one
  semitone. A pitch drag does not toggle the step or navigate to another tab.
- The selected note's **−/+** buttons change a semitone. The velocity and gate
  panels are horizontal sliders. Shorter sequence lengths retain the inactive
  steps for later reuse.
- The clock retains fractional milliseconds, so non-integral periods such as
  137 BPM do not accumulate the tempo error of an integer `15000 / BPM`
  interval. Scheduling uses wrap-safe unsigned time differences.
- A delayed tick skips elapsed steps. It emits at most one new attack and
  releases a previous note first. It does not replay a burst of missed notes.
  If a step's gate has already ended, that late attack is omitted.
- A failed release remains pending, including after Stop. A new attack cannot
  overtake that release. The controller and transport additionally own session
  retirement and panic; `abandonAfterPanic()` is reserved for a session they
  have independently retired.

The native evaluator advances this same controller from rendered sample time
in quanta of at most 128 samples. The firmware drives it from its normal loop
clock. Host timings do not establish ESP32 scheduling jitter or radio latency.
Patterns and themes are currently session state. This change does not claim a
saved pattern bank, clock synchronization to external MIDI, polyphony,
microtiming, per-step parameter locks or an instrument's structured pattern
protocol.

## Colour and rendering

Three compact colourways give the two new instruments distinct identities:
the existing green/blue palette, an amber/rose palette and a violet/cyan
palette. **Theme** cycles them on the XY page. A stack-local Canvas wrapper
translates semantic colours, preserving the existing renderer without a
second framebuffer or global mutable palette.

The native display and the CYD use the same fixed font, clipped primitives and
geometry. The ESP32 path retains a 40-row RGB565 strip: 320 × 40 × 2 = **25,600
bytes**. Six strips form a frame. Fonts, knobs, telemetry and preset cards do
not allocate memory while drawing. The renderer only changes when controller
state changes; there is no continuously running visual animation that must
compete with control handling.

`strip_renderer_test` compares every pixel of the six-strip result with a full
framebuffer for 38 scenes: all companion pages, all three colourways,
fullscreen XY corners/centre, setup, and the four legacy pages. This proves
that strip clipping preserves the shared drawing result. It does not measure
the physical panel bus or prove a physical refresh rate.

## Reproducing the evidence

Build with the repository's normal CMake instructions. The targeted checks
are `ui_test`, `performance_ui_test`, `performance_test` and
`strip_renderer_test`, with actual-engine checks in `companion_gesture_test`;
they are included in CTest. The interaction suite
checks captured-step editing, disabled capabilities, full-range XY, final
release samples, cancellation, context changes, native-transport gating,
bounded presentation arrays and theme separation. The sequence suite includes
a 60-second 137-BPM timing run, clock wrap, transport refusal, long stalls,
boundary values and 400,000 elapsed-step endurance.

Generate actual native screenshots with:

```sh
python scripts/capture_companion.py --binary build/fm1-simulator --output docs/images
```

The script negotiates a real native endpoint, renders actual instrument audio
to advance its state, and captures the shared C++ framebuffer. It records
each PNG's SHA-256, the command sequence and the native source revision in
`companion-captures.json`. It refuses an unnegotiated endpoint. These are
software screenshots, not photographs of a CYD or evidence of an FM-1 CPU
emulator.

Run the integrated resource probe with:

```sh
build/fm1-resource-probe > resource-host.json
```

The probe uses the real Controller, Ui, companion protocol and both bundled
native cores. Its default workload renders 1,048,576 samples per instrument,
alternating local sequencing with rapid gated XY manipulation and repeated
strikes, while producing six-strip frames at approximately 30 Hz of simulated
sample time. It reports native audio, control/protocol, drawing and complete
iteration time distributions, actual PCM energy, event counts, coalescing,
endpoint rejection/overflow counts and final note-release state. One thousand
undrained XY moves plus the initial gated touch must coalesce to at most five
outgoing events: two initial parameter values, the note attack, and two latest
values after that attack. Coalescing must preserve the note's ordering barrier.
The final
panic must leave neither a held note nor a running sequence.

All resource-probe timings are **host CPU execution**, with real native sound
engines but no physical display, radio, FM-1 processor or OS audio device. Its
object sizes are explicitly for the host ABI; pointers and alignment differ
on the ESP32. Use the target link map and firmware serial heap/stack/render
diagnostics for the first physical session. Keep the full distributions,
including scheduling outliers, rather than treating a host median as a
hardware deadline guarantee.
