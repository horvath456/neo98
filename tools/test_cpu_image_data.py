#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""synthetic PE data, protection and backing changes. Use serial.py."""
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: the Unicorn, FEX and ARM64 engines run on macOS only; Linux runs x86 code natively')  # macOS only; skipped elsewhere
        _sys.exit(77)
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import neo98
import arm64_run
import native_app

out = ROOT / 'build/cpu-image-data'
out.mkdir(exist_ok=True)
fex = native_app.process_engine() == 'fex'
flags = ['-std=c++20', '-O2']
extra = []
sources = [ROOT / name for name in ['tools/test_cpu_image_data.cpp', 'src/x86_cpu.cpp',
    'src/x86_cpu_unicorn.cpp', 'src/x86_cpu_dual.cpp', 'src/x86_address_space.cpp',
    'src/pe32.cpp', 'src/win98_exports.cpp', 'src/virtual_memory.cpp']]
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
for dual in [0, 1]:
    for mode in [*range(7), *range(10, 18)]:
        result = arm64_run.run([str(binary), str(mode), str(dual)], timeout=30,
                               env=dict(os.environ, NEO98_FEX_DEBUG='1'), cwd=ROOT)
        text = result.stdout + result.stderr
        (out / f'{binary.name}-{mode}-{dual}.log').write_text(text)
        counts = [int(v) for v in re.findall(r'\bemulations=(\d+)', text)]
        passed = result.returncode == 0 and 'CPU IMAGE DATA PASS' in text
        if fex and mode == 0:
            passed &= counts == [0]
        print(f'mode={mode} dual={dual} emulations={counts} {"PASS" if passed else "FAIL"}', flush=True)
        if not passed:
            print(text[-6000:])
            raise SystemExit(1)
