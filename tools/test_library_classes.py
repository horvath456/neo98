#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""A DLL that registers a window class registers it again after a reload.

Windows 98 takes the classes of a 32-bit DLL away when the process frees the
DLL; a program that loads the DLL a second time then registers them anew. The
classes of the program itself stay.
"""
if __name__ == '__main__':
    import sys as _sys
    if not _sys.platform.startswith('linux'):
        print('SKIP: runs its fixture on the Linux command-line host')  # Linux only; skipped elsewhere
        _sys.exit(77)
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import neo98
import native_app
import win98_c


def main():
    out = ROOT / 'build/library-classes'
    out.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    imports = {'KERNEL32': {'LoadLibraryA': 4, 'FreeLibrary': 4, 'GetProcAddress': 8, 'GetModuleHandleA': 4, 'ExitProcess': 4},
               'USER32': {'RegisterClassA': 4, 'DefWindowProcA': 16}}
    libraries = []
    for dll, exports in imports.items():
        definition, library = out / f'{dll}.def', out / f'{dll}.lib'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' + ''.join(f'{name}@{size}\n' for name, size in exports.items()))
        subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)], env=env, check=True)
        libraries.append(str(library))
    compile_flags = ['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-c',
                     str(ROOT / 'tools/library_classes_fixture.c')]
    link_flags = ['lld-link', '/machine:x86', '/nodefaultlib', '/safeseh:no', '/timestamp:0']
    subprocess.run([*compile_flags, '-DLIBRARY', '-o', str(out / 'library.obj')], env=env, check=True)
    subprocess.run([*link_flags, '/dll', '/entry:DllMain', '/base:0x10000000', str(out / 'library.obj'), *libraries,
                    '/out:' + str(out / 'CLASSLIB.DLL')], env=env, check=True)
    subprocess.run([*compile_flags, '-o', str(out / 'program.obj')], env=env, check=True)
    subprocess.run([*link_flags, '/entry:mainCRTStartup', '/subsystem:console', '/fixed', str(out / 'program.obj'), *libraries,
                    '/out:' + str(out / 'CLASSES.EXE')], env=env, check=True)
    binary = ROOT / 'build/native-app/neo98-cli'
    if not binary.exists():
        binary = native_app.build_fast('neo98_cli.cpp', 'neo98-cli')
    stage = out / 'drive'
    stage.mkdir(exist_ok=True)
    for name in ('CLASSES.EXE', 'CLASSLIB.DLL'):
        (stage / name).write_bytes((out / name).read_bytes())
    result = subprocess.run([str(binary), '--drive-c', str(win98_c.use_pristine()), '--drive', 'E=' + str(stage),
                             '--exe', 'E:\\CLASSES.EXE', '--ram-drive', '--no-registry-file'],
                            env=env, input=b'', capture_output=True, timeout=60)
    (out / 'host.log').write_bytes(result.stdout + result.stderr)
    assert result.returncode == 7, (result.returncode, result.stderr.decode(errors='replace')[-600:])
    print('LIBRARY CLASSES PASS: registered on each of three loads, refused twice within a load, program class kept')


if __name__ == '__main__':
    main()
