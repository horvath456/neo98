#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Metal honors all color write masks. Run through serial.py."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
import neo98
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline',type=Path)
a=p.parse_args()
out=ROOT/'build/d3d-color-mask';out.mkdir(exist_ok=True)
binary=out/('before' if a.baseline else 'after')
env=dict(neo98.tool_environment(),MTL_DEBUG_LAYER='1')
subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-fobjc-arc',
 '-I',str(ROOT/'tools'),str(ROOT/'tools/test_d3d_color_mask_metal.cpp'),'-x','objective-c++',
 str(a.baseline or ROOT/'tools/host_metal.mm'),'-x','c++',str(ROOT/'src/d3d_shader_msl.cpp'),
 '-framework','Metal','-framework','Foundation','-o',str(binary)],env=env,check=True)
subprocess.run([str(binary),*(['--expect-broken'] if a.baseline else [])],env=env,check=True)
