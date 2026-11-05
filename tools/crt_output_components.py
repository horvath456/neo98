# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
from pathlib import Path
ROOT=next(p for p in Path(__file__).resolve().parents if(p/'neo98.py').is_file());OUT=ROOT/'build/crt-output'
IMPORTS={'KERNEL32':{'ExitProcess':4,'GetCommandLineA':0,'GetModuleHandleA':4,'GetProcAddress':8}}
CRT_IMPORTS=['sprintf','vsprintf','_snprintf','_vsnprintf','swprintf','vswprintf','_snwprintf','_vsnwprintf','setlocale','_errno']
