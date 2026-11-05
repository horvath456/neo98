#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Import contract of the WINMM fixture: the timer family, the device queries
without a device and the three error texts, plus the kernel calls they need."""
IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'CreateEventA': 16, 'WaitForSingleObject': 8,
                 'CloseHandle': 4, 'Sleep': 4, 'GetTickCount': 0},
    'WINMM': {'timeGetDevCaps': 8, 'timeBeginPeriod': 4, 'timeEndPeriod': 4,
              'timeSetEvent': 20, 'timeKillEvent': 4, 'timeGetTime': 0,
              'waveInGetNumDevs': 0, 'midiOutGetNumDevs': 0, 'mixerGetNumDevs': 0,
              'joyGetNumDevs': 0, 'auxGetNumDevs': 0, 'midiInGetNumDevs': 0,
              'waveInGetDevCapsA': 12, 'midiOutGetDevCapsA': 12, 'mixerGetDevCapsA': 12,
              'joyGetDevCapsA': 12, 'joyGetPos': 8, 'joyGetPosEx': 8,
              'waveOutGetVolume': 8, 'waveOutSetVolume': 8, 'midiOutSetVolume': 8, 'midiOutGetVolume': 8,
              'waveOutGetErrorTextA': 12, 'midiOutGetErrorTextA': 12, 'mciGetErrorStringA': 12,
              'waveInOpen': 24, 'mixerOpen': 20, 'mixerClose': 4, 'OpenDriver': 12, 'PlaySoundA': 12,
              'OpenDriverA': 12, 'DrvOpen': 12, 'CloseDriver': 12, 'DrvClose': 12,
              'SendDriverMessage': 16, 'DefDriverProc': 20, 'DrvDefDriverProc': 20,
              'GetDriverModuleHandle': 4, 'DrvGetModuleHandle': 4, 'GetDriverFlags': 4,
              'mciSendStringA': 16, 'mmioWrite': 12},
}
