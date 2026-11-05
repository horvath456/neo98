#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile synthetic release runtime regressions; requires no Windows files."""
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[1]
out = ROOT / 'build/release-runtime'
out.mkdir(parents=True, exist_ok=True)
subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                '-DNEO98_NO_REGISTRY_IMAGE', str(ROOT/'tools/test_release_runtime.cpp'),
                *[str(ROOT/'src'/p) for p in ('registry.cpp','registry_persist.cpp','config_manager.cpp')],
                '-o', str(out/'test')],check=True)
subprocess.run([str(out/'test')],check=True)
print('PASS: all raster truth tables, legacy journal accepted, invalid signature rejected')
