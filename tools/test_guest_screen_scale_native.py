#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Host unit test for tools/guest_screen_scale.h: the coordinate conversion
between the host window's fixed 800x600 canvas and whatever screen the guest
currently has (SM_CXSCREEN/SM_CYSCREEN after a DirectDraw or GDI mode
switch). Pure integer math, no SDL and no Application, so this links nothing
of the runtime: clang++ with ASan/UBSan directly on the header's own test.

    build/analysis-venv/bin/python tools/test_guest_screen_scale_native.py


"""
from pathlib import Path
import argparse
import subprocess
import sys

P = Path(__file__).resolve()
ROOT = next(p for p in P.parents if (p / 'neo98.py').exists() and (p / 'build/analysis-venv').exists())
sys.path.insert(0, str(ROOT))
import neo98  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument('--out', type=Path, default=None)
args = parser.parse_args()
OUT = (args.out or neo98.output_directory('guest-screen-scale')).resolve()
OUT.mkdir(parents=True, exist_ok=True)

env = neo98.tool_environment()
env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
env['ASAN_OPTIONS'] = 'halt_on_error=1'
binary = OUT / 'test-guest-screen-scale'
build = subprocess.run(
    ['clang++', '-std=c++17', '-O1', '-g', '-Wall', '-fsanitize=address,undefined',
     str(ROOT / 'tools/test_guest_screen_scale_native.cpp'), '-o', str(binary)],
    env=env, capture_output=True, text=True)
(OUT / 'build.log').write_text(build.stdout + build.stderr)
if build.returncode:
    print(build.stdout + build.stderr)
build.check_returncode()

run = subprocess.run([str(binary)], env=env, capture_output=True, text=True)
(OUT / 'run.log').write_text(run.stdout + run.stderr)
if run.returncode:
    print(run.stdout + run.stderr)
run.check_returncode()
print(run.stdout.strip())
