# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
from pathlib import Path
ROOT=next(p for p in Path(__file__).resolve().parents if(p/'neo98.py').is_file())
OUT=ROOT/'build/gdi-object-type'
IMPORTS={
 'KERNEL32':{'ExitProcess':4,'GetLastError':0,'SetLastError':4,'GetModuleHandleA':4,'GetProcAddress':8},
 'GDI32':{'GetObjectType':4,'GetCurrentObject':8,'CreateCompatibleDC':4,'DeleteDC':4,'DeleteObject':4,'SelectObject':8,'SelectPalette':12,'GetStockObject':4,'CreatePen':12,'CreateSolidBrush':4,'CreateRectRgn':16,'CreatePalette':4,'CreateDIBSection':24,'CreateDCA':16,'CreateICA':16,'SaveDC':4,'RestoreDC':8},
 'USER32':{'GetDC':4,'ReleaseDC':8},
}
