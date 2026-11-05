#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Regression for clipped caption stems after Marlett moves the pen origin."""
from marlett_glyphs import DEFAULT_FONT, table
from truetype_hint import verify

def test():
    _, cells = table(sizes=[10])
    # Pixel masks of the grid-aligned rectangular outlines at the caption's
    # 10-pixel inner size. Check both stems and the overlap, not just advance.
    expected = {
        '1': [0x1ff,0x1ff,0x101,0x101,0x101,0x101,0x101,0x101,0x1ff,0],
        '2': [0xfc,0xfc,0x84,0xbf,0xbf,0xe1,0x21,0x21,0x3f,0],
    }
    for code, rows in expected.items():
        assert cells[10,code][0] == rows, (code,cells[10,code][0])
    # The close cross 'r' is symmetric at every caption size: a pixel centre
    # on the right edge of a stroke is inside, as the scan converter's rule
    # says. Before, the right strokes lost a column.
    _, crosses = table(sizes=range(6, 25))
    for size in range(6, 25):
        rows, width, _ = crosses[size, 'r']
        mask = [[bool(row >> column & 1) for column in range(width)] for row in rows]
        columns = [column for column in range(width) if any(line[column] for line in mask)]
        left, right = min(columns), max(columns)
        for line in mask:
            assert line[left:right + 1] == line[left:right + 1][::-1], (size, rows)
    checks=verify(DEFAULT_FONT)
    assert not checks['hdmx']['mismatch'], checks
    print('MARLETT ORIGIN PASS: maximize/restore pixels and', checks['hdmx']['checked'], 'original advances')
if __name__=='__main__':test()
