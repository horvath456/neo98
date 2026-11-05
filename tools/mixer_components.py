#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Import contract of the mixer fixture: the mixer family of WINMM, the wave
entries it compares against and the two kernel calls it needs."""
IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'Sleep': 4},
    'WINMM': {'mixerGetNumDevs': 0, 'mixerGetDevCapsA': 12, 'mixerOpen': 20, 'mixerClose': 4,
              'mixerGetID': 12, 'mixerGetLineInfoA': 12, 'mixerGetLineControlsA': 12,
              'mixerGetControlDetailsA': 12, 'mixerSetControlDetails': 12, 'mixerMessage': 16,
              'waveOutGetVolume': 8, 'waveOutSetVolume': 8, 'waveOutOpen': 24, 'waveOutClose': 4,
              'waveOutPrepareHeader': 12, 'waveOutWrite': 12, 'waveOutReset': 4},
}
