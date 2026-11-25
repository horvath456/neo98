#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Reproduce Neo98's Unplug or Eject Hardware icon from the pixel map below, without input art."""
from pathlib import Path
import struct

# A green arrow that points up out of a grey card with gold contacts, in the
# 16 Windows colours. '.' is transparent.
ART = (
    "....K...........",
    "...KGK..........",
    "..KGGGK.........",
    ".KGGGGGK........",
    "KGGGGGGgK.......",
    "KKKGGgKKK.......",
    "..KGGgKKKKKKKKK.",
    "..KGGgKWWWWWWWLK",
    "..KGGgKLLLLLLLDK",
    "..KKKKKLWWWWWLDK",
    "..KWLLLLWDDDWLDK",
    "..KWLLLLWWWWWLDK",
    "..KWLLLLLLLLLLDK",
    "..KWYDYDYDYDYDDK",
    "..KDDDDDDDDDDDDK",
    "..KKKKKKKKKKKKKK",
)
COLOURS = {'K': (0, 0, 0), 'G': (0, 255, 0), 'g': (0, 128, 0), 'W': (255, 255, 255),
           'L': (192, 192, 192), 'D': (128, 128, 128), 'Y': (128, 128, 0)}


def icon():
    pixels = []
    masks = []
    for row in reversed(ART):
        bits = 0
        for x, key in enumerate(row):
            if key == '.':
                bits |= 1 << (31 - x)
                pixels.append(b'\0\0\0\0')
            else:
                pixels.append(bytes((*reversed(COLOURS[key]), 255)))
        masks.append(struct.pack('>I', bits))
    payload = struct.pack('<IiiHHIIiiII', 40, 16, 32, 1, 32, 0, 1088, 0, 0, 0, 0)
    payload += b''.join(pixels) + b''.join(masks)
    return struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', 16, 16, 0, 0, 1, 32, len(payload), 22) + payload


if __name__ == '__main__':
    Path(__file__).with_name('eject.ico').write_bytes(icon())
