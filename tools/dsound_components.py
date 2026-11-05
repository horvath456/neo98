#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Import contract of the DirectSound fixture.

DSOUND.DLL itself is loaded by hand at run time, so nothing of DirectSound is
in here. What the fixture needs is the loader, the sleep it waits with, the clock
it measures the play cursor against and the window it hands to
SetCooperativeLevel.
"""
IMPORTS = {
    'KERNEL32': {'LoadLibraryA': 4, 'GetProcAddress': 8, 'OutputDebugStringA': 4,
                 'ExitProcess': 4, 'Sleep': 4, 'GetTickCount': 0},
    'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16,
               'ShowWindow': 8, 'SetForegroundWindow': 4},
}
