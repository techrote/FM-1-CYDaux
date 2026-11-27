# Dependencies and interoperability sources

CYDaux's original application, UI, bridge, tests and documentation are under
[MIT](LICENSE). The CYD firmware does not embed an FM-1 audio engine or distribute
Melodee, SLOOP, Skiff or X0X firmware binaries. The host evaluator includes the
source-pinned native engines listed below; their licenses remain separate.
Protocol mappings and fixtures are
independent interoperability implementations, audited against the immutable
sources in [docs/upstream-sources.json](docs/upstream-sources.json).

## CYD binary dependencies

| Component | Pin | Source / notices |
| --- | --- | --- |
| PlatformIO Espressif32 | 6.9.0 | [platform-espressif32](https://github.com/platformio/platform-espressif32/tree/v6.9.0) |
| Arduino-ESP32 | PlatformIO package 3.20017.241212, core 2.0.17 | [source](https://github.com/espressif/arduino-esp32/tree/2.0.17); LGPL-2.1-or-later for the core, component-specific notices |
| ESP-IDF SDK | As packaged by the pinned Arduino core | [source](https://github.com/espressif/esp-idf/tree/v4.4.7); Apache-2.0 and component-specific notices |
| TFT_eSPI | 2.5.43 | [source](https://github.com/Bodmer/TFT_eSPI/tree/V2.5.43); upstream license/Adafruit notices retained in `licenses/` |
| XPT2046_Touchscreen | d57f64c8b5f2bc5b8d10d121550806eeff7b06d9 (v1.4) | [source](https://github.com/PaulStoffregen/XPT2046_Touchscreen/tree/d57f64c8b5f2bc5b8d10d121550806eeff7b06d9); MIT |
| NimBLE-Arduino | 1.4.3 | [source](https://github.com/h2zero/NimBLE-Arduino/tree/1.4.3); Apache-2.0, NimBLE and TinyCrypt notices in `licenses/` |
| ArduinoJson | 6.21.5 | [source](https://github.com/bblanchon/ArduinoJson/tree/v6.21.5); MIT |

The build downloads these dependencies; they are not copied into the source
tree. Distribution packages retain the principal dependency notices in
`licenses/`. Build configuration and all original application source are
available in this repository so modified libraries and the application can
be rebuilt and relinked. No signing key or device authorization is required to
install a modified build. Dependencies retain their own licenses.

The optional host MIDI backend uses [Mido](https://github.com/mido/mido) and
[python-rtmidi](https://github.com/SpotlightKid/python-rtmidi), pinned in
`bridge/requirements.txt`; these are installed on demand, not bundled with the
native simulator. The Python simulator frontend and mock bridge use the Python
standard library. The 5×7 screen font and UI renderer are original code.

## Native evaluator instrument cores

| Component | Exact source | Scope and rights |
| --- | --- | --- |
| ZaaggenZ procedural engine | `techrote/ZaaggenZ-FM1` at `ac21d089e7afce6bc7adc555deb0ce90b56a39cd` | Original engine code is MIT. Vendored files are byte-identical and retain upstream notices. The local adapter uses procedural `ZG_NATIVE` mode only. Owner-supplied source-image banks and hybrid/source modes are excluded. |
| Xenoresonator | `techrote/FM-1-VIBEWARE` at `fa27a317bb116f2afb62a710876f96314756f814`, `instruments/xenoresonator` | Original owner-provided C11 instrument, integrated at the owner's request. `LicenseRef-Owner-Provided-Unspecified`; no general permissive redistribution grant is asserted. It is not relicensed by CYDaux's MIT license. |

| Grimoire Orbit Gate | `techrote/FM-1-VIBEWARE` at `ca1b83a5f6c8f1e87dea671f93f7e2a6d3bc432f`, `instruments/grimoire` | Original owner-provided portable core; owner-authorized integration only. LicenseRef-Owner-Provided-Unspecified; no general redistribution grant asserted. |

`third_party/*/UPSTREAM.json` records source locations, revisions, exact file
hashes and local modifications. These source files and notices travel with the
owner evaluator so the engine behavior and licensing boundary remain inspectable.
No native engine is linked into the ESP32 image. None is an emulated FM-1 CPU.

The standalone Windows launcher packages Python 3.11 and PyInstaller 6.22.3.
Python retains the PSF License and incorporated component notices. PyInstaller's
GPL license includes its bootloader exception, permitting distribution of the
bundled original application under its own license. The build includes the
installed runtime license and PyInstaller license with the package.

The esptool flasher is installed separately at version 4.5.1. Its source and
license are at <https://github.com/espressif/esptool/tree/v4.5.1>.
