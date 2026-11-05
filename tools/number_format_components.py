# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
from pathlib import Path
ROOT = next(p for p in Path(__file__).resolve().parents if (p / 'neo98.py').is_file())
OUT = ROOT / 'build/nls-format-fixture'
IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'SetLastError': 4, 'GetLastError': 0,
                 'GetNumberFormatA': 24, 'GetCurrencyFormatA': 24},
    'ADVAPI32': {'RegCreateKeyExA': 36, 'RegSetValueExA': 24, 'RegCloseKey': 4},
}
