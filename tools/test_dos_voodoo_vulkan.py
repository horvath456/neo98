#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DOS Voodoo Vulkan buffers and saved-state restoration, without Windows."""
import os
import platform
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))

def main():
    if sys.platform != 'linux' or platform.machine() != 'x86_64':
        print('SKIP: Vulkan host requires Linux x64')
        return 77
    import neo98
    from native_app import vulkan_objects
    out = ROOT / 'build/dos-voodoo-vulkan'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'test'
    env = neo98.tool_environment()
    objects = vulkan_objects()
    subprocess.run(['clang++', '-std=c++17', '-O2', '-DNEO98_WITH_VULKAN',
                    str(ROOT / 'tools/test_dos_voodoo_vulkan.cpp'), 'src/dos_voodoo.cpp',
                    *map(str, objects), '-lvulkan', '-ldl', '-o', str(binary)], env=env, cwd=ROOT, check=True)
    result = subprocess.run([str(binary)], env=dict(os.environ, NEO98_GPU='vulkan'))
    if result.returncode:
        return result.returncode
    # No device and explicit opt-out must leave the host without a GPU.
    for changes in ({'NEO98_GPU': 'none'}, {'NEO98_GPU': 'vulkan', 'VK_ICD_FILENAMES': '/nonexistent/neo98.json'}):
        result = subprocess.run([str(binary)], env=dict(os.environ, **changes), capture_output=True, text=True)
        assert result.returncode == 77 and 'SKIP no Vulkan device' in result.stdout, result
        print('ok software fallback selection', changes)
    return 0

if __name__ == '__main__':
    sys.exit(main())
