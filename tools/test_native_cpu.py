#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build tools/test_native_cpu.cpp and run it with both native modes (ptrace
and direct). Linux x86_64 only; elsewhere the test is skipped (exit 77)."""
from pathlib import Path
import os, platform, subprocess, sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['tools/test_native_cpu.cpp', 'src/x86_cpu.cpp', 'src/x86_address_space.cpp',
           'src/jit_memory.cpp', 'tools/host_native_cpu.cpp', 'tools/direct_bridge.S']


def main():
    if sys.platform != 'linux' or platform.machine() not in ('x86_64', 'AMD64'):
        print('SKIP: the native CPU runs on Linux x86_64 only')
        return 77
    sys.path.insert(0, str(ROOT))
    import neo98
    environment = neo98.tool_environment()
    out = ROOT / 'build/native-cpu-test'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'native-cpu'
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-DNEO98_NO_UNICORN',
                    *(str(ROOT / source) for source in SOURCES), '-ldl', '-pthread', '-o', str(binary)],
                   cwd=ROOT, env=environment, check=True)
    for mode in ('ptrace', 'direct'):
        result = subprocess.run([str(binary)], env={**os.environ, 'NEO98_NATIVE_MODE': mode},
                                capture_output=True, text=True, timeout=120)
        print(f'{mode}: {(result.stdout + result.stderr).strip()}')
        if result.returncode:
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
