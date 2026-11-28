# Ecophon host semantic seam (Draft/HOLD)

Base: CYDaux #13 at 4d52c9870b0926868cc4322cf0ef0a67e84c0d4f, hosted native Linux/Windows and ESP32 PASS (46095922163).
Upstream: VIBEWARE #8 at feda1327f93f10b4070e8596cab09141c9203c4d, hosted native CI 46098655740 PASS.
The engine and profile are pinned; no CYBER branch changes or license grant.
The original adapter-only milestone was compiled/tested on host only; the habitat panel candidate below makes it selectable in the native Studio.

- Four preset choices, including the upstream frozen comparator (regime zero with ablation).
- Fertility/sharing/pressure retain full 0..255 precision. Invalid controls are rejected
  atomically; XY retains sharing and freeze. No use of the seven-bit CY1 fields.
- Four resource patches, eight bounded agent slots, counters and envelope snapshots are
  actual native core state. Slots are not individual identities. Frozen ecology still
  replenishes resources; it is not a stopped clock.
- The local audio owner serializes calls. Reset begins stopped, start/resume advances the
  core only on bounded PCM requests; stop/disconnect silences output and pauses model time.
  A UI integration must additionally invalidate queued gestures and audio generations.
- Test: `ctest --test-dir build -C Release -R eco_adapter_test --output-on-failure`.
  Covers partition equivalence, population accounting, actual PCM, frozen contrast,
  intervention, bounds, atomic rejection, silence, and exact pause/resume.

Next: integrate the same simulator process/framebuffer and touch capture with 4-patch/8-slot
visualization, three environmental controls, regime reset, freeze, start and panic.
Use the common PCM lease and label actual local native state; exercise authored Habitat
Arc cues with the CYBER fixture before claiming them. No MIDI or enhanced transport,
device telemetry, ESP32 profile, physical claim, or new wire format.

## Habitat panel candidate
The common native process now links Ecophon and exposes the local habitat editor via the same Canvas, touch capture, audio lease and package inventory. Actual local agent slots, resource levels and energy/envelope bars are bounded; full-width XY and sharing, freeze, preset reset and Start/Stop are exposed. Switching between native editors cancels captured input and retires the shared native-editor audio generation. Hosted tests and independent QA pending. The browser witness exercises real Web Audio, full-width XY, sharing, native ecology, bounded queue and panic. Habitat Arc automation is subsequent work; no claim of implementation here.
