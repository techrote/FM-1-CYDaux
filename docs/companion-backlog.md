# Common companion follow-up work

These are executable follow-ups to the common-platform candidate, not claims
that the work is already complete. The current implementation is in
[CYDaux #2](https://github.com/techrote/FM-1-CYDaux/pull/2); the independent
FM-1 USB endpoint is in
[VIBEWARE #15](https://github.com/techrote/FM-1-VIBEWARE/pull/15). Inspect live
heads before claiming work. Keep the preserved legacy PR #1 and other workers'
branches intact.

| Priority | Issue and concrete result | Owners / dependency |
| --- | --- | --- |
| P0 | [#4 — Physical acceptance](https://github.com/techrote/FM-1-CYDaux/issues/4): identified CYD/FM-1 pairing, calibration, real audio/radio/USB behavior, interruption/recovery, heap/stack/timing receipt | Owner-controlled hardware session; CYDAUX + PLATFORM + independent QA |
| P1 | [#5 — PLATFORM endpoint and complete bridge route](https://github.com/techrote/FM-1-CYDaux/issues/5): shared controller → authenticated bridge → actual compiled target/emulator, with bounded telemetry and recovery dispatch preserved | Coordinate VIBEWARE PLATFORM #13 and endpoint #15; do not duplicate the runtime |
| P1 | [#6 — Third real creative consumer](https://github.com/techrote/FM-1-CYDaux/issues/6): Grimoire or Chronomorph actual audio, semantics and an instrument-specific interaction through the same runtime | Creative worker owns sound/semantics; CYDAUX owns shared UI/profile; PLATFORM owns endpoint |
| P2 | [#7 — Demonstrated metadata/bank extension](https://github.com/techrote/FM-1-CYDaux/issues/7): bounded descriptors or banks justified by two consumers, explicit compatibility, malicious-input tests and resource measurements | Preserve v1/static-profile behavior; avoid a speculative universal protocol |

## Worker start procedure

Read the current programme and
[CYDAUX lane issue](https://github.com/techrote/FM-1-VIBEWARE/issues/9), this
repository's `AGENTS.md`, the active PRs and their exact CI results. Claim a
narrow issue. Fetch the chosen source heads and inspect overlap before writing.
Run the current native, bridge, audio and target gates before changing them.
No task description or historical receipt substitutes for the live source.

For a new instrument, begin with `CompanionProfile`, the actual native adapter
and an end-to-end test using `Controller`, `Ui` and real PCM. The two existing
adapters are small working examples. Port the endpoint only after its musical
semantics and bounded behavior work. Keep new capabilities disabled until a
matching endpoint demonstrates them. Add specialized pages only when the
existing parameter, XY, preset and sequence modules cannot express the task.

Every PR should state the user-visible change, exact engine/dependency pins,
licensing boundary, tests actually executed, evidence class, package hashes and
next physical acceptance step. QA should independently reproduce the important
claim. Preserve failure receipts when a gate discovers a defect.

## Ownership boundaries

CYDAUX owns one shared companion runtime, widgets, profiles, transport adapters,
browser/native evaluator and CYD packaging. VIBEWARE PLATFORM owns the FM-1
runtime, hardware/USB services and target deployment gates. Creative workers own
their engines and musical meaning. HYBRID owns audiovisual instrument design
and can request shared modules without creating a competing companion firmware.
QA owns independent reproduction and qualification clarity.

This repository prepares the recurring worker to contribute in both authorized
repositories. It does not activate, pause, reschedule or replace any task.
Draft/HOLD, recovery and explicit physical acceptance boundaries remain in force.
