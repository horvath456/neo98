#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""mip uploads through Metal with ASan/UBSan (tools/test_mipmap_metal.cpp). macOS only.

    build/analysis-venv/bin/python -I tools/test_mipmap_metal.py


"""
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: Metal exists on macOS only')  # macOS only; skipped elsewhere
        _sys.exit(77)
import os
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
    binary = out / 'test-mipmap-metal'
    env = neo98.tool_environment()
    subprocess.run(['clang++', '-std=c++17', '-O1', '-fsanitize=address,undefined', '-fobjc-arc',
                    '-I', str(ROOT / 'tools'), str(ROOT / 'tools/test_mipmap_metal.cpp'),
                    '-x', 'objective-c++', str(ROOT / 'tools/host_metal.mm'),
                    '-x', 'c++', str(ROOT / 'src/d3d_shader_msl.cpp'),
                    '-framework', 'Metal', '-framework', 'Foundation', '-o', str(binary)], env=env, check=True)
    subprocess.run([str(binary)], env=dict(os.environ, MTL_DEBUG_LAYER='1'), check=True)


if __name__ == '__main__':
    main()
