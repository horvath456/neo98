# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
from pathlib import Path

ROOT = next(p for p in Path(__file__).resolve().parents if (p / 'neo98.py').is_file())
OUT = ROOT / 'build/window-context-help-family'
IMPORTS = {
    'KERNEL32': {
        'ExitProcess': 4, 'GetLastError': 0, 'SetLastError': 4,
        'GetModuleHandleA': 4,
    },
    'USER32': {
        'RegisterClassA': 4, 'CreateWindowExA': 48, 'DestroyWindow': 4,
        'DefWindowProcA': 16,
        'GetWindowContextHelpId': 4, 'SetWindowContextHelpId': 8,
    },
}
