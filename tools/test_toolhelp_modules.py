#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""A Toolhelp snapshot lists every module of a process with many modules.

The program loads 48 DLLs and walks three module snapshots; each must name
all of them. A snapshot list of a fixed size was written past its end.
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
    out = ROOT / 'build/toolhelp-modules'
    out.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    imports = {'KERNEL32': {'LoadLibraryA': 4, 'CreateToolhelp32Snapshot': 8, 'Module32First': 8, 'Module32Next': 8,
                            'CloseHandle': 4, 'ExitProcess': 4}}
    libraries = []
    for dll, exports in imports.items():
        definition, library = out / f'{dll}.def', out / f'{dll}.lib'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' + ''.join(f'{name}@{size}\n' for name, size in exports.items()))
        subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)], env=env, check=True)
        libraries.append(str(library))
    compile_flags = ['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-c',
                     str(ROOT / 'tools/toolhelp_modules_fixture.c')]
    link_flags = ['lld-link', '/machine:x86', '/nodefaultlib', '/safeseh:no', '/timestamp:0']
    subprocess.run([*compile_flags, '-DLIBRARY', '-o', str(out / 'library.obj')], env=env, check=True)
    subprocess.run([*link_flags, '/dll', '/entry:DllMain', '/base:0x10000000', str(out / 'library.obj'), *libraries,
                    '/out:' + str(out / 'M.DLL')], env=env, check=True)
    subprocess.run([*compile_flags, '-o', str(out / 'program.obj')], env=env, check=True)
    subprocess.run([*link_flags, '/entry:mainCRTStartup', '/subsystem:console', '/fixed', str(out / 'program.obj'), *libraries,
                    '/out:' + str(out / 'MODULES.EXE')], env=env, check=True)
    binary = ROOT / 'build/native-app/neo98-cli'
    if not binary.exists():
        binary = native_app.build_fast('neo98_cli.cpp', 'neo98-cli')
    stage = out / 'drive'
    stage.mkdir(exist_ok=True)
    (stage / 'MODULES.EXE').write_bytes((out / 'MODULES.EXE').read_bytes())
    for number in range(48):
        (stage / f'M{number:02}.DLL').write_bytes((out / 'M.DLL').read_bytes())
    result = subprocess.run([str(binary), '--drive-c', str(win98_c.use_pristine()), '--drive', 'E=' + str(stage),
                             '--exe', 'E:\\MODULES.EXE', '--ram-drive', '--no-registry-file'],
                            env=env, input=b'', capture_output=True, timeout=60)
    (out / 'host.log').write_bytes(result.stdout + result.stderr)
    assert result.returncode == 7, (result.returncode, result.stderr.decode(errors='replace')[-600:])
    print('TOOLHELP MODULES PASS: three snapshots each list all 48 loaded libraries')


if __name__ == '__main__':
    main()
