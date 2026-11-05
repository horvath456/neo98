#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""exercise indirect-cache invalidation at aliasing target addresses."""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import neo98
import arm64_run
import native_app

out = ROOT / 'build/fex-branch-cache'
out.mkdir(exist_ok=True)
library = ROOT / 'build/unicorn-host/lib/libunicorn.2.dylib'
sources = [ROOT / 'tools/test_fex_branch_cache.cpp', ROOT / 'src/x86_cpu.cpp',
           ROOT / 'src/x86_cpu_unicorn.cpp', ROOT / 'src/x86_address_space.cpp']
flags = ['-std=c++20', '-O2']
extra = []
fex = native_app.process_engine() == 'fex'
if fex:
    import build_fex_host
    flags += build_fex_host.compile_flags()
    sources.append(ROOT / 'tools/host_fex_cpu.cpp')
    extra = list(map(str, build_fex_host.libraries()))
binary = out / ('test-fex' if fex else 'test-unicorn')
subprocess.run(['clang++', *flags, *map(str, sources), *extra, str(library),
                '-Wl,-rpath,' + str(library.parent), '-o', str(binary)],
               cwd=ROOT, env=neo98.tool_environment(), check=True)
for stride in [8192, 1048576]:
    result = arm64_run.run([str(binary), str(stride)], timeout=60,
                           env=dict(os.environ, NEO98_FEX_DEBUG='1'), cwd=ROOT)
    text = result.stdout + result.stderr
    (out / f'{binary.name}-{stride}.log').write_text(text)
    print(result.stdout, end='', flush=True)
    if result.returncode or 'CPU BRANCH CACHE PASS' not in text:
        raise SystemExit(text)
