#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Import contract of the DirectSoundCapture fixture. DSOUND.DLL is loaded
by hand, so only the loader, the clock and the command line are imported."""
IMPORTS = {
    'KERNEL32': {'LoadLibraryA': 4, 'GetProcAddress': 8, 'OutputDebugStringA': 4,
                 'ExitProcess': 4, 'Sleep': 4, 'GetTickCount': 0, 'GetCommandLineA': 0},
}
