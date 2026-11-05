#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Reproduce Neo98's eject icons from geometric primitives, without input art."""
from pathlib import Path
import struct


def icon(dialog=False):
    # A teal upward arrow above a detached base. The dialog adds a blue frame.
    pixels = []
    masks = []
    for y in reversed(range(16)):
        bits = 0
        for x in range(16):
            colour = None
            if dialog and (x in (0, 15) or y in (0, 15)):
                colour = (45, 65, 100)
            if (3 <= y <= 7 and abs(x - 7.5) <= y - 2.5) or (6 <= x <= 9 and 7 <= y <= 10):
                colour = (0, 135, 135)
            if 4 <= x <= 11 and 12 <= y <= 13:
                colour = (45, 65, 100)
            if colour is None:
                bits |= 1 << (31 - x)
                pixels.append(b'\0\0\0\0')
            else:
                pixels.append(bytes((*reversed(colour), 255)))
        masks.append(struct.pack('>I', bits))
    payload = struct.pack('<IiiHHIIiiII', 40, 16, 32, 1, 32, 0, 1088, 0, 0, 0, 0)
    payload += b''.join(pixels) + b''.join(masks)
    return struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', 16, 16, 0, 0, 1, 32, len(payload), 22) + payload


if __name__ == '__main__':
    for name, dialog in [('eject-tray', False), ('eject-dialog', True)]:
        Path(__file__).with_name(name + '.ico').write_bytes(icon(dialog))
