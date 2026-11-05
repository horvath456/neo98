#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The picture a program shows after its start is the picture it paints again.

Each program is started alone in the host without a screen. After it has
settled a screenshot is taken, then every window is made invalid with its
frame and its children (the `redraw=1` step of NEO98_HOST_INPUT, as
RedrawWindow(NULL, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME |
RDW_ALLCHILDREN)), and a second screenshot follows. A difference means the
first picture held something the program does not paint (pixels left by
another window, a control drawn in the wrong place) or the repaint lost
something.

Notepad and Calculator of the Win98 folder always run; the programs of the
collection run when the collection is there. A program whose own WM_PAINT
does not repaint (MahJong 2000 only erases) is no use here and is not in the
list.

    build/analysis-venv/bin/python tools/test_redraw_stable_host.py [--no-build]
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
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's
from programs_drive import GAMES  # noqa: E402

# name, folder of the collection (None: drive C:), program, seconds until it
# has settled, points that may differ (Notepad's caret blinks: 1 by 15).
PROGRAMS = [('notepad', None, 'C:\\WINDOWS\\NOTEPAD.EXE', 4, 40),
            ('calc', None, 'C:\\WINDOWS\\CALC.EXE', 4, 0),
            ('chips', 'CHIPS', 'CHIPS.EXE', 7, 0),
            ('schnauz', 'SCHNAUZ', 'SCHNAUZ.EXE', 7, 0),
            ('tetwin', 'ALTEPC/Spiele/TETWIN', 'TETWIN.EXE', 7, 0)]


def different(first, second):
    a, b = Path(first).read_bytes(), Path(second).read_bytes()
    if len(a) != len(b):
        return -1
    start = len(a) - len(a.split(b'\n', 3)[3])
    return sum(a[i:i + 3] != b[i:i + 3] for i in range(start, len(a), 3)) if a != b else 0


def main():
    out = neo98.output_directory('redraw-stable-host')
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    checks, skipped = {}, []
    for name, folder, program, settle, allowed in PROGRAMS:
        if folder is not None and not (GAMES / folder / program).exists():
            skipped.append(name)
            continue
        work = out / name
        shutil.rmtree(work, ignore_errors=True)
        work.mkdir(parents=True)
        drive_c, drive_d = work / 'drive-c', work / 'drive-d'
        subprocess.run(['cp', '-c', '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
        drives = []
        if folder is not None:
            subprocess.run(['cp', '-c', '-R', str(GAMES / folder), str(drive_d)], check=True)
            drives = ['--drive', f'D={drive_d}']
            program = 'D:\\' + program
        before, after = work / 'before.ppm', work / 'after.ppm'
        steps = [f'{settle}:shot={before}', f'{settle + 0.3}:redraw=1', f'{settle + 3}:shot={after}',
                 f'{settle + 3.3}:menu=quit']
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_DEADLINE='120')
        env.pop('NEO98_HOST_ATTACHED', None)
        env.pop('NEO98_HOST_MENU', None)
        command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                   '--system-tree', '--exe', program, '--quiet', '--drive-c', str(drive_c), *drives,
                   '--no-registry-file', '--crash-report-dir', str(work / 'reports')]
        with (work / 'host.log').open('w') as log:
            result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                    stdin=subprocess.DEVNULL, timeout=150)
        shutil.rmtree(drive_c, ignore_errors=True)
        shutil.rmtree(drive_d, ignore_errors=True)
        log_text = (work / 'host.log').read_text(errors='replace')
        checks[f'{name}: host ended'] = result.returncode == 0
        checks[f'{name}: the redraw step ran'] = 'NEO98 HOST redraw 1' in log_text
        written = before.exists() and after.exists()
        checks[f'{name}: screenshots written'] = written
        if written:
            count = different(before, after)
            checks[f'{name}: the same picture after the redraw ({count} points differ)'] = 0 <= count <= allowed
            before.unlink()
            after.unlink()
    (out / 'results.json').write_text(json.dumps({'checks': checks, 'skipped': skipped}, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    for name in skipped:
        print(f'SKIP {name}: not in the collection')
    if not all(checks.values()):
        print(f'REDRAW STABLE FAIL; see {out}')
        return 1
    print('REDRAW STABLE PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
