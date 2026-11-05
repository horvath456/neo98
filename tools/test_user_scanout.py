#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check deferred GPU scanout and GDI access with ASan and UBSan."""
import os
from pathlib import Path
import subprocess
from native_app import ROOT
from test_user_region import SOURCES
import neo98

def main():
    out = ROOT / 'build/native-linux/user-scanout'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'test-user-scanout'
    command = ['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
               '-fno-omit-frame-pointer', '-I' + str(ROOT / 'src'),
               str(ROOT / 'tools/test_user_scanout.cpp')]
    command += [str(ROOT / 'src' / (name + '.cpp')) for name in SOURCES]
    subprocess.run([*command, '-o', str(binary)], env=neo98.tool_environment(), check=True)
    subprocess.run([str(binary)], env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1'), check=True)

if __name__ == '__main__':
    main()
