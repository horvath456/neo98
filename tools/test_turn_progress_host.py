#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Two busy processes get on, however long the change between them takes.

On Linux x64 the guests share the 32-bit addresses of the host process, and
the first run of a turn brings its guest there. When that takes longer than a
turn lasts (4 ms), the turn was over at its first API gate, before the call
was served: with two processes that both have work, every turn of both ended
there, and Windows stood still. It happened on a busy machine once one of the
two held several hundred 16-bit segments.

tools/turn_progress_fixture.c starts itself a second time; both processes
call Windows in a loop for a second and a half. The host runs with
NEO98_DIRECT_SWITCH_DELAY=6000, which makes every change of processor take
6 ms more, whatever the machine does otherwise. Both processes have to come
to their end with their rounds counted.

    build/analysis-venv/bin/python tools/test_turn_progress_host.py [host binary]
"""
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98
IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'GetModuleFileNameA': 12, 'CreateProcessA': 40, 'CreateFileMappingA': 24,
                 'OpenFileMappingA': 12, 'MapViewOfFile': 20, 'WaitForSingleObject': 8, 'GetExitCodeProcess': 8,
                 'GetTickCount': 0, 'GetFileAttributesA': 4},
}


def run(binary, out, drive):
    runtime = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   NEO98_HOST_INPUT='', NEO98_HOST_MENU='12:quit', NEO98_HOST_DEADLINE='60',
                   NEO98_DIRECT_SWITCH_DELAY='6000', NEO98_NATIVE_STATS='1')
    runtime.pop('NEO98_CPU', None)
    try:
        result = subprocess.run([str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                                 '--system-tree', '--exe', 'E:\\TURNS.EXE', '--drive-readonly',
                                 'E=' + str(out / 'e'), '--drive-c', str(drive), '--no-registry-file', '--quiet',
                                 '--crash-report-dir', str(out / 'reports')],
                                cwd=ROOT, env=runtime, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, errors='replace', timeout=180)
        text, code = result.stdout, result.returncode
    except subprocess.TimeoutExpired as expired:
        text, code = (expired.stdout or b'').decode(errors='replace') + '\nthe host did not end\n', 1
    (out / 'host.log').write_text(text)
    changes = sum(int(word.split('=')[1]) for line in text.splitlines() if line.startswith('DIRECT SWITCH')
                  for word in line.split() if word.startswith('switches='))
    okay = code == 0 and 'exited code=0' in text
    if not okay:
        print('\n'.join(line for line in text.splitlines()[-400:]
                        if 'exited' in line or 'EXCEPTION' in line or 'Fail' in line or 'did not end' in line)[-1500:])
    return okay, changes


def main():
    if sys.platform != 'linux' or platform.machine() not in ('x86_64', 'AMD64'):
        print('SKIP: only the native CPU of Linux x86_64 changes its guests at shared addresses')
        return 77
    out = neo98.output_directory('turn-progress')
    (out / 'e').mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    dlltool = shutil.which('llvm-dlltool', path=env['PATH'])
    assert dlltool
    libraries = []
    for dll, names in IMPORTS.items():
        definition, library = out / f'{dll}.def', out / f'{dll}.lib'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' +
                              ''.join(f'{name}@{size}\n' for name, size in names.items()))
        subprocess.run([dlltool, '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)],
                       env=env, check=True)
        libraries.append(str(library))
    obj, exe = out / 'fixture.obj', out / 'e' / 'TURNS.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding',
                    '-fno-stack-protector', '-fno-builtin', '-c', str(ROOT / 'tools/turn_progress_fixture.c'),
                    '-o', str(obj)], env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/nodefaultlib',
                    '/machine:x86', '/timestamp:0', '/out:' + str(exe), str(obj), *libraries],
                   env=env, check=True)
    binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build/native-app/neo98-host'
    import win98_c
    win98_c.use_pristine()
    drive = out / 'drive-c'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', '--reflink=auto', '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
    okay, changes = run(binary, out, drive)
    shutil.rmtree(drive, ignore_errors=True)
    checks = {'two busy processes come to their end with a change of 6 ms between them': okay,
              f'the processes took turns ({changes} changes of processor)': changes >= 20}
    for name, passed in checks.items():
        print(f"{'ok  ' if passed else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'TURN PROGRESS FAIL; see {out}')
        return 1
    print(f'TURN PROGRESS PASS: {len(checks)} checks')
    return 0


if __name__ == '__main__':
    sys.exit(main())
