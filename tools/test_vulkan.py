#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the Vulkan renderer offscreen, including screen-space sky depth."""
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]


def main():
    if sys.platform != 'linux' or platform.machine() != 'x86_64':
        print('SKIP: Vulkan host requires Linux x64')
        return 77
    import neo98
    from native_app import vulkan_objects
    out = ROOT / 'build/vulkan-test'
    out.mkdir(parents=True, exist_ok=True)
    objects = vulkan_objects()
    binary = out / 'test'
    subprocess.run(['clang++', '-std=c++17', '-O2', str(ROOT / 'tools/test_vulkan.cpp'),
                    *map(str, objects), '-lvulkan', '-ldl', '-o', str(binary)],
                   env=neo98.tool_environment(), cwd=ROOT, check=True)
    return subprocess.run([str(binary)], env=dict(os.environ, NEO98_GPU='vulkan')).returncode


if __name__ == '__main__':
    sys.exit(main())
