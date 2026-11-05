# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
from pathlib import Path
ROOT=next(p for p in Path(__file__).resolve().parents if(p/'neo98.py').is_file());OUT=ROOT/'build/crt-strtod'
IMPORTS={'KERNEL32':{'ExitProcess':4,'GetModuleHandleA':4,'GetProcAddress':8,'CreateThread':24,'WaitForSingleObject':8,'GetExitCodeThread':8,'CloseHandle':4,'Sleep':4}}
CRT_IMPORTS=['strtod','atof','_errno','setlocale']
