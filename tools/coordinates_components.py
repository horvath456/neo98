# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
from pathlib import Path
ROOT=next(p for p in Path(__file__).resolve().parents if(p/'neo98.py').is_file());OUT=ROOT/'build/coordinates-family'
IMPORTS={'KERNEL32':dict(ExitProcess=4,GetModuleHandleA=4,GetProcAddress=8,GetLastError=0,SetLastError=4),'USER32':dict(RegisterClassA=4,CreateWindowExA=48,DestroyWindow=4,DefWindowProcA=16,ClientToScreen=8,ScreenToClient=8,MapWindowPoints=16,GetClientRect=8,GetWindowRect=8,GetDesktopWindow=0,FindWindowA=8,MoveWindow=24,ShowWindow=8,IsIconic=4)}
