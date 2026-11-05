#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Test CpuBackend::None and transfer of its address space to Unicorn. Build tools/test_address_space.cpp with AddressSanitizer and UndefinedBehaviorSanitizer, then run it."""
import subprocess
import sys
from pathlib import Path

def _engine_link_extra():
    """The FEXCore provider for this runner when the host links it
    (tools/native_app.py, engine_link_extra), else nothing."""
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parent))
    import native_app as _native_app
    return [str(p) for p in _native_app.engine_link_extra()]


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98  # noqa: E402

OUT = ROOT / 'build/native-app'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    library = ROOT / 'build/unicorn-host/lib/libunicorn.2.dylib'
    sources = [ROOT / 'tools/test_address_space.cpp', ROOT / 'src/x86_cpu.cpp', ROOT / 'src/x86_cpu_unicorn.cpp', ROOT / 'src/x86_address_space.cpp']
    binary = OUT / 'test-address-space'
    subprocess.run(['clang++', '-std=c++20', '-O1', '-g', '-fsanitize=address,undefined', *map(str, sources),
                    *_engine_link_extra(), str(library), '-Wl,-rpath,' + str(library.parent), '-o', str(binary)],
                   cwd=ROOT, env=neo98.tool_environment(), check=True)
    result = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
    print(result.stdout + result.stderr, end='')
    if result.returncode or 'ADDRESS SPACE PASS' not in result.stdout:
        sys.exit('ADDRESS SPACE FAILED')


if __name__ == '__main__':
    main()
