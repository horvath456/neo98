#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A program calls the window procedure of a system class as a function.

GetWindowLong(GWL_WNDPROC), GetClassLong(GCL_WNDPROC) and GetClassInfo of
Win98 answer an address a program may call. The setup of Plus! 98 subclasses
its wizard and forwards every message with `call [old]`; here the address is a
token of the port, and the call ended in "SETUP caused an invalid page fault
in module <unknown> ".

The host runs tools/direct_wndproc_fixture.c from a clone of the pristine drive
C: under SDL's dummy video driver. The fixture calls the procedures of the
dialog class, BUTTON, EDIT, STATIC, LISTBOX and COMBOBOX that way and writes
what held to C:\\DIRECTWP.TXT.

    build/analysis-venv/bin/python tools/test_direct_wndproc_host.py [--no-build]

Results in build/direct-wndproc-host/results.json.
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
                        'SetFilePointer': 16, 'ExitProcess': 4},
           'USER32': {'CreateDialogIndirectParamA': 20, 'CreateWindowExA': 48, 'GetDlgItem': 8, 'GetWindowLongA': 8,
                      'SetWindowLongA': 12, 'GetClassLongA': 8, 'GetClassInfoA': 12, 'SendMessageA': 16,
                      'GetWindowTextA': 12, 'DestroyWindow': 4}}


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
    obj, exe = out / 'fixture.obj', out / 'DIRECTWP.EXE'
    run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
         '-c', ROOT / 'tools/direct_wndproc_fixture.c', '-o', obj])
    run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0', '/nodefaultlib',
         '/machine:x86', '/out:' + str(exe), obj, *libs])
    return exe


def run_once(binary, drive_c, out, backend):
    record = drive_c / 'DIRECTWP.TXT'
    record.unlink(missing_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='120',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'), NEO98_DOCK_HELPERS='off')
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_KEYMAP', 'NEO98_HOST_INPUT', 'NEO98_CPU'):
        env.pop(key, None)
    # The host keeps the last frame of an ended program until its window
    # closes; the menu script closes it.
    env['NEO98_HOST_MENU'] = '15:quit'
    if backend != 'fex':
        env['NEO98_CPU'] = backend
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
               '--exe', 'C:\\DIRECTWP.EXE', '--drive-c', str(drive_c), '--crash-report-dir', str(out / 'reports')]
    log_path = out / f'host-{backend}.log'
    with log_path.open('w') as log:
        try:
            code = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                  stdin=subprocess.DEVNULL, timeout=300).returncode
        except subprocess.TimeoutExpired:
            code = 'timeout'
    written = record.read_text(errors='replace').split() if record.exists() else []
    text = log_path.read_text(errors='replace')
    return {'exit': code, 'written': ' '.join(written), 'fault': 'FAULT BOX' in text or 'EXCEPTION c0000005' in text}


def main():
    out = neo98.output_directory('direct-wndproc-host')
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
    subprocess.run(['cp', '-c', '-R', str(pristine) + '/', str(drive_c)], check=True)
    (drive_c / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    shutil.copyfile(exe, drive_c / 'DIRECTWP.EXE')
    expected = 'dialog button edit static lists done'
    runs = {backend: run_once(binary, drive_c, out, backend) for backend in ('fex', 'unicorn')}
    checks = {}
    for backend, result in runs.items():
        checks[f'{backend}: every direct call answered like CallWindowProc'] = result['written'] == expected
        checks[f'{backend}: no access violation'] = not result['fault']
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
    print(f'direct window procedure: {len(checks)} checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
