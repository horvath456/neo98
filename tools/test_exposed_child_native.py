#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""exposed children and ancestor invalidation, with ASan/UBSan. Use serial.py."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    out = ROOT / 'build/exposed-child-native'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'exposed-child'
    sources = ['gdi', 'dib_text', 'dibeng', 'win98_rect', 'user_draw',
               'text_edit', 'win98_dialog', 'win98_parameters']
    command = ['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
               '-fno-omit-frame-pointer', str(ROOT / 'tools/test_exposed_child_native.cpp'),
               *[str(ROOT / 'src' / (name + '.cpp')) for name in sources], '-o', str(binary)]
    try:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        (out / 'build.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            print(result.stdout + result.stderr)
        result.check_returncode()
        env = dict(os.environ, UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        result = subprocess.run([str(binary)], cwd=ROOT, env=env, capture_output=True, text=True)
        (out / 'run.log').write_text(result.stdout + result.stderr)
        print(result.stdout + result.stderr, end='')
        result.check_returncode()
    finally:
        shutil.rmtree(str(binary) + '.dSYM', ignore_errors=True)


if __name__ == '__main__':
    main()
