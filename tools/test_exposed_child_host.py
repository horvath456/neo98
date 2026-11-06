#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A parent without WS_CLIPCHILDREN paints under a child that paints nothing.

The autorun window of Plus! 98 draws its picture itself; its two buttons
answer WM_ERASEBKGND and WM_PAINT without drawing. After a change of the
desktop size their rectangles stayed black: the port made only the child
invalid where Win98 makes the parent invalid as well, because the child's area
belongs to the visible region of a parent without WS_CLIPCHILDREN.

The host runs tools/exposed_child_fixture.c from a clone of the pristine drive
C: under SDL's dummy video driver: a red parent, a child that draws nothing, a
blue window that covers both and goes, then a change of the desktop to
1024x768. The pixel in the middle of the child must be red after each step.

    build/analysis-venv/bin/python tools/test_exposed_child_host.py [--no-build]

Results in build/exposed-child-host/results.json.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
import win98_c  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402

IMPORTS = {'KERNEL32': {'GetModuleHandleA': 4, 'CreateFileA': 28, 'WriteFile': 20, 'CloseHandle': 4,
                        'SetFilePointer': 16, 'ExitProcess': 4, 'Sleep': 4},
           'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16, 'BeginPaint': 8, 'EndPaint': 8,
                      'FillRect': 12, 'PeekMessageA': 20, 'DispatchMessageA': 4, 'DestroyWindow': 4, 'GetDC': 4,
                      'ReleaseDC': 8, 'GetWindowRect': 8, 'GetClientRect': 8, 'ChangeDisplaySettingsA': 8,
                      'GetClassLongA': 8},
           'GDI32': {'CreateSolidBrush': 4, 'GetPixel': 12}}


def build_fixture(out):
    env = neo98.tool_environment()
    out.mkdir(parents=True, exist_ok=True)

    def run(args):
        subprocess.run(list(map(str, args)), env=env, check=True)
    libs = []
    for dll, entries in IMPORTS.items():
        definition, lib = out / f'{dll}.def', out / f'{dll}.lib'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' + ''.join(f'{n}@{b}\n' for n, b in entries.items()))
        run(['llvm-dlltool', '-m', 'i386', '-k', '-d', definition, '-l', lib])
        libs.append(lib)
    obj, exe = out / 'fixture.obj', out / 'EXPOSED.EXE'
    run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
         '-c', ROOT / 'tools/exposed_child_fixture.c', '-o', obj])
    run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0', '/nodefaultlib',
         '/machine:x86', '/out:' + str(exe), obj, *libs])
    return exe


# The engines of macOS; Linux x64 runs x86 code on the native CPU only.
BACKENDS = ('fex', 'unicorn') if sys.platform == 'darwin' else ('native',)


def run_once(binary, drive_c, out, backend):
    record = drive_c / 'EXPOSED.TXT'
    record.unlink(missing_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='120',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'), NEO98_DOCK_HELPERS='off')
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_KEYMAP', 'NEO98_HOST_INPUT', 'NEO98_CPU'):
        env.pop(key, None)
    # The host keeps the last frame of an ended program until its window
    # closes; the menu script closes it.
    env['NEO98_HOST_MENU'] = '20:quit'
    if backend not in ('fex', 'native'):
        env['NEO98_CPU'] = backend
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
               '--exe', 'C:\\EXPOSED.EXE', '--drive-c', str(drive_c), '--crash-report-dir', str(out / 'reports')]
    log_path = out / f'host-{backend}.log'
    with log_path.open('w') as log:
        process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL)
        try:
            code = process.wait(timeout=300)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=20)
            code = 'timeout'
    written = record.read_text(errors='replace').split() if record.exists() else []
    text = log_path.read_text(errors='replace')
    return {'exit': code, 'written': ' '.join(written), 'fault': 'FAULT BOX' in text or 'EXCEPTION c0000005' in text}


def main():
    out = neo98.output_directory('exposed-child-host')
    for name in ('drive-c', 'reports', 'fixture', 'settings', 'logs'):
        shutil.rmtree(out / name, ignore_errors=True)
    for name in ('drive-c.registry', 'drive-c.host-lock'):
        (out / name).unlink(missing_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    pristine = win98_c.use_pristine()
    exe = build_fixture(out / 'fixture')
    drive_c = out / 'drive-c'
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(pristine) + '/', str(drive_c)], check=True)
    (drive_c / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    shutil.copyfile(exe, drive_c / 'EXPOSED.EXE')
    expected = 'start red covered blue uncovered red mode changed mode red done'
    runs = {backend: run_once(binary, drive_c, out, backend) for backend in BACKENDS}
    checks = {}
    for backend, result in runs.items():
        checks[f'{backend}: host ended normally without a guest fault'] = result['exit'] == 0 and not result['fault']
        checks[f'{backend}: the parent painted under the child after every step'] = result['written'] == expected
    for name in ('drive-c', 'reports', 'settings', 'logs'):
        shutil.rmtree(out / name, ignore_errors=True)
    for name in ('drive-c.registry', 'drive-c.host-lock'):
        (out / name).unlink(missing_ok=True)
    (out / 'results.json').write_text(json.dumps({'checks': checks, 'runs': runs}, indent=2) + '\n')
    failed = [name for name, okay in checks.items() if not okay]
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if failed:
        print(f'{len(failed)} of {len(checks)} checks failed; {runs}')
        return 1
    print(f'exposed child: {len(checks)} checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
