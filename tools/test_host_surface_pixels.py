#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Exact Metal readback pixels, including format rounding and padded rows."""
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98
out = ROOT / 'build/host-surface-pixels'
out.mkdir(parents=True, exist_ok=True)
binary = out / 'test-host-surface-pixels'
subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                str(ROOT / 'tools/test_host_surface_pixels.cpp'), '-o', str(binary)],
               env=neo98.tool_environment(), check=True)
subprocess.run([str(binary)], check=True)
