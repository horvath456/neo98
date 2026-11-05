#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build CHURN.EXE from tools/gdi_churn_launch_fixture.c."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/gdi-churn-launch'
IMPORTS = {'KERNEL32': {'ExitProcess': 4, 'CreateProcessA': 40, 'GetCommandLineA': 0},
           'USER32': {'GetMessageA': 16, 'DispatchMessageA': 4},
           'GDI32': {'CreateSolidBrush': 4, 'DeleteObject': 4}}


def build():
    sys.path.insert(0, str(ROOT))
    import neo98
    env = neo98.tool_environment()
    OUT.mkdir(parents=True, exist_ok=True)
    libs = []
    for dll, functions in IMPORTS.items():
        definition = OUT / f'{dll}.def'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' +
                              ''.join(f'{name}@{size}\n' for name, size in functions.items()))
        library = OUT / f'{dll}.lib'
        subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)],
                       env=env, check=True)
        libs.append(str(library))
    obj, exe = OUT / 'churn.obj', OUT / 'CHURN.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-c', str(ROOT / 'tools/gdi_churn_launch_fixture.c'), '-o', str(obj)],
                   env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0', '/nodefaultlib',
                    '/machine:x86', '/out:' + str(exe), str(obj), *libs], env=env, check=True)
    return exe


if __name__ == '__main__':
    print(build())
