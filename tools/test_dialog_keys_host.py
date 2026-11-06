#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Keys a dialog leaves to its controls, from the keyboard in the host.

The guest tools/dialog_keys_fixture.c opens a dialog with two multiline edits
(the second with ES_WANTRETURN), an automatic and a plain check box, OK and
Cancel. The keys:

- a multiline EDIT answers WM_GETDLGCODE with DLGC_WANTALLKEYS (8Dh,
  USER) and handles TAB, ENTER and ESC itself: TAB and SHIFT+TAB send
  WM_NEXTDLGCTL, CTRL+TAB types a TAB character, ENTER without ES_WANTRETURN focuses the default button and posts
  it the ENTER, with ES_WANTRETURN it is a line break, ESC posts
  WM_CLOSE to the dialog. Outside a dialog a TAB character is a TAB
  in a multiline EDIT and nothing (a beep) in a single-line one.
- '+', '-' and '=' on a focused check box : the
  automatic one follows them, the plain one only reports BN_CLICKED when the
  check would change.

    build/analysis-venv/bin/python tools/test_dialog_keys_host.py [--no-build]

Results in build/dialog-keys-host/results.json.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

IMPORTS = {
    'KERNEL32': {'GetModuleHandleA': 4, 'CreateFileA': 28, 'WriteFile': 20, 'CloseHandle': 4, 'ExitProcess': 4},
    'USER32': {'DialogBoxIndirectParamA': 20, 'CreateWindowExA': 48, 'DestroyWindow': 4, 'GetDlgItem': 8,
               'GetWindowTextA': 12, 'EndDialog': 8, 'SendMessageA': 16},
}
# The dialog "Keys" ends with OK from ENTER in the first edit, "Keys 2" with
# ESC; the last line is the TAB character outside a dialog.
EXPECTED = ('end=1 e100=[a] e101=[b\\tc\\r\\nd] auto=1 plain=0 clicks=1\n'
            'end=2 e100=[x] e101=[] auto=0 plain=0 clicks=0\n'
            'multiline=[x\\ty] single=[xy]\n')


def build_fixture(out):
    env = neo98.tool_environment()
    libs = []

    def run(args):
        subprocess.run(list(map(str, args)), env=env, check=True)
    for dll, entries in IMPORTS.items():
        definition, lib = out / f'{dll}.def', out / f'{dll}.lib'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' + ''.join(f'{n}@{b}\n' for n, b in entries.items()))
        run(['llvm-dlltool', '-m', 'i386', '-k', '-d', definition, '-l', lib])
        libs.append(lib)
    obj, exe = out / 'fixture.obj', out / 'DLGKEYS.EXE'
    run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
         '-c', ROOT / 'tools/dialog_keys_fixture.c', '-o', obj])
    run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0', '/nodefaultlib',
         '/machine:x86', '/out:' + str(exe), obj, *libs])
    return exe


def shifted(step, modifier, vk):
    return [f'{step}:keydown={modifier}', f'{step}:key={vk}', f'{step}:keyup={modifier}']


def main():
    out = neo98.output_directory('dialog-keys-host')
    fixture = out / 'fixture'
    fixture.mkdir(parents=True, exist_ok=True)
    exe = build_fixture(fixture)
    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    shutil.copyfile(exe, drive_c / 'DLGKEYS.EXE')
    registry = out / 'session.registry'
    shutil.copyfile(win98_c.journal(win98_c.FOLDER), registry)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    steps = ['0:wait=#32770|Keys', '0:idle=1000',
             # First edit: a, TAB moves on without a character.
             '1:type=a', '1:key=09', '1:idle=300',
             # Second edit (ES_WANTRETURN): CTRL+TAB is a TAB, ENTER a break.
             '2:type=b', *shifted(2, '11', '09'), '2:type=c', '2:key=0d', '2:type=d', '2:idle=300',
             # TAB to the automatic check box: +, -, = end checked.
             '3:key=09', '3:idle=300', '3:type=+', '3:type=-', '3:type==', '3:idle=300',
             # The plain check box: - on the clear box is silent, + reports
             # BN_CLICKED and leaves the check to the dialog, which sets none.
             '4:key=09', '4:idle=300', '4:type=-', '4:type=+', '4:idle=300',
             # SHIFT+TAB back through the check box and both edits, ENTER in
             # the first one presses OK.
             *shifted(5, '10', '09'), *shifted(5, '10', '09'), '5:idle=300', *shifted(5, '10', '09'),
             '5:idle=300', '5:key=0d',
             '6:wait=#32770|Keys 2', '6:idle=1000', '6:type=x', '6:key=1b']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_KEYMAP'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
               '--exe', 'C:\\DLGKEYS.EXE', '--quiet', '--drive-c', str(drive_c), '--registry', str(registry),
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    # The host keeps the last frame on screen after the guest ended; this
    # test ends the host it started once the log says so.
    ended = ('NEO98 HOST exited', 'NEO98 HOST failed')
    with log_path.open('w') as log:
        host = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL)
        deadline = time.monotonic() + 900
        while host.poll() is None and time.monotonic() < deadline:
            if any(mark in log_path.read_text(errors='replace') for mark in ended):
                break
            time.sleep(0.5)
        if host.poll() is None:
            host.terminate()
            host.wait(timeout=30)
    text = log_path.read_text(errors='replace')
    record = next((p for p in drive_c.iterdir() if p.name.upper() == 'DLGKEYS.TXT'), None)
    got = record.read_text(errors='replace') if record else None
    shutil.rmtree(drive_c, ignore_errors=True)
    checks = {
        'dialog Keys opened': 'input wait=#32770|Keys reached' in text,
        'dialog Keys 2 opened': 'input wait=#32770|Keys 2 reached' in text,
        'the record matches Win98': got == EXPECTED,
        'the guest ended normally': 'NEO98 HOST exited code=0' in text,
    }
    (out / 'results.json').write_text(json.dumps({'steps': steps, 'checks': checks, 'record': got},
                                                indent=2, ensure_ascii=False) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'record:\n{got}expected:\n{EXPECTED}')
        print(f'DIALOG KEYS HOST FAIL: see {log_path}')
        return 1
    print(f'DIALOG KEYS HOST PASS: {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
