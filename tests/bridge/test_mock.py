"""Profile semantics and exact source-qualified SLOOP reply framing."""

import unittest

from bridge.mock import DemoMidi, SYSEX_PREFIX


class ProfileMockTests(unittest.TestCase):
    def test_supported_controls_are_consumed_without_inventing_acknowledgement(self):
        for profile, channel, cc in (("sloop", 0, 74), ("skiff", 5, 91),
                                     ("melodee", 3, 75), ("x0x", 1, 16)):
            with self.subTest(profile=profile):
                mock = DemoMidi(profile)
                mock.send([0xB0 | channel, cc, 90])
                self.assertEqual(mock.controls[(channel, cc)], 90)
                self.assertEqual(mock.receive(), [])

    def test_unknown_cc_and_unqualified_pc_are_not_fake_confirmed(self):
        for profile in ("sloop", "skiff", "melodee", "x0x"):
            with self.subTest(profile=profile):
                mock = DemoMidi(profile)
                channel = 1 if profile == "x0x" else 0
                with self.assertRaises(ValueError):
                    mock.send([0xB0 | channel, 99, 90])
                self.assertEqual(mock.receive(), [])
        for profile in ("melodee", "x0x"):
            mock = DemoMidi(profile)
            with self.assertRaises(ValueError):
                mock.send([0xC1, 12])

    def test_sloop_info_has_real_identity_counts_names_and_protocol_tail(self):
        mock = DemoMidi("sloop")
        mock.send([0xF0, 0x7D, 0x46, 0x4C, 1, 0xF7])
        reply = bytes(mock.receive()[0])
        prefix = bytes((0xF0, 0x7D, 0x46, 0x4C, 1)) + b"FELUCCA SLOOP 2.8.5\0"
        self.assertTrue(reply.startswith(prefix + bytes((12, 71, 50, 64, 63))))
        self.assertIn(b"ANALOG\0DIGITAL\0", reply)
        self.assertTrue(reply.endswith(b"NOISE\0" + bytes((4, 12, 0xF7))))
        self.assertLessEqual(len(reply), 256)

    def test_sloop_live_is_56_bytes_and_playhead_moves_only_while_playing(self):
        mock = DemoMidi("sloop")
        mock.tick(10.0)
        mock.send([*SYSEX_PREFIX, 0x4D, 1, 1, 0xF7])
        started = mock.receive()[0]
        self.assertEqual(len(started), 56)
        self.assertEqual(started[:7], (*SYSEX_PREFIX, 0x4D, 1, 1))
        self.assertEqual(started[8:10], (120, 64))  # biased BPM 120+8192
        mock.tick(10.5)
        mock.send([*SYSEX_PREFIX, 0x4D, 0, 0xF7])
        live = mock.receive()[0]
        self.assertEqual(live[25], 4)  # track 1 step, from source state offset 19
        self.assertEqual(live[26], 64)
        self.assertEqual(live[-17:-1], (72,) * 16)
        mock.send([*SYSEX_PREFIX, 0x4D, 1, 0, 0xF7])
        mock.receive()
        mock.tick(20.0)
        mock.send([*SYSEX_PREFIX, 0x4D, 0, 0xF7])
        stopped = mock.receive()[0]
        self.assertEqual(stopped[6] & 1, 0)
        self.assertEqual(stopped[25], 4)

    def test_profile_change_discards_old_readback_and_sloop_queries_fail_elsewhere(self):
        mock = DemoMidi("sloop")
        mock.send([*SYSEX_PREFIX, 1, 0xF7])
        mock.set_profile("skiff")
        self.assertEqual(mock.receive(), [])
        with self.assertRaises(ValueError):
            mock.send([*SYSEX_PREFIX, 0x4D, 0, 0xF7])

    def test_x0x_sustain_ignore_does_not_block_panic(self):
        mock = DemoMidi("x0x")
        mock.send([0x91, 60, 100])
        mock.send([0xB1, 64, 0])
        mock.send([0xB1, 123, 0])
        self.assertEqual(mock.notes, set())
        self.assertEqual(mock.receive(), [])

    def test_sloop_drum_track_ignores_synth_only_resonance_and_envelopes(self):
        mock = DemoMidi("sloop")
        for cc in (71, 72, 73):
            mock.send([0xB9, cc, 90])
            self.assertNotIn((9, cc), mock.controls)
        mock.send([0xB9, 74, 90])
        self.assertEqual(mock.controls[(9, 74)], 90)
        self.assertEqual(mock.receive(), [])

    def test_sloop_drum_programs_ignore_values_outside_46_kits(self):
        mock = DemoMidi("sloop")
        mock.send([0xC9, 45])
        self.assertEqual(mock.programs[9], 45)
        mock.send([0xC9, 46])
        self.assertEqual(mock.programs[9], 45)
        mock.send([0xC0, 127])
        self.assertEqual(mock.programs[0], 127)
        self.assertEqual(mock.receive(), [])


if __name__ == "__main__":
    unittest.main()
