#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise a native shutdown through the real ARM64 dynrec frame ABI."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98

def main():
    # The generated frame is ARM64 code (the dynrec of DOSBox Pure on macOS).
    import platform
    if sys.platform != 'darwin' or platform.machine() != 'arm64':
        print('SKIP: the DOSBox dynrec frame ABI exists on macOS ARM64 only')
        return 77
    out = neo98.output_directory('dos-dynrec-unwind')
    binary = out / 'test-dos-dynrec-unwind'
    subprocess.run(['clang++', '-std=gnu++11', '-O1', '-g', '-fomit-frame-pointer',
                    '-fsanitize=address,undefined', '-I', str(ROOT / 'tools'),
                    '-I', str(ROOT / 'third_party/dosbox-pure/include'),
                    str(ROOT / 'tools/test_dos_dynrec_unwind.cpp'), '-o', str(binary)], check=True,
                   env=neo98.tool_environment())
    env = dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
    before = subprocess.run([str(binary), '--unregistered'], env=env, capture_output=True, text=True)
    if before.returncode == 0 or 'uncaught exception' not in before.stderr:
        print('FAIL unregistered generated frame reproduces the shutdown abort')
        return 1
    print('ok unregistered generated frame reproduces the shutdown abort', flush=True)
    return subprocess.run([str(binary)], env=env).returncode

if __name__ == '__main__':
    sys.exit(main())
