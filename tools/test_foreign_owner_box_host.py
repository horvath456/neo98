#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A message box whose owner is a window of another process appears.

WLOADER.EXE (Win16, Coktel) needs a 256-colour desktop and says so in a
message box before it has a window of its own. The owner it names is what
GetTopWindow(0) answers; started from the shell that is a window of
EXPLORER.EXE. The port refused such an owner twice (in the message box and in
CreateWindow), MessageBox answered 0, and the program ended without a sign.

The host runs under SDL's dummy video driver: the shell on the session after
the first logon (tools/first_logon.py), Start, Run, the program. Checks:

- the box is on the screen (its caption bar and the yellow of its icon),
- its OK button closes it, and the Start menu still opens afterwards.

The program comes from the collection of the reference system; without it the test
is skipped.

    build/analysis-venv/bin/python tools/test_foreign_owner_box_host.py [--no-build]
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
from native_app import ROOT, build_host  # noqa: E402
import first_logon  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

from external_paths import external_path
SOURCE = external_path('NEO98_COLLECTION') / 'GAMES/ALTEPC/COKTEL/JUNIOR5'
BOX = (267, 227, 533, 345)       # the box in the middle of 800x600
START = (0, 300, 160, 570)       # where the Start menu opens


def pixels(path):
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    return body, int(size.split()[0])


def count(image, box, colour):
    body, width = image
    left, top, right, bottom = box
    return sum(1 for y in range(top, bottom) for x in range(left, right)
               if body[(y * width + x) * 3:(y * width + x) * 3 + 3] == colour)


def differing(first, second, box):
    (a, width), (b, _) = first, second
    left, top, right, bottom = box
    return sum(1 for y in range(top, bottom) for x in range(left, right)
               if a[(y * width + x) * 3:(y * width + x) * 3 + 3] != b[(y * width + x) * 3:(y * width + x) * 3 + 3])


def main():
    if not (SOURCE / 'WLOADER.EXE').is_file():
        print(f'SKIP: {SOURCE} is not there (program collection)')
        raise SystemExit(77)
    out = neo98.output_directory('foreign-owner-box-host')
    shutil.rmtree(out / 'drive-d', ignore_errors=True)
    drive_c, registry = first_logon.clone(out)
    drive_d = out / 'drive-d'
    drive_d.mkdir()
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(SOURCE), str(drive_d / 'JUNIOR5')], check=True)
    shots = {name: out / f'{name}.ppm' for name in ('box', 'closed', 'start')}
    for shot in shots.values():
        shot.unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    steps = ['0:wait=Shell_TrayWnd', '0:idle=3000', '1:click=25,587', '1:idle=1500', '2:type=r',
             '2:wait=#32770|Run', '2:idle=1000', '3:type=D:\\JUNIOR5\\WLOADER.EXE', '3:idle=500', '3:key=0d',
             '3:idle=6000', f'4:shot={shots["box"]}', '5:click=399,318', '5:idle=3000',
             f'6:shot={shots["closed"]}', '7:click=25,587', '7:idle=2000', f'8:shot={shots["start"]}',
             '9:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps))
    env.pop('NEO98_HOST_ATTACHED', None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', str(win98_c.FOLDER), '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--quiet',
               '--drive-c', str(drive_c), '--registry', str(registry), '--drive', f'D={drive_d}',
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=400)
    checks, numbers = {'host ended': result.returncode == 0}, {}
    checks['three screenshots'] = all(shot.exists() for shot in shots.values())
    if checks['three screenshots']:
        box, closed, start = (pixels(shots[name]) for name in ('box', 'closed', 'start'))
        numbers['caption pixels'] = count(box, BOX, bytes([0, 0, 128]))
        numbers['icon pixels'] = count(box, BOX, bytes([255, 255, 0]))
        numbers['box pixels gone after OK'] = differing(box, closed, BOX)
        numbers['Start menu pixels'] = differing(closed, start, START)
        checks['the box is on the screen'] = numbers['caption pixels'] > 3000 and numbers['icon pixels'] > 100
        checks['OK closes the box'] = numbers['box pixels gone after OK'] > 20000
        checks['the Start menu opens afterwards'] = numbers['Start menu pixels'] > 20000
    (out / 'results.json').write_text(json.dumps({'log': str(log_path), 'exit_code': result.returncode,
                                                  'numbers': numbers, 'checks': checks}, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    print(json.dumps(numbers))
    failed = [name for name, okay in checks.items() if not okay]
    if failed:
        print(f'FOREIGN OWNER BOX FAIL: {len(failed)} of {len(checks)} checks; see {log_path}')
        return 1
    print(f'FOREIGN OWNER BOX PASS: {len(checks)} checks')
    return 0


if __name__ == '__main__':
    sys.exit(main())
