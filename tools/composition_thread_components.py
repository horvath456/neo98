#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Imports and output directory of the composition thread binding guest test."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/composition-thread'
IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'GetCurrentThreadId': 0, 'CreateThread': 24,
                 'WaitForSingleObject': 8, 'CloseHandle': 4, 'Sleep': 4,
                 'CreateEventA': 16, 'SetEvent': 4, 'GetModuleHandleA': 4,
                 'TlsAlloc': 0, 'TlsSetValue': 8, 'TlsGetValue': 4},
    'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16,
               'DestroyWindow': 4, 'ShowWindow': 8, 'InvalidateRect': 12,
               'PeekMessageA': 20, 'GetMessageA': 16, 'TranslateMessage': 4, 'DispatchMessageA': 4,
               'PostQuitMessage': 4, 'GetDlgItem': 8, 'SendMessageA': 16},
}
