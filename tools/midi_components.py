#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Import contract of the MIDI fixture: the midiOut and midiStream families
and the three kernel calls the guest needs to wait for its callbacks."""
IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'Sleep': 4, 'GetTickCount': 0},
    'WINMM': {'midiOutGetNumDevs': 0, 'midiOutGetDevCapsA': 12, 'midiOutOpen': 20,
              'midiOutClose': 4, 'midiOutReset': 4, 'midiOutShortMsg': 8,
              'midiOutLongMsg': 12, 'midiOutPrepareHeader': 12,
              'midiOutUnprepareHeader': 12, 'midiOutGetVolume': 8, 'midiOutSetVolume': 8,
              'midiOutMessage': 16, 'midiOutGetErrorTextA': 12,
              'midiStreamOpen': 24, 'midiStreamClose': 4, 'midiStreamOut': 12,
              'midiStreamPause': 4, 'midiStreamRestart': 4, 'midiStreamStop': 4,
              'midiStreamProperty': 12, 'midiStreamPosition': 12},
}
