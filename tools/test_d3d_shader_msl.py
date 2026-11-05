#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Pixel shaders 1.1 to 1.4 through the MSL translation of the Direct3D HAL
and the Metal compiler (tools/test_d3d_shader_msl.cpp). macOS only.

    build/analysis-venv/bin/python tools/test_d3d_shader_msl.py


"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98  # noqa: E402


def main():
    if sys.platform != 'darwin':
        raise SystemExit('the Metal backend is macOS only')
    out = ROOT / 'build/direct3d'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'test-d3d-shader-msl'
    env = neo98.tool_environment()
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined', '-fobjc-arc',
                    '-I', str(ROOT / 'tools'), str(ROOT / 'tools/test_d3d_shader_msl.cpp'),
                    '-x', 'objective-c++', str(ROOT / 'tools/host_metal.mm'),
                    '-x', 'c++', str(ROOT / 'src/d3d_shader_msl.cpp'),
                    '-framework', 'Metal', '-framework', 'Foundation', '-o', str(binary)], env=env, check=True)
    subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
