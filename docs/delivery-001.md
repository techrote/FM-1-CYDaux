# CYDAUX-001 — initial implementation delivery record

**Recorded:** 2026-11-25 UTC. **Disposition:** software-qualified implementation candidate, physical acceptance pending; [PR #1](https://github.com/techrote/FM-1-CYDaux/pull/1) was Draft and unmerged at the time of this record. This is an immutable historical evidence snapshot, not a claim about future HEAD.

## Source and CI

* Qualified implementation commit: [476a13f2a3d92e8ae7c3c35d5cd7657de1f189a2](https://github.com/techrote/FM-1-CYDaux/commit/476a13f2a3d92e8ae7c3c35d5cd7657de1f189a2).
* Git tree: 562a760da7c604e724a03aba25bb17d4ac46fb4a.
* Source branch at initial delivery: feat/cyd-companion-001.
* [Exact-head push run 45531062398](https://github.com/techrote/FM-1-CYDaux/actions/runs/45531062398): completed success.
* [PR run 45531135481](https://github.com/techrote/FM-1-CYDaux/actions/runs/45531135481): completed success.

The exact-head push run is the primary binary provenance authority. The PR run also reported successful Windows, Linux and ESP32 jobs. CI status and artifact metadata were re-read on 25 November 2026 for this documentation record.

## Downloadable artifacts

The following artifacts are from the exact-head push run, not a floating latest link. They were unexpired at documentation time and report expiration at 2027-02-23 02:10:40 UTC. GitHub may require sign-in for artifact download. Preserve a local copy if needed beyond retention; no permanent release asset is claimed.

| Package | Artifact ID | ZIP size (bytes) | Download |
| --- | ---: | ---: | --- |
| Windows simulator | 14984138648 | 237492 | [ZIP](https://github.com/techrote/FM-1-CYDaux/actions/runs/45531062398/artifacts/14984138648) |
| Linux simulator | 14984342912 | 110985 | [ZIP](https://github.com/techrote/FM-1-CYDaux/actions/runs/45531062398/artifacts/14984342912) |
| ESP32-2432S028R firmware | 14985610777 | 9474934 | [ZIP](https://github.com/techrote/FM-1-CYDaux/actions/runs/45531062398/artifacts/14985610777) |

GitHub-reported SHA-256 digests of the artifact ZIPs (not the inner binaries):

| Package | SHA-256 |
| --- | --- |
| Windows | e8a49632e6a5cf759bafa35f4df3ee2c9f7c5a938d9a62bcedcbef08a40ac0ac |
| Linux | 98ec70463687230571163ac03f37f6b74f73a9272e3880f5a5dd40553797a508 |
| ESP32 | 5fc0ae96d703ca9e9ab99da72d8960fb4239010c71b468d29be6f636857e5e70 |

The initial delivery reported downloaded ZIP integrity and source/packaged-hash checks. This documentation pass verified the published metadata and run outcomes, not a fresh binary download. Each package has manifest.json; verify its source_commit, source_dirty and file hashes. For firmware run the included flash.py --check before opening a port. The simulator uses Python 3.10+ and a native executable; it has no audio engine. The firmware package includes the original CYD images, exact offsets, license notices and diagnostic ELF/map as available. It contains no FM-1 firmware.

## Software evidence

| Check | Result | Evidence |
| --- | --- | --- |
| Native behavior | 8/8 Windows and Linux | [Windows job](https://github.com/techrote/FM-1-CYDaux/actions/runs/45531062398/job/138926185373), [Linux job](https://github.com/techrote/FM-1-CYDaux/actions/runs/45531062398/job/138926185393) |
| Bridge | 38 cases per OS, including two shared-core actual TCP cases | Same native jobs; [test inventory](testing.md) |
| Simulator | 8 cases per OS | Same jobs; native framebuffer/HTTP/gesture path |
| Frontend event integration | Pass per OS | Real HTTP/native engine with DOM shim, not real browser visual qualification |
| Flash-package tests | 6 per OS | Same native jobs |
| ESP32 compile and package check | Pass | [ESP32 job](https://github.com/techrote/FM-1-CYDaux/actions/runs/45531062398/job/138926185008) |

Hosted exact-head compile reports 1,154,489 bytes linked flash out of 3,145,728 (36.7%), 92,820 bytes static RAM out of 327,680 (28.3%), and the initial delivery reported a 1,161,072-byte application image. These are compile/link measurements, not runtime available memory. The earlier local build differed slightly in linked flash size; use the hosted result for this artifact. Runtime heap and stack need physical measurements.

Known non-fatal build warnings in the historical logs include TFT_eSPI TOUCH_CS undefined (separate touch driver by design), MSVC integer-narrowing warnings and test NDEBUG override, and runner action Node deprecation warnings. Green CI is not warning-free and is not a physical test.

## Delivered scope and exclusions

Four pinned profiles: Melodee 1.6.1, SLOOP 2.8.5, Skiff 2.7.1, and X0X's two 303 roles. Direct BLE performance MIDI for Melodee; Wi-Fi to computer USB MIDI for all four; source-shaped mock and local demo; shared native UI/controller; setup/calibration; bounded protocol and release handling; SLOOP INFO/LIVE subset and explicit Play/Stop. Exact mapping and protocol details are in [firmwares.md](firmwares.md).

No real device, CYD touch/display, BLE pairing/subscription, real USB MIDI backend, audio response, physical timing, disconnect or on-board memory margin was qualified during initial development. No direct FM-1 Wi-Fi API, full editor, audio streaming, external clock generator, updater, sample/project transfer or other X0X roles are implemented. The SLOOP fixture is synthetic.

Use [hardware acceptance](hardware-validation.md) and a [session record](hardware-session-template.md) before asserting a real route works. Subsequent docs-only changes are not a new binary qualification; their own CI runs can establish the next exact-head software record.
