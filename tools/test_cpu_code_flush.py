#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Translated code survives FlushCode and x87 switches inside hooks.

    build/analysis-venv/bin/python tools/test_cpu_code_flush.py
    NEO98_CPU=unicorn build/analysis-venv/bin/python tools/test_cpu_code_flush.py

Builds tools/test_cpu_code_flush.cpp against the CPU layer (and FEXCore where
native_app.process_engine says so) and runs it under that engine.
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import neo98  # noqa: E402

OUT = ROOT / 'build/cpu-code-flush'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    import native_app
    fex = native_app.process_engine() == 'fex'
    library = ROOT / 'build/unicorn-host/lib/libunicorn.2.dylib'
    sources = [ROOT / 'tools/test_cpu_code_flush.cpp', ROOT / 'src/x86_cpu.cpp', ROOT / 'src/x86_cpu_unicorn.cpp',
               ROOT / 'src/x86_address_space.cpp']
    flags = ['-std=c++20', '-O1', '-g']
    extra = []
    if fex:
        import build_fex_host
        flags += build_fex_host.compile_flags()
        sources.append(ROOT / 'tools/host_fex_cpu.cpp')
        extra = [str(p) for p in build_fex_host.libraries()]
    binary = OUT / ('test-cpu-code-flush-fex' if fex else 'test-cpu-code-flush')
    from native_build import compile_objects
    objects = compile_objects(ROOT, sources, flags, env, OUT / 'object-cache')
    subprocess.run(['clang++', *flags, *map(str, objects), *extra, str(library),
                    '-Wl,-rpath,' + str(library.parent), '-o', str(binary)], cwd=ROOT, env=env, check=True)
    result = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
    lines = (result.stdout + result.stderr).splitlines()
    print('\n'.join(lines))
    if result.returncode or not any('CPU CODE FLUSH PASS' in l for l in lines):
        sys.exit('CPU CODE FLUSH FAILED')


if __name__ == '__main__':
    main()
