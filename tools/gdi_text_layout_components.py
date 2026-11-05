# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
from pathlib import Path
ROOT = next(p for p in Path(__file__).resolve().parents if (p / 'neo98.py').is_file())
OUT = ROOT / 'build/gdi-text-layout'
IMPORTS = {
 'KERNEL32': {'ExitProcess':4, 'GetModuleHandleA':4, 'GetProcAddress':8},
 'GDI32': {'CreateCompatibleDC':4, 'DeleteDC':4, 'SelectObject':8, 'DeleteObject':4,
 'CreateDIBSection':24, 'SaveDC':4, 'RestoreDC':8, 'SetTextAlign':8, 'GetTextAlign':4,
 'SetTextCharacterExtra':8, 'GetTextCharacterExtra':4, 'SetTextJustification':12,
 'MoveToEx':16, 'GetCurrentPositionEx':8, 'GetTextExtentPoint32A':16, 'GetTextMetricsA':8,
 'TextOutA':20, 'ExtTextOutA':32, 'SetTextColor':8, 'SetBkColor':8, 'SetBkMode':8,
 'SetMapMode':8, 'SetWindowExtEx':16, 'SetViewportExtEx':16},
}
