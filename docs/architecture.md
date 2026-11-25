# Architecture and integration decisions

## Ownership of sound and state

The FM-1 runs its selected musical firmware and owns its audio engine,
sequencer, presets and projects. CYDaux is an external MIDI controller with
firmware-specific capability maps. The CYD runs one profile and one connection
at a time; profile selection is never an FM-1 firmware installation action.

The portable C++17 core contains the profile registry, validated MIDI/BLE codec,
bounded outgoing queue, state origins, SLOOP parser/session guard, renderer,
touch hit testing, and touch-calibration transform. The desktop simulator and
ESP32 application compile those same sources. There is no parallel JavaScript
implementation of controller semantics.

## Connection paths

Direct BLE is a central/client implementation for Melodee's MIDI service.
It uses bounded discovery, requires one matching Melodee name or an explicit
address, checks the service/characteristic/CCCD, and subscribes before reporting
ready. It permits the LE Legacy Just Works pairing required by the audited
Melodee implementation. Direct BLE SysEx is outside this version's scope.

Wi-Fi connects to an explicitly configured computer over TCP. Protocol-1 hello
and token authentication precede MIDI. The computer owns an explicitly named
USB MIDI output and optional input; the input is required for SLOOP live state.
The bridge's mock uses the same protocol with an explicit mock label. It does
not echo all controls as acknowledgements and it produces no audio.

The classic ESP32 CYD cannot directly host the FM-1's USB connection, so the
computer bridge is the supported route for firmware without qualified BLE.
Wi-Fi and BLE are selected modes; simultaneous links are not implemented.

## State and delivery

Parameter values carry a provenance: Unknown, Pending, Sent, Device or Demo.
Controller admission is not transport delivery, and delivery is not device
acceptance. UI counters and Pending→Sent transitions are driven by transport
completion events. Incoming MIDI is never automatically echoed to output.

The controller's 40-message ring coalesces unsent CCs on the same channel/CC.
Critical note release has priority on overflow and covers all successfully
touched/queued note channels. The ESP32 transport independently tracks sent
notes, purges queued MIDI on Panic, and attempts sustain-off/all-notes-off
before closing a reachable link. No performance commands are replayed after
disconnect or USB backend loss.

Connection attempts have monotonically changing generations, and USB readiness
changes also advance a MIDI delivery epoch. Old callbacks and queued commands
cannot join a new session. Sent/lifecycle events have a separate bounded queue
from received MIDI; loss of a critical event explicitly fails the link. BLE
callbacks copy bounded packets to a queue; decoding happens on the worker.

SLOOP identity and LIVE capability are negotiated per connection. There is one
editor request in flight, a one-second response timeout, and a 250 ms read poll
when possible. Play/Stop use explicit target state and are never automatically
replayed after timeout. A reply must match the expected command/operation and
pass layout/range checks before replacing state. The exact source layout and
its differences from upstream prose documentation are in the firmware audit.

## ESP32 responsibilities

The Arduino loop owns the controller, touch, display and setup portal. A
FreeRTOS worker owns BLE and Wi-Fi transport operations. Its blocking calls
have bounded connection timeouts; the touch loop remains independently active.
The UI renders a 320×240 RGB565 display through a 320×40 strip buffer (25 KiB). TFT and
resistive touch use separate SPI buses, with explicit board pins.

Touch calibration uses an affine transform fitted to three raw ADC points and
checks an independent fourth point before writing a versioned Preferences
record. Holding BOOT during the running app requests a fresh calibration after
the transport has closed. Holding BOOT during reset has the ESP32's usual
serial-bootloader meaning.

Setup first closes the active transport and waits for its matching-generation
offline event, then starts a temporary WPA-protected access point. Its random
password is shown exactly on screen, and settings are entered at 192.168.4.1.
The form has bounded fields and an unpredictable per-session nonce; existing
passwords/tokens are not returned in HTML. NVS stores local settings. Setup is
available only on the AP while explicitly open. The app boots into Demo rather
than silently reconnecting to an instrument.

## Validation boundary

Native tests cover musical mappings, malformed MIDI/SysEx, BLE timestamp and
running-status rules, session negotiation/timeouts, queue pressure, note
release, state origins, XY behavior, gesture cancellation and calibration.
Python tests exercise bridge sockets and the actual C++ controller together;
frontend tests exercise shipped event handlers against the HTTP/native engine.
Flash-package checks validate hashes, chip/board labels and partition bounds.
CI builds Linux, Windows and ESP32 artifacts with source identity manifests.

These checks cannot establish physical radio discovery/pairing, panel wiring,
touch feel, USB driver compatibility, audio changes or runtime heap/stack
headroom. The hardware acceptance record intentionally remains pending.
