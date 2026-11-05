#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Import contract of the wave input fixture: the waveIn family and the
kernel and user calls the guest needs to wait for its callbacks."""
IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'Sleep': 4, 'GetTickCount': 0, 'GetCommandLineA': 0,
                 'CreateEventA': 16, 'WaitForSingleObject': 8, 'CloseHandle': 4,
                 'GetCurrentThreadId': 0, 'OutputDebugStringA': 4},
    'USER32': {'PeekMessageA': 20, 'CreateWindowExA': 48, 'DestroyWindow': 4},
    'WINMM': {'waveInGetNumDevs': 0, 'waveInGetDevCapsA': 12, 'waveInOpen': 24,
              'waveInClose': 4, 'waveInPrepareHeader': 12, 'waveInUnprepareHeader': 12,
              'waveInAddBuffer': 12, 'waveInStart': 4, 'waveInStop': 4, 'waveInReset': 4,
              'waveInGetPosition': 12, 'waveInGetID': 8, 'waveInMessage': 16,
              'mixerGetDevCapsA': 12, 'mixerOpen': 20, 'mixerClose': 4, 'mixerGetID': 12,
              'mixerGetLineInfoA': 12, 'mixerGetLineControlsA': 12, 'mixerSetControlDetails': 12,
              'mixerGetControlDetailsA': 12},
}
