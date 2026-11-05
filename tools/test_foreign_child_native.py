#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check screen saver preview children across processes under ASan and UBSan."""
import os
import subprocess

from native_app import ROOT
from test_user_region import SOURCES
from native_build import compile_objects
import neo98


def main():
    out = ROOT / 'build/foreign-child'
    out.mkdir(parents=True, exist_ok=True)
    flags = ['-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
             '-fno-omit-frame-pointer', '-I' + str(ROOT / 'src')]
    env = neo98.tool_environment()
    objects = compile_objects(ROOT, [ROOT / 'src' / (name + '.cpp') for name in SOURCES],
                              flags, env, out / 'objects')
    binary = out / 'test-foreign-child'
    subprocess.run(['clang++', *flags, str(ROOT / 'tools/test_foreign_child_native.cpp'),
                    *map(str, objects), '-o', str(binary)], env=env, check=True)
    result = subprocess.run([str(binary)], env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1'),
                            capture_output=True, text=True)
    (out / 'run.log').write_text(result.stdout + result.stderr)
    print(result.stdout + result.stderr, end='')
    result.check_returncode()


if __name__ == '__main__':
    main()
