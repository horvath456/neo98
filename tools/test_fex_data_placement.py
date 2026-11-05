#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""learn accesses independently and retain page rights. Use serial.py."""
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import neo98
import arm64_run
import native_app

out = ROOT / 'build/fex-data-placement'
out.mkdir(exist_ok=True)
fex = native_app.process_engine() == 'fex'
flags = ['-std=c++20', '-O2']
extra = []
sources = [ROOT / 'tools/test_fex_data_placement.cpp', ROOT / 'src/x86_cpu.cpp',
           ROOT / 'src/x86_cpu_unicorn.cpp', ROOT / 'src/x86_address_space.cpp']
if fex:
    import build_fex_host
    flags += build_fex_host.compile_flags()
    sources.append(ROOT / 'tools/host_fex_cpu.cpp')
    extra = list(map(str, build_fex_host.libraries()))
library = ROOT / 'build/unicorn-host/lib/libunicorn.2.dylib'
binary = out / ('test-fex' if fex else 'test-unicorn')
subprocess.run(['clang++', *flags, *map(str, sources), *extra, str(library),
                '-Wl,-rpath,' + str(library.parent), '-o', str(binary)],
               cwd=ROOT, env=neo98.tool_environment(), check=True)
failed = False
for mode in [0, 1, 4, 5, 6, 7]:
    result = arm64_run.run([str(binary), str(mode)], timeout=30,
                           env=dict(os.environ, NEO98_FEX_DEBUG='1'), cwd=ROOT)
    text = result.stdout + result.stderr
    (out / f'{binary.name}-{mode}.log').write_text(text)
    counts = [int(v) for v in re.findall(r'\bemulations=(\d+)', text)]
    passed = result.returncode == 0 and 'CPU DATA PLACEMENT PASS' in text
    # Two stores must learn independently: four faults per store. The old
    # page-wide promotion incorrectly learned both together after four.
    if fex and mode in [0, 4, 5, 6, 7]:
        passed = passed and counts == [8]
    print(f'mode={mode} emulations={counts} {"PASS" if passed else "FAIL"}', flush=True)
    failed |= not passed
if '--bench' in sys.argv:
    for repeat in range(3):
        for mode in [2, 3]:
            result = arm64_run.run([str(binary), str(mode)], timeout=60, cwd=ROOT)
            print(result.stdout, flush=True, end='')
            if result.returncode:
                raise SystemExit(result.stderr)
if failed:
    raise SystemExit(1)
