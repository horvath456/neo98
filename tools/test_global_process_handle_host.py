#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A global handle of a process answers in every process.

SHELL32 keeps a list in shared memory with one entry for each process that
runs its change notification thread: the thread and
ConvertToGlobalHandle(OpenProcess(SYNCHRONIZE, FALSE, own id)).
Whoever sends a notification asks every handle with WaitForSingleObject and
drops the entries that do not answer WAIT_TIMEOUT; the list is
freed when it is empty. ConvertToGlobalHandle answered zero for a process, so
the first notification emptied the list, and Internet Explorer read the null
of the freed list when it closed (page fault in SHELL32 at ).

tools/global_process_handle_fixture.c starts itself a second time. Both
processes make a global handle of themselves and of an event; each asks the
other's handles, the two events have two values, and the child's end signals
the child's handle in the parent. The exit code names the first step that
failed.

    build/analysis-venv/bin/python tools/test_global_process_handle_host.py [host binary]
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98
IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'GetModuleFileNameA': 12, 'CreateProcessA': 40, 'CreateFileMappingA': 24,
                 'OpenFileMappingA': 12, 'MapViewOfFile': 20, 'CreateEventA': 16, 'SetEvent': 4,
                 'WaitForSingleObject': 8, 'GetExitCodeProcess': 8, 'GetCurrentProcessId': 0, 'OpenProcess': 12,
                 'ConvertToGlobalHandle': 4, 'CloseHandle': 4},
}


def run(binary, out, drive, engine):
    runtime = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   NEO98_HOST_INPUT='', NEO98_HOST_MENU='12:quit', NEO98_HOST_DEADLINE='40')
    runtime.pop('NEO98_CPU', None)
    if engine:
        runtime['NEO98_CPU'] = engine
    result = subprocess.run([str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                             '--system-tree', '--exe', 'E:\\GLOBALH.EXE', '--drive-readonly',
                             'E=' + str(out / 'e'), '--drive-c', str(drive), '--no-registry-file', '--quiet',
                             '--crash-report-dir', str(out / 'reports')],
                            cwd=ROOT, env=runtime, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, errors='replace', timeout=120)
    (out / f'host-{engine or "default"}.log').write_text(result.stdout)
    okay = result.returncode == 0 and 'exited code=0' in result.stdout
    if not okay:
        print('\n'.join(line for line in result.stdout.splitlines()[-400:]
                        if 'exited' in line or 'EXCEPTION' in line or 'Fail' in line)[-1500:])
    return okay


def main():
    out = neo98.output_directory('global-process-handle')
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
    obj, exe = out / 'fixture.obj', out / 'e' / 'GLOBALH.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding',
                    '-fno-stack-protector', '-fno-builtin', '-c', str(ROOT / 'tools/global_process_handle_fixture.c'),
                    '-o', str(obj)], env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/nodefaultlib',
                    '/machine:x86', '/timestamp:0', '/out:' + str(exe), str(obj), *libraries],
                   env=env, check=True)
    binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build/native-app/neo98-host'
    import win98_c
    win98_c.use_pristine()
    drive = out / 'drive-c'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', '-c', '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
    checks = {f'two processes ask the global handles of each other ({engine or "default engine"})': run(binary, out, drive, engine)
              for engine in ('',)}
    shutil.rmtree(drive, ignore_errors=True)
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'GLOBAL PROCESS HANDLE FAIL; see {out}')
        return 1
    print(f'GLOBAL PROCESS HANDLE PASS: {len(checks)} checks')
    return 0


if __name__ == '__main__':
    sys.exit(main())
