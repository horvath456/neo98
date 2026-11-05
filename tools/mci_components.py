#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Import contract of the MCI fixture: the three MCI entries, the error text,
the file calls the guest needs to lay down its sequence and the window calls
it needs to receive MM_MCINOTIFY."""
IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'Sleep': 4, 'GetTickCount': 0, 'CreateFileA': 28,
                 'WriteFile': 20, 'CloseHandle': 4, 'DeleteFileA': 4},
    'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16,
               'DestroyWindow': 4, 'PeekMessageA': 20, 'DispatchMessageA': 4},
    'WINMM': {'mciSendCommandA': 16, 'mciSendStringA': 16, 'mciGetDeviceIDA': 4,
              'mciGetErrorStringA': 12},
}
