#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Transparent statics under a Windows Installer style dialog."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98
OUT = ROOT / 'build/transparent-static'
IMPORTS = {
    'KERNEL32': {'GetModuleHandleA': 4, 'ExitProcess': 4, 'Sleep': 4, 'OutputDebugStringA': 4},
    'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'CallWindowProcA': 20, 'DefWindowProcA': 16,
               'SetWindowLongA': 12, 'SetFocus': 4, 'GetWindowLongA': 8, 'SetClassLongA': 12, 'SetWindowTextA': 8, 'ShowWindow': 8,
               'InvalidateRect': 12, 'UpdateWindow': 4, 'PeekMessageA': 20, 'DispatchMessageA': 4,
               'GetDC': 4, 'ReleaseDC': 8, 'GetSysColor': 4, 'SendMessageA': 16, 'IsWindowVisible': 4,
               'GetWindowTextA': 12, 'BeginPaint': 8, 'EndPaint': 8, 'FillRect': 12},
    'GDI32': {'GetPixel': 12, 'SetBkMode': 8, 'GetStockObject': 4, 'CreateSolidBrush': 4, 'TextOutA': 20},
}

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    dlltool = shutil.which('llvm-dlltool', path=env['PATH'])
    assert dlltool
    libraries = []
    for dll, names in IMPORTS.items():
        definition, library = OUT / f'{dll}.def', OUT / f'{dll}.lib'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' +
                              ''.join(f'{name}@{size}\n' for name, size in names.items()))
        subprocess.run([dlltool, '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)],
                       env=env, check=True)
        libraries.append(str(library))
    obj, exe = OUT / 'fixture.obj', OUT / 'TSTATIC.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding',
                    '-fno-stack-protector', '-c', str(ROOT / 'tools/transparent_static_fixture.c'),
                    '-o', str(obj)], env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/nodefaultlib',
                    '/machine:x86', '/timestamp:0', '/out:' + str(exe), str(obj), *libraries],
                   env=env, check=True)
    binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build/native-app/neo98-host'
    import win98_c
    win98_c.use_pristine()
    drive = OUT / 'drive-c'
    if not drive.exists():
        subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
    runtime = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   NEO98_HOST_INPUT='', NEO98_HOST_MENU='25:quit', NEO98_HOST_DEADLINE='30')
    result = subprocess.run([str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                             '--system-tree', '--exe', 'E:\\TSTATIC.EXE', '--drive-readonly',
                             'E=' + str(OUT), '--drive-c', str(drive), '--no-registry-file', '--quiet'],
                            cwd=ROOT, env=runtime, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, timeout=60)
    (OUT / 'host.log').write_text(result.stdout)
    assert result.returncode == 0 and 'exited code=0' in result.stdout, result.stdout[-2500:]
    print('TRANSPARENT STATIC PASS')

if __name__ == '__main__':
    main()
