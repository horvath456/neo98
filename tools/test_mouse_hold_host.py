#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A program that holds the mouse lets it go on Ctrl+Option.

tools/host_mouse_hold_fixture.c puts the pointer back to the middle of its
window every 20 ms (SetCursorPos), as a game does; the host moves the Mac
pointer with it, and the hand never leaves the window. The host, headless
(SDL's dummy driver), in its ordinary window and in the seamless view:

- the host notices the hold (`mouse hold=1`), and the title of the window
  and the menu Maschine name the keys;
- Ctrl+Option (an SDL key event, hostkeys=) lets the mouse go: the host no
  longer warps the Mac pointer, shows it, and the guest sees no mouse move;
- a press in the window takes the mouse back and reaches the guest;
- losing the activation lets go, and coming back does not take it again;
- Machine > "Maus freigeben" lets go;
- a pointer clip (ClipCursor) counts as a hold as well, and a mouse let go
  from it is the guest's again when the clip ends;
- when the program stops recentring, the hint goes.

    build/analysis-venv/bin/python tools/test_mouse_hold_host.py [--no-build]

Results in build/mouse-hold-host/results.json.
"""
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

IMPORTS = {'KERNEL32': {'ExitProcess': 4, 'GetModuleHandleA': 4},
           'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16, 'GetMessageA': 16,
                      'DispatchMessageA': 4, 'SetWindowTextA': 8, 'SetTimer': 16, 'SetCursorPos': 8,
                      'ClipCursor': 4},
           'GDI32': {'GetStockObject': 4}}


def fixture(out):
    environment = neo98.tool_environment()
    dlltool = shutil.which('llvm-dlltool', path=environment['PATH'])
    assert dlltool, 'llvm-dlltool not found'
    libraries = []
    for dll, names in IMPORTS.items():
        definition, library = out / (dll + '.def'), out / (dll + '.lib')
        definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' +
                              ''.join(f'{name}@{bytes_}\n' for name, bytes_ in names.items()))
        subprocess.run([dlltool, '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)],
                       env=environment, check=True)
        libraries.append(str(library))
    obj, exe = out / 'fixture.obj', out / 'HMHOLD.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-c', str(ROOT / 'tools/host_mouse_hold_fixture.c'),
                    '-o', str(obj)], env=environment, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0',
                    '/nodefaultlib', '/machine:x86', '/out:' + str(exe), str(obj), *libraries],
                   env=environment, check=True)
    return exe


# The hand moves beside the middle of the window (300,250), twice.
MOVES = ['0:motion=330,270', '0:idle=150', '0:motion=280,230', '0:idle=300']
STEPS = ['0:wait=|ready', '0:idle=1500', *MOVES,
         # Ctrl+Option: the guest sees no move afterwards.
         '0:hostkeys=1', '0:idle=300', '0:key=5a', '0:wait=|zero', *MOVES, '0:key=4d', '0:wait=|still',
         # A press takes the mouse back and reaches the guest.
         '0:press=300,250', '0:wait=|down 1', '0:release=300,250', '0:idle=200',
         '0:key=5a', '0:wait=|zero', *MOVES, '0:key=4d', '0:wait=|moved',
         # Another application comes to the front, and NEO98 comes back.
         '0:activation=0', '0:idle=300', '0:activation=1', '0:idle=300',
         '0:key=5a', '0:wait=|zero', *MOVES, '0:key=4d', '0:wait=|still',
         '0:press=300,250', '0:wait=|down 2', '0:release=300,250', '0:idle=200',
         # The menu.
         '0:menu=describe', '0:menu=mouse-release', '0:idle=300', '0:menu=describe',
         '0:press=300,250', '0:wait=|down 3', '0:release=300,250', '0:idle=200',
         # A pointer clip holds as well; let go, it ends with the clip.
         '0:key=43', '0:wait=|clipped', '0:idle=3500', '0:hostkeys=1', '0:idle=300',
         '0:key=55', '0:wait=|unclipped', '0:idle=500',
         # Nothing holds the mouse any more: Ctrl+Option does nothing.
         '0:hostkeys=1', '0:idle=300', '0:key=5a', '0:wait=|zero', *MOVES, '0:key=4d', '0:wait=|moved',
         '0:menu=quit']


def main():
    out = neo98.output_directory('mouse-hold-host')
    shutil.rmtree(out / 'drive-c', ignore_errors=True)
    exe = fixture(out)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    results = {}
    for name, extra in (('window', []), ('seamless', ['--seamless'])):
        drive = out / 'drive-c'
        subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
        command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
                   '--exe', str(exe), '--quiet', '--drive-c', str(drive), '--no-registry-file',
                   '--crash-report-dir', str(out / 'reports'), *extra]
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_POINTER_TRACE='1',
                   NEO98_HOST_INPUT=';'.join(STEPS), NEO98_HOST_SCREEN='1100x700', NEO98_HOST_DEADLINE='240')
        for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
            env.pop(key, None)
        log_path = out / f'host-{name}.log'
        with log_path.open('w') as log:
            code = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                  stdin=subprocess.DEVNULL, timeout=300).returncode
        shutil.rmtree(drive, ignore_errors=True)
        text = log_path.read_text(errors='replace')
        lines = [line for line in text.splitlines() if line.startswith('NEO98 HOST')]
        waits = re.findall(r'^NEO98 HOST input wait=(\S.*) (reached|timed out) after', text, re.M)

        def index(needle, start=0):
            return next((i for i in range(start, len(lines)) if needle in lines[i]), None)

        def warps(a, b):
            return sum(1 for line in lines[a:b] if line.startswith('NEO98 HOST POINTER warp '))

        keys = index('mouse released (ctrl+option)')
        press = index('mouse captured (press)', keys or 0)
        lost = index('mouse released (another application is active)', press or 0)
        press2 = index('mouse captured (press)', lost or 0)
        menu = index('mouse released (menu)', press2 or 0)
        press3 = index('mouse captured (press)', menu or 0)
        keys2 = index('mouse released (ctrl+option)', press3 or 0)
        clip_end = index('mouse captured (the pointer clip is gone)', keys2 or 0)
        order = [keys, press, lost, press2, menu, press3, keys2, clip_end]
        complete = all(v is not None for v in order)
        described = [i for i, line in enumerate(text.splitlines()) if 'mouse-release' in line and '>' in line]
        menu_lines = [text.splitlines()[i] for i in described]
        checks = {
            'host ended': code == 0,
            'every wait reached': bool(waits) and all(w[1] == 'reached' for w in waits),
            'the host sees the hold': index('mouse hold=1 released=0') is not None
                                      and (keys is None or index('mouse hold=1 released=0') < keys),
            'the title names the keys': any('title ' in line and 'Ctrl+Option releases the mouse' in line for line in lines),
            'released and captured in order': complete and order == sorted(order),
            'the pointer was warped before Ctrl+Option': keys is not None and warps(0, keys) >= 2,
            'no warp while the mouse is let go': complete and warps(keys, press) == 0 and warps(lost, press2) == 0
                                                 and warps(menu, press3) == 0,
            'warps again after the press': complete and warps(press, lost) >= 2,
            'the Mac pointer shows when let go': keys is not None and (
                name == 'seamless' or 'POINTER show=1' in ' '.join(lines[max(keys - 2, 0):keys + 3])),
            'the title says a press takes it back': any('title ' in line and 'click the window to capture it' in line for line in lines),
            'the menu item is enabled while held, and says free afterwards':
                len(menu_lines) >= 2 and 'Release mouse (Ctrl+Option)' in menu_lines[0] and '(disabled)' not in menu_lines[0]
                and 'Mouse released' in menu_lines[-1],
            'the hint goes when nothing holds the mouse': bool(lines) and index('mouse hold=0 released=0', clip_end or 0) is not None
                                                          and index('mouse released', (clip_end or 0) + 1) is None,
        }
        results[name] = checks
        for check, okay in checks.items():
            print(f"{'ok  ' if okay else 'FAIL'} {name}: {check}")
    (out / 'results.json').write_text(json.dumps(results, indent=1))
    if not all(all(c.values()) for c in results.values()):
        print(f'MOUSE HOLD FAIL; see {out}')
        return 1
    print('MOUSE HOLD PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
