#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Import contract of the wave output fixture: the waveOut family, PlaySound
and the three kernel calls the guest needs to wait for its callbacks."""
IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'Sleep': 4, 'GetTickCount': 0, 'GetCurrentThread': 0, 'SetThreadPriority': 8},
    'WINMM': {'waveOutGetNumDevs': 0, 'waveOutGetDevCapsA': 12, 'waveOutOpen': 24,
              'waveOutClose': 4, 'waveOutPrepareHeader': 12, 'waveOutUnprepareHeader': 12,
              'waveOutWrite': 12, 'waveOutPause': 4, 'waveOutRestart': 4, 'waveOutReset': 4,
              'waveOutBreakLoop': 4, 'waveOutGetPosition': 12, 'waveOutGetVolume': 8,
              'waveOutSetVolume': 8, 'waveOutGetID': 8, 'waveOutGetPitch': 8,
              'waveOutSetPlaybackRate': 8, 'waveOutMessage': 16, 'PlaySoundA': 12},
}
