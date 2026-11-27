#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Host unit test for src/capture_crop.h: the part of a camera picture that
fills a capture format of another shape, so that the 16:9 picture of a Mac's
camera is not squeezed into the 4:3 of a Windows program. Pure integer math:
clang++ with ASan/UBSan directly on the header's own test.

    build/analysis-venv/bin/python tools/test_capture_crop_native.py
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
OUT = (args.out or neo98.output_directory('capture-crop')).resolve()
OUT.mkdir(parents=True, exist_ok=True)

env = neo98.tool_environment()
env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
env['ASAN_OPTIONS'] = 'halt_on_error=1'
binary = OUT / 'test-capture-crop'
build = subprocess.run(
    ['clang++', '-std=c++17', '-O1', '-g', '-Wall', '-fsanitize=address,undefined',
     str(ROOT / 'tools/test_capture_crop_native.cpp'), '-o', str(binary)],
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
