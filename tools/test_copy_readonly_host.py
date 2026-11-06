#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""CopyFile of a read-only file fills the read-only copy (Office XP System Files Update)."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98
OUT = ROOT / 'build/copy-readonly'
IMPORTS = {
    'KERNEL32': {'CreateFileA': 28, 'WriteFile': 20, 'ReadFile': 20, 'CloseHandle': 4, 'CopyFileA': 12,
                 'GetFileAttributesA': 4, 'SetFileAttributesA': 8, 'DeleteFileA': 4, 'ExitProcess': 4},
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
    obj, exe = OUT / 'fixture.obj', OUT / 'COPYRO.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding',
                    '-fno-stack-protector', '-c', str(ROOT / 'tools/copy_readonly_fixture.c'),
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
                             '--system-tree', '--exe', 'E:\\COPYRO.EXE', '--drive-readonly',
                             'E=' + str(OUT), '--drive-c', str(drive), '--no-registry-file', '--quiet'],
                            cwd=ROOT, env=runtime, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, timeout=60)
    (OUT / 'host.log').write_text(result.stdout)
    assert result.returncode == 0 and 'exited code=0' in result.stdout, result.stdout[-2500:]
    print('COPY READONLY PASS')

if __name__ == '__main__':
    main()
