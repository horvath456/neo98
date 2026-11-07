// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// The mark of a paused machine : a dark plate with the two
// bars and the word PAUSE, as rectangles of `unit` pixels around cx, cy.
// fill(x, y, w, h, light) draws one; the renderer and the pixel copy of the
// seamless view both take it.
template <class Fill> inline void PauseMark(int cx, int cy, int unit, Fill fill)
{
    const int left = cx - 22 * unit, top = cy - 20 * unit;
    fill(left, top, 44 * unit, 40 * unit, false);
    fill(left + 13 * unit, top + 5 * unit, 6 * unit, 16 * unit, true);
    fill(left + 25 * unit, top + 5 * unit, 6 * unit, 16 * unit, true);
    static const unsigned char letters[5][7] = {
        {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}, // P
        {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}, // A
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}, // U
        {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}, // S
        {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}, // E
    };
    for (int letter = 0; letter < 5; ++letter)
        for (int row = 0; row < 7; ++row)
            for (int column = 0; column < 5; ++column)
                if (letters[letter][row] & (0x10 >> column))
                    fill(left + (7 + letter * 6 + column) * unit, top + (27 + row) * unit, unit, unit, true);
}
