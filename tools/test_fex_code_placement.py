#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""equivalent code placements stop faulting after warmup.

The bound is on emulated accesses, not elapsed time. Also checks every load
and store result. Run through the shared serial.py test lock.
"""
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98
import native_app
import arm64_run

out = ROOT / 'build/fex-code-placement'
out.mkdir(exist_ok=True)
library = ROOT / 'build/unicorn-host/lib/libunicorn.2.dylib'
sources = [ROOT / 'tools/test_fex_code_placement.cpp', ROOT / 'src/x86_cpu.cpp',
           ROOT / 'src/x86_cpu_unicorn.cpp', ROOT / 'src/x86_address_space.cpp']
flags = ['-std=c++20', '-O1']
extra = []
fex = native_app.process_engine() == 'fex'
if fex:
    import build_fex_host
    flags += build_fex_host.compile_flags()
    sources.append(ROOT / 'tools/host_fex_cpu.cpp')
    extra = list(map(str, build_fex_host.libraries()))
binary = out / ('test-placement-fex' if fex else 'test-placement-unicorn')
subprocess.run(['clang++', *flags, *map(str, sources), *extra, str(library),
                '-Wl,-rpath,' + str(library.parent), '-o', str(binary)],
               cwd=ROOT, env=neo98.tool_environment(), check=True)
failures = []
for offset in [0x100, 0xff0, 0xff8, 0x1000, 0x2100, 0x3ff8]:
    for write in [0, 1]:
        result = arm64_run.run([str(binary), str(offset), str(write)], timeout=30,
                               env=dict(os.environ, NEO98_FEX_DEBUG='1'), cwd=ROOT)
        text = result.stdout + result.stderr
        (out / f'{offset}-{write}.log').write_text(text)
        counts = [int(v) for v in re.findall(r'\bemulations=(\d+)', text)]
        passed = result.returncode == 0 and 'CPU CODE PLACEMENT PASS' in text
        if fex:
            passed = passed and len(counts) == 1 and counts[0] <= 8
        print(f'offset={offset:#x} write={write} emulations={counts} {"PASS" if passed else "FAIL"}', flush=True)
        if not passed:
            failures.append((offset, write))
if failures:
    raise SystemExit(f'Code placement failures: {failures}')
