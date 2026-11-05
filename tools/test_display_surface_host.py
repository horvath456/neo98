#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Framebuffer allocation and colour-depth changes retain existing pixels."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
SOURCES=['gdi','dibeng','dib_text','win98_rect','user','user_draw','text_edit','win98_dialog','win98_parameters']
out=ROOT/'build/display-surface';out.mkdir(parents=True,exist_ok=True)
binary=out/'test-display-surface'
subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(ROOT/'src'),str(ROOT/'tools/test_display_surface_host.cpp'),*[str(ROOT/'src'/(s+'.cpp')) for s in SOURCES],'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
