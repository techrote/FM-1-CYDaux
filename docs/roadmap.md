# Development roadmap and scope guardrails

This is a proposed sequence for review, not authorization to start new features or a promise of delivery dates. The first milestone is software-qualified; the primary missing evidence is hardware.

| Stage | Goal | Gate |
| --- | --- | --- |
| CYDAUX-001 (delivered candidate) | Shared UI, four pinned profiles, direct Melodee BLE, PC Wi-Fi/USB bridge, simulator and qualification pipeline | Historical [delivery record](delivery-001.md); physical checks pending |
| CYDAUX-002 (proposed) | Board and connection acceptance, fix actual discovered defects; owner-ready binaries | Real CYD screen/touch/calibration, LAN mock, actual Melodee and SLOOP sessions, exact evidence, no stale commands |
| CYDAUX-003 (proposed) | Better performance surfaces within source-qualified capabilities | Owner preference and accepted physical baseline; no fake state |
| CYDAUX-004 (proposed) | Richer negotiated SLOOP LIVE mixer/sections and Skiff contextual remote controls | Source-pinned operations, safe gating, mock/negative tests and real device readback |
| CYDAUX-005 (exploratory) | Small dedicated standalone USB-host wireless bridge, avoiding a PC | Separate suitable USB-host hardware and real interoperability qualification; classic CYD USB is not host |
| CYDAUX-006 (exploratory) | Further firmware profiles, X0X drums/mix or richer editors | Per-firmware source audit, capability and schema design, resource and hardware checks |

## Priority after hardware arrives

Qualify the original CYD in Demo and with the LAN mock first. Establish stable touch, setup and reconnect before adding screens. Next use Melodee's direct BLE route and then SLOOP's bidirectional USB path. Use real MIDI evidence to correct the mocks and protocol assumptions. Capture runtime memory and timing while controlling a playing instrument. Record failures and the exact accepted revision.

Potential user-visible refinements include better parameter labels and value units, accessible touch targets, profile-specific controls, saved nonsecret control preferences, and more informative link diagnostics. Consider them based on owner testing rather than adding UI that cannot be supported by the firmware.

## Deliberately separate research

Direct Wi-Fi on the FM-1, an embedded USB-host adapter, screen mirroring/export protocols, MIDI clock generation, bulk SysEx/project transfer, patch and pattern editing, and audio streaming are separate technical projects. Upstream projects evolve; do not claim their current feature set is already supported by CYDaux. A new transport must preserve musical correctness, explicit endpoint selection, bounded messages, and disconnect behavior. A new firmware/profile must have documented versions, mappings and feedback boundaries.

Do not add an FM-1 firmware updater under the guise of a control profile. Do not widen flash targets, enable simultaneous radio connections, or relax identity checks without an explicit design and qualification review. Preserve the initial tested artifacts and prior evidence before moving the implementation forward.
