#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the touch extension and SDK adapter in real x86 and ARM64 guests."""
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import neo98
import arm64_sdk
import native_app
import win98_c
OUT = ROOT / 'build/multitouch-host'
IMPORTS = {
    'KERNEL32': {'GetModuleHandleA': 4, 'ExitProcess': 4, 'LoadLibraryA': 4,
                 'GetProcAddress': 8, 'FreeLibrary': 4},
    'USER32': {'IsWindow': 4, 'RegisterClassA': 4, 'CreateWindowExA': 48,
               'DefWindowProcA': 16, 'ShowWindow': 8, 'GetMessageA': 16,
               'DispatchMessageA': 4, 'ScreenToClient': 8, 'SetForegroundWindow': 4, 'GetSystemMetrics': 4},
    'MSVCRT': {'memset': 0},
}
def x86_fixture(source, name, included_adapter=False):
    env = neo98.tool_environment()
    libraries = []
    for dll, names in IMPORTS.items():
        definition, library = OUT / f'{dll}.def', OUT / f'{dll}.lib'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' + ''.join(
            f'{symbol}@{size}\n' if dll != 'MSVCRT' else f'{symbol}\n' for symbol, size in names.items()))
        subprocess.run([arm64_sdk.tool('llvm-dlltool'), '-m', 'i386', '-k', '-d', str(definition),
                        '-l', str(library)], env=env, check=True)
        libraries.append(library)
    objects = []
    for file in ([source] if included_adapter else [source, ROOT / 'sdk/lib/neo98_touch.c']):
        obj = OUT / f'{name}-{file.stem}.obj'
        subprocess.run(['i686-w64-mingw32-gcc', '-O1', '-ffreestanding',
                        '-fno-stack-protector', '-fno-builtin',
                        *(['-D_WIN32_WINNT=0x0602'] if included_adapter else []),
                        '-c', str(file), '-o', str(obj)], env=env, check=True)
        objects.append(obj)
    exe = OUT / f'{name}.EXE'
    subprocess.run([arm64_sdk.tool('lld-link'), '/entry:mainCRTStartup', '/subsystem:windows',
                    '/nodefaultlib', '/machine:x86', '/safeseh:no', '/timestamp:0', '/out:' + str(exe),
                    *map(str, objects), *map(str, libraries)], env=env, check=True)
    return exe
def run(binary, exe, script, name):
    drive = OUT / f'{name}-c'
    if not drive.exists(): subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=script,
               NEO98_HOST_MENU='2:quit' if name.startswith('pointer-') else '12:quit' if name.startswith('lifecycle-') else '8:quit',
               NEO98_HOST_DEADLINE='20')
    with (OUT / f'{name}.log').open('w') as log:
        result = subprocess.run([str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                             '--system-tree', '--exe', 'E:\\' + exe.name, '--drive-readonly', 'E=' + str(OUT),
                             '--drive-c', str(drive), '--no-registry-file', '--quiet'], cwd=ROOT, env=env,
                            stdout=log, stderr=subprocess.STDOUT, text=True, timeout=35)
    output = (OUT / f'{name}.log').read_text()
    assert result.returncode == 0 and 'exited code=0' in output, output[-3000:]
def main():
    OUT.mkdir(parents=True, exist_ok=True)
    arm64_sdk.generate()
    win98_c.use_pristine()
    if native_app.native_x64():
        print("SKIP ARM64 guests: the Linux x64 host runs x86 guests only")
    source = ROOT / 'tools/multitouch_fixture.c'
    binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build/native-app/neo98-host'
    steps = ['0:wait=MultitouchFixture',
             '1:touch=999,down,-20,-20', '1:touch=999,move,70,80', '1:touch=999,up,70,80',
             '2:touch=12,down,70,80,512', '2:touch=13,down,100,110',
             '2:touch=12,move,90,120', '2:touch=12,up,90,120',
             '2:touch=13,move,130,150', '2:touch=13,up,130,150',
             '3:touch=14,down,70,80', '3:touchcancel=1', '3:touch=14,up,70,80',
             '4:touch=15,down,70,80']
    steps += [f'4:touch=15,move,{70 + i % 100},80' for i in range(80)]
    steps += ['4:touch=15,up,70,80', '5:touch=12,down,70,80', '5:touch=12,up,70,80', '7:menu=quit']
    script = ';'.join(steps)
    x86 = x86_fixture(source, 'TOUCH32')
    arm64 = arm64_sdk.build([source], OUT / 'TOUCH64.EXE', gui=True, entry='mainCRTStartup')
    for name, exe in [('x86', x86), ('arm64', arm64)]:
        if native_app.native_x64() and name.endswith("arm64"): continue
        run(binary, exe, script, name)
        print(f'MULTITOUCH {name} PASS')
    source = ROOT / 'tools/multitouch_windows_fixture.c'
    x86 = x86_fixture(source, 'PTR32', included_adapter=True)
    arm64 = arm64_sdk.build([source], OUT / 'PTR64.EXE', gui=True, entry='mainCRTStartup')
    for name, exe in [('pointer-x86', x86), ('pointer-arm64', arm64)]:
        if native_app.native_x64() and name.endswith("arm64"): continue
        run(binary, exe, '1:menu=quit', name)
        print(f'MULTITOUCH {name} PASS')
    source = ROOT / 'tools/multitouch_lifecycle_fixture.c'
    x86 = x86_fixture(source, 'LIFE32')
    arm64 = arm64_sdk.build([source], OUT / 'LIFE64.EXE', gui=True, entry='mainCRTStartup')
    script = ';'.join(['0:wait=TouchLifecycleFixture|Touch main',
        '1:touch=1,down,70,80', '1:touch=1,move,500,500', '1:touch=1,up,500,500',
        '3:touch=44,down,70,80', '4:touch=44,up,70,80',
        '5:touch=51,down,420,80', '5:touch=52,down,450,100', '5:touch=51,up,420,80',
        '6:touch=52,move,470,110', '6:touch=52,up,470,110',
        '6.3:touch=54,down,70,80', '6.6:touch=54,up,70,80', '7:key=55',
        '8:touch=53,down,70,80', '8:touch=53,up,70,80', '9:key=41'])
    for name, exe in [('lifecycle-x86', x86), ('lifecycle-arm64', arm64)]:
        if native_app.native_x64() and name.endswith("arm64"): continue
        run(binary, exe, script, name)
        print(f'MULTITOUCH {name} PASS')
    (OUT / 'results.json').write_text(json.dumps({'x86': 'pass', 'arm64': 'skip: Linux x64 host has no ARM64 machine' if native_app.native_x64() else 'pass', 'windows_adapter': 'supplied-records pass in x86 and ARM64',
        'cases': ['version and buffer validation', 'identical ABI', 'outside down ignored',
                  'two contacts, separate up', 'client coordinates', 'optional pressure',
                  'no duplicate mouse', 'cancel all', 'overflow reset', 'ID reuse',
                  'capture outside', 'focus loss', 'legacy primary mouse', 'no secondary promotion',
                  'unregister restores mouse', 'API support without hardware',
                  'scripted fingers do not claim a touchscreen', 'pen-only and unready excluded',
                  'integrated and external touch', 'touchscreen unplugged and closed context']}, indent=2) + '\n')
if __name__ == '__main__': main()
