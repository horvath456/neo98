#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""DXT cube upload under Metal validation and sanitizers. Use serial.py."""
from pathlib import Path
import argparse,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
import neo98
p=argparse.ArgumentParser();p.add_argument('--baseline',type=Path);a=p.parse_args()
out=ROOT/'build/d3d-compressed-cube';out.mkdir(exist_ok=True)
env=dict(neo98.tool_environment(),MTL_DEBUG_LAYER='1')
binary=out/('before' if a.baseline else 'after')
subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-fobjc-arc','-I',str(ROOT/'tools'),str(ROOT/'tools/test_d3d_compressed_cube_metal.cpp'),'-x','objective-c++',str(a.baseline or ROOT/'tools/host_metal.mm'),'-x','c++',str(ROOT/'src/d3d_shader_msl.cpp'),'-framework','Metal','-framework','Foundation','-o',str(binary)],env=env,check=True)
r=subprocess.run([str(binary)],env=env,capture_output=True,text=True)
print(r.stdout+r.stderr,flush=True)
if a.baseline:
 assert r.returncode==-6 and 'expect MTLTextureTypeCube' in r.stderr,(r.returncode,r.stderr[-2000:])
 print('PASS: baseline reproduces the compressed-cube texture type assertion')
else:assert r.returncode==0,r.returncode
