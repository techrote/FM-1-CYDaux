"""Profile-aware simulated FM-1. It emits no invented CC acknowledgement.

SLOOP INFO/LIVE framing follows the source audit in docs/PROTOCOL_AUDIT.md.
All values are synthetic, never measurements from an attached instrument.
"""

from __future__ import annotations

from typing import Iterable

from .midi_io import EndpointUnavailable, MockMidi, validate_midi_bytes


PROFILE_RULES = {
    "sloop": {"channels": {0, 1, 2, 9}, "cc": {7, 10, 71, 72, 73, 74, 91, 94}, "pc": True},
    "skiff": {"channels": set(range(6)), "cc": {7, 10, 71, 72, 73, 74, 91, 94}, "pc": True},
    "melodee": {"channels": set(range(4)), "cc": {5, 7, 10, 71, 72, 73, 74, 75}, "pc": False},
    "x0x": {"channels": {1, 2}, "cc": set(range(16, 24)) | {7}, "pc": False},
}
SYSEX_PREFIX = (0xF0, 0x7D, 0x46, 0x4C)
ENGINE_NAMES = (
    "ANALOG", "DIGITAL", "PHASE", "LOFI", "SAMPLE", "VOICE",
    "TRIO", "WHEEL", "GRAIN", "FM6", "PHYS", "NOISE",
)


def sloop_info() -> tuple[int, ...]:
    identity = b"FELUCCA SLOOP 2.8.5\0"
    counts = bytes((12, 71, 50, 64, 63))
    engines = b"".join(name.encode("ascii") + b"\0" for name in ENGINE_NAMES)
    return tuple(bytes((*SYSEX_PREFIX, 0x01)) + identity + counts + engines + bytes((4, 12, 0xF7)))


class DemoMidi(MockMidi):
    """Accept the qualified profile subset; SLOOP can answer INFO and LIVE."""

    def __init__(self, profile: str = "sloop") -> None:
        super().__init__(echo=False)
        self._now = 0.0
        self.set_profile(profile)

    def set_profile(self, profile: str) -> None:
        if profile not in PROFILE_RULES:
            raise ValueError("Unknown mock profile")
        self.profile = profile
        self.detail = f"mock {profile}: simulated values, no instrument"
        self._incoming.clear()
        self.controls: dict[tuple[int, int], int] = {}
        self.programs: dict[int, int] = {}
        self.notes: set[tuple[int, int]] = set()
        self.playing = False
        self.bpm = 120
        self.step = 0
        self._play_origin = self._now

    def tick(self, now: float) -> None:
        self._now = now
        if self.playing:
            self.step = int(max(0.0, now - self._play_origin) * self.bpm / 60.0 * 4) % 64

    def _set_playing(self, playing: bool) -> None:
        if playing and not self.playing:
            self._play_origin = self._now - self.step * 60.0 / (self.bpm * 4)
        self.playing = playing

    def _live(self, op: int) -> tuple[int, ...]:
        # Upstream emits 50 payload bytes INCLUDING op: 49 state bytes.
        state = [0] * 49
        state[0] = int(self.playing)
        biased_bpm = self.bpm + 8192
        state[2] = biased_bpm & 0x7F
        state[3] = (biased_bpm >> 7) & 0x7F
        state[17] = 1  # one bar in this synthetic song entry
        for track in range(4):
            state[19 + track * 3] = self.step
            state[20 + track * 3] = 64
            state[21 + track * 3] = 0  # no synthesized audio or invented meter
        state[33:49] = [72] * 16  # default packed drum tune/decay
        return (*SYSEX_PREFIX, 0x4D, op, *state, 0xF7)

    def _sysex(self, message: tuple[int, ...]) -> None:
        if self.profile != "sloop":
            raise ValueError("Mock profile has no supported SysEx readback")
        if message == (*SYSEX_PREFIX, 0x01, 0xF7):
            self.inject(sloop_info())
        elif message == (*SYSEX_PREFIX, 0x4D, 0, 0xF7):
            self.inject(self._live(0))
        elif (len(message) == 8 and message[:6] == (*SYSEX_PREFIX, 0x4D, 1)
              and message[6] in (0, 1) and message[7] == 0xF7):
            self._set_playing(bool(message[6]))
            self.inject(self._live(1))
        else:
            raise ValueError("Mock does not implement this SysEx command")

    def send(self, data: Iterable[int]) -> None:
        if not self.connected:
            raise EndpointUnavailable("Mock endpoint is closed")
        message = validate_midi_bytes(data)
        status = message[0]
        if status == 0xF0:
            self._sysex(message)
            self.sent.append(message)
            return
        if status >= 0xF0:
            if self.profile == "x0x" and status in (0xFA, 0xFB, 0xFC):
                self._set_playing(status != 0xFC)
            elif status != 0xF8:
                raise ValueError("Mock profile does not implement this real-time command")
            self.sent.append(message)
            return
        kind, channel = status & 0xF0, status & 0x0F
        # Always tolerate channel release, including after changing profiles.
        if kind == 0xB0 and message[1] in (64, 120, 123):
            if message[1] in (120, 123):
                self.notes = {note for note in self.notes if note[0] != channel}
            # X0X ignores sustain, as does this mock. No ACK is fabricated.
            self.sent.append(message)
            return
        rules = PROFILE_RULES[self.profile]
        if channel not in rules["channels"]:
            raise ValueError("Channel is outside this mock profile's qualified subset")
        if kind == 0xB0:
            if message[1] not in rules["cc"]:
                raise ValueError("CC is outside this mock profile's qualified subset")
            # SLOOP's drum track ignores resonance and envelope CCs. Preserve
            # that behavior instead of pretending its synth-only knobs work.
            if not (self.profile == "sloop" and channel == 9 and message[1] in (71, 72, 73)):
                self.controls[(channel, message[1])] = message[2]
        elif kind == 0xC0:
            if not rules["pc"]:
                raise ValueError("Program change is not qualified for this mock profile")
            # The pinned SLOOP build has 46 drum kits. Higher programs on its
            # drum track are ignored, while synth programs retain 0..127.
            if not (self.profile == "sloop" and channel == 9 and message[1] >= 46):
                self.programs[channel] = message[1]
        elif kind in (0x80, 0x90):
            note = (channel, message[1])
            if kind == 0x90 and message[2]:
                self.notes.add(note)
            else:
                self.notes.discard(note)
        elif kind != 0xE0:
            raise ValueError("Message is outside this mock profile's qualified subset")
        self.sent.append(message)
        # Keep sent-state honest: ordinary controls produce no fake readback.
