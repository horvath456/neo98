# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Distance rounding and phantom origins used by Marlett's hint programs."""
from types import SimpleNamespace
import unittest
from truetype_hint import Interpreter, ONE16


class HintingTests(unittest.TestCase):
    def test_grid_rounding_preserves_sign_and_rounds_magnitude(self):
        interpreter = Interpreter.__new__(Interpreter)
        for state, values in [('RDTG', (0, 0, 64, 64)), ('RUTG', (64, 64, 64, 128))]:
            interpreter.round_state = state
            for distance, expected in zip((1, 63, 64, 65), values):
                self.assertEqual(interpreter.round_value(distance), expected)
                self.assertEqual(interpreter.round_value(-distance), -expected)

    def test_header_origin_flag_overrides_different_hmtx_bearing(self):
        interpreter = Interpreter.__new__(Interpreter)
        interpreter.scale = ONE16
        interpreter.zones = [None, None]
        for flags, origin in ((0, -256), (2, 0)):
            interpreter.font = SimpleNamespace(
                outline=lambda index: ([(0, 128)], [True], [0], (0, 128, 1920, 2048), b''),
                advance=lambda index: (2048, 256), win_ascent=2048, win_descent=0,
                head_flags=flags)
            interpreter.load_glyph(0)
            self.assertEqual(interpreter.glyph.orus[1][0], origin)
            self.assertEqual(interpreter.glyph.orus[2][0], origin + 2048)


if __name__ == '__main__':
    unittest.main()
