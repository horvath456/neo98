#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The bits saved under a CS_SAVEBITS window come back when it goes.

MahJong 2000 draws its board directly through GetDC; its WM_PAINT only erases.
Its "Game Results" dialog has CS_SAVEBITS. USER dropped the saved bits as soon
as any window underneath drew anywhere, and it never restored what lay under
the controls of the dialog. So the board area became invalid when the dialog
went, the paint erased it, and a green rectangle stood where the dialog had
been. Windows 98 checks a save against the bounds GDI collected for the
drawing DC (USER, GetBoundsRect 8001h) and puts the bits back.

The fixture (tools/host_savebits_fixture.c) does the same without the game.
Checks, on screenshots taken without a screen:

1. picture, dialog, a paint of the window away from the dialog, dialog
   hidden: the picture stands again where the dialog was, also under its
   button and its caption; the painted square is erased.
2. dialog shown again, the window draws under it, dialog hidden: that spot is
   invalid and erased, not restored from the stale save.
3. the same as 1 with the dialog destroyed instead of hidden.
4. hide a saved window while another saved window covers it, then destroy
   the upper window: its snapshot must not restore the hidden window.
5. close the upper one first: the lower backup must preserve the board
   drawn through GetDC, which WM_PAINT cannot reproduce.

    build/analysis-venv/bin/python tools/test_savebits_restore_host.py [--no-build]
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
           'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16,
                      'GetMessageA': 16, 'TranslateMessage': 4, 'DispatchMessageA': 4,
                      'BeginPaint': 8, 'EndPaint': 8, 'FillRect': 12, 'GetClientRect': 8,
                      'SetWindowTextA': 8, 'GetDC': 4, 'ReleaseDC': 8, 'InvalidateRect': 12,
                      'ShowWindow': 8, 'DestroyWindow': 4, 'SetWindowPos': 28,
                      'SetFocus': 4, 'SetForegroundWindow': 4},
           'GDI32': {'CreateSolidBrush': 4, 'SelectObject': 8, 'PatBlt': 24}}
RED, GREEN, BLUE = 'ff0000', '00ff00', '0000ff'


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
    obj, exe = out / 'fixture.obj', out / 'HSAVEB.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-c', str(ROOT / 'tools/host_savebits_fixture.c'),
                    '-o', str(obj)], env=environment, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0',
                    '/nodefaultlib', '/machine:x86', '/out:' + str(exe), str(obj), *libraries],
                   env=environment, check=True)
    return exe


def image(path):
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    width = int(size.split()[0])

    def at(x, y):
        i = (y * width + x) * 3
        return body[i:i + 3].hex()
    return at


def main():
    out = neo98.output_directory('savebits-restore-host')
    for name in ('drive-c', 'reports'):
        shutil.rmtree(out / name, ignore_errors=True)
    for path in out.glob('*.ppm'):
        path.unlink()
    exe = fixture(out)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    drive_c = out / 'drive-c'
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    shots = {name: out / f'{name}.ppm' for name in ('dialog', 'hidden', 'under', 'destroyed', 'overlap', 'upper-first')}

    def keys(*steps):
        """key=, then the title the fixture sets, then a quiet queue."""
        result = []
        for key, title in steps:
            result += [f'0.2:key={ord(key):02X}', f'0:wait=|{title}', '0.2:idle=300']
        return result
    steps = ['0:wait=HOST_SAVEBITS_W', '0.5:idle=300',
             # 1. The save survives a paint elsewhere and comes back.
             *keys(('1', 'board'), ('2', 'dialog')), f'0.2:shot={shots["dialog"]}',
             *keys(('3', 'painted'), ('4', 'hidden')), f'0.2:shot={shots["hidden"]}',
             # 2. Drawing under the dialog makes the save stale.
             *keys(('2', 'dialog'), ('5', 'under'), ('4', 'hidden')), f'0.2:shot={shots["under"]}',
             # 3. The same as 1 with DestroyWindow.
             *keys(('1', 'board'), ('6', 'destroyed'), ('2', 'dialog'), ('3', 'painted'), ('6', 'destroyed')),
             f'0.2:shot={shots["destroyed"]}',
             *keys(('7', 'overlapping'), ('8', 'lower-hidden'), ('9', 'upper-destroyed')),
             f'0.2:shot={shots["overlap"]}',
             *keys(('7', 'overlapping'), ('A', 'upper-first'), ('4', 'hidden')),
             f'0.2:shot={shots["upper-first"]}', '0.2:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_DEADLINE='150')
    env.pop('NEO98_HOST_ATTACHED', None)
    env.pop('NEO98_HOST_MENU', None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', str(exe), '--quiet', '--drive-c', str(drive_c),
               '--no-registry-file', '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=180)
    shutil.rmtree(drive_c, ignore_errors=True)
    log_text = log_path.read_text(errors='replace')
    waits = re.findall(r'^NEO98 HOST input wait=(.*) (reached|timed out)', log_text, re.M)
    checks = {'host ended': result.returncode == 0,
              'every step reached': bool(waits) and all(state == 'reached' for _, state in waits),
              'screenshots written': all(path.exists() for path in shots.values())}
    if checks['screenshots written']:
        dialog, hidden, under, destroyed = (image(shots[name]) for name in ('dialog', 'hidden', 'under', 'destroyed'))
        # The window stands at 50,40; the dialog at 150,120, 250 by 180, its
        # button at about 233,233..313,263 of the screen.
        spots = {'caption': (270, 130), 'client': (170, 280), 'button': (270, 250), 'edge': (397, 297)}
        checks['the dialog covers the picture'] = (dialog(60, 50) == RED and dialog(170, 280) != RED
                                                 and dialog(270, 250) != RED)
        for name, (x, y) in spots.items():
            checks[f'hidden: picture back under the {name}'] = hidden(x, y) == RED
            checks[f'destroyed: picture back under the {name}'] = destroyed(x, y) == RED
        checks['hidden: the painted square is erased'] = hidden(490, 380) == GREEN and hidden(440, 330) == RED
        checks['destroyed: the painted square is erased'] = destroyed(490, 380) == GREEN
        # The blue block at 170,140..230,180 was drawn while the dialog
        # covered it: nothing of it reached the screen, the save is stale
        # there, and the paint erases.
        checks['under: the spot drawn under the dialog is erased'] = under(200, 160) == GREEN
        checks['under: unchanged parts of the saved background survive'] = all(under(x, y) == RED for x, y in spots.values())
        checks['under: the picture beside the dialog stays'] = under(60, 50) == RED and under(540, 60) == RED
        overlap = image(shots['overlap'])
        checks['overlap: no pixels of the hidden lower dialog return'] = all(
            overlap(x, y) == GREEN for y in range(160, 260) for x in range(150, 400))
        upper_first = image(shots['upper-first'])
        checks['upper-first: lower backup preserves the directly drawn board'] = all(
            upper_first(x, y) == RED for y in range(120, 300) for x in range(150, 400))
    report = {'log': str(log_path), 'exit_code': result.returncode, 'checks': checks}
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'SAVEBITS RESTORE FAIL; see {log_path}')
        return 1
    print('SAVEBITS RESTORE PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
