# CYDaux engineering contract

Build the common ESP32-2432S028R companion and its playable evaluator. Read
README.md, docs/companion-platform.md, docs/interaction-design.md,
docs/virtual-evaluator.md and the current mission PR before editing.

## Recover before changing

Inspect live main, open PRs, exact heads and current qualification. PR #1 is the
preserved legacy foundation; VIBEWARE-COMPANION-PLATFORM-001 extends that exact
implementation. Do not assume an older artifact or a scheduled-task description
identifies current accepted source. Claim a narrow issue and inspect overlapping
work before writing. Never force-push another worker's branch.

## Ownership and integration

- CYDAUX owns common controller, renderer, widgets, profile registry, transport,
  simulator and companion packages here.
- VIBEWARE PLATFORM owns FM-1 runtime/USB/target adapters. Propose coordinated
  interface changes in a separate Draft/HOLD PR; do not overwrite its active PR.
- Creative workers own actual sound engines and musical semantics. HYBRID owns
  audiovisual instrument design, not a competing CYD firmware.
- QA independently rebuilds exact commits, extracts packages and reproduces
  important claims. Authored tests alone are not independent qualification.

An explicit owner mission may authorize coordinated changes in both repositories.
Keep that authorization and the affected issue/branch visible. This file does not
activate, pause or change any schedule. Do not alter schedules during this mission.

## Behavioral requirements

Preserve legacy Melodee/SLOOP/Skiff/X0X maps and safeguards. Native engine,
emulated firmware, mock values and physical measurements are different evidence
classes. A capability bit from an endpoint is a declaration, not hardware proof.
Do not expose extended controls before a matching identity/version handshake.
Unmodified Fune does not implement CY1 or the native evaluator's CC mapping.

Keep fixed buffers, bounded queues, no mutation replay, session/sequence guards,
touch capture, independent panic/release and disconnect cleanup. Parameter
coalescing must preserve note/preset ordering and must not consume sequence
numbers for discarded unsent touch samples. Do not run DSP in the CYD UI loop.
Keep FM-1 audio callbacks free of blocking USB, allocation and rendering.

Keep engine code pinned and original notices intact. The ZaaggenZ source-image
bank is excluded; use its actual procedural engine. Xenoresonator has an explicit
owner-provided license status, not an invented MIT grant. Do not distribute FM-1
installable images without the downstream board/format/rights/recovery gates.

## Reproduction

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
FM1_CYD_SIMULATOR=build/fm1-simulator python -m unittest discover -s tests/bridge -v
FM1_CYD_SIMULATOR=build/fm1-simulator python -m unittest discover -s simulator -p 'test_*.py' -v
node simulator/test_frontend.cjs build/fm1-simulator
node simulator/test_audio_player.cjs
node simulator/test_audio_lifecycle.cjs
pio run -e cyd -j 2
```

Use `build/fm1-simulator.exe` on Windows; set the environment variable with the
shell's native syntax. `.github/workflows/build.yml` is the executable Windows,
Linux, browser and ESP32 gate. Run the resource probe and extracted-package
qualifier for delivery changes. Add a regression test for a concrete defect,
then repeat the affected gate. Do not manufacture passing receipts from skipped
tests or substitute screenshots for actual touch/controller/engine execution.

## Delivery

Produce a coherent Draft/HOLD PR, exact source/tree identity, dependency pins,
checksummed artifacts and concise owner steps. Keep pending hardware checks
explicit. No merge, public release or physical flashing without owner authority.
