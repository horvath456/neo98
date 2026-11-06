#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""alpha comparisons with fixed function and PS 1/2. Use serial.py."""
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: Metal exists on macOS only')  # macOS only; skipped elsewhere
        _sys.exit(77)
from pathlib import Path
import argparse
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98

parser = argparse.ArgumentParser()
parser.add_argument('--baseline', type=Path)
parser.add_argument('--include-packed-boundary', action='store_true',
                    help='compatibility option; packed boundaries are now always tested')
args = parser.parse_args()
out = ROOT / 'build/d3d-alpha-test'
out.mkdir(exist_ok=True)
# GPU shader instrumentation hides the original miscompile. API validation stays on.
env = dict(neo98.tool_environment(), MTL_DEBUG_LAYER='1', MTL_SHADER_VALIDATION='0',
           SDL_AUDIODRIVER='dummy')
binary = out / ('before' if args.baseline else 'after')
subprocess.run([
    'clang++', '-std=c++17', '-O1', '-fsanitize=address,undefined', '-fobjc-arc',
    '-I', str(ROOT / 'tools'), str(ROOT / 'tools/test_d3d_alpha_test_metal.cpp'),
    '-x', 'objective-c++', str(args.baseline or ROOT / 'tools/host_metal.mm'),
    '-x', 'c++', str(ROOT / 'src/d3d_shader_msl.cpp'),
    '-framework', 'Metal', '-framework', 'Foundation', '-o', str(binary)
], env=env, stdin=subprocess.DEVNULL, check=True)
for mode in ([], ['--packed-sweep']):
    result = subprocess.run([str(binary), *mode], env=env, stdin=subprocess.DEVNULL,
                            capture_output=True, text=True)
    print(result.stdout + result.stderr, end='', flush=True)
    if args.baseline:
        assert result.returncode == 1 and 'FAIL reference=' in result.stdout, result.returncode
        print('PASS: baseline reproduces incorrect alpha-test coverage')
    else:
        assert result.returncode == 0, result.returncode
