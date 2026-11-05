#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A batch file as the start program of the host starts each Windows program once.

D:\\BATT\\TWO.BAT names NOTEPAD.EXE and CALC.EXE. The DOS machine hands both
to Windows and ends inside its first frame. Upstream DOSBox Pure took a
machine that had exited when the frontend first asked for its av info as a
reboot of PUREMENU and ran the autoexec again, so both programs started twice;
and two requests inside one frame overwrote each other in the one slot the
core shares with the host, so NOTEPAD was lost. Checks: each program is
requested and created exactly once (tools/build_dosbox_pure.py, patch 4b and
DBP_Neo98LaunchWindows).

    build/analysis-venv/bin/python tools/test_batch_start_host.py [--no-build]

Results in build/batch-start-host/results.json.
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


def main():
    out = neo98.output_directory('batch-start-host')
    drive_c, drive_d = out / 'drive-c', out / 'drive-d'
    for path in (drive_c, drive_d):
        shutil.rmtree(path, ignore_errors=True)
    subprocess.run(['cp', '-c', '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    registry = out / 'session.registry'
    shutil.copyfile(win98_c.journal(win98_c.FOLDER), registry)
    (drive_d / 'BATT').mkdir(parents=True)
    (drive_d / 'BATT/TWO.BAT').write_bytes(b'C:\\WINDOWS\\NOTEPAD.EXE\r\nC:\\WINDOWS\\CALC.EXE\r\n')
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    steps = ['0:wait=Notepad', '0:wait=SciCalc', '0:idle=4000', '1:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
               '--exe', 'D:\\BATT\\TWO.BAT', '--drive-c', str(drive_c), '--registry', str(registry),
               '--drive', f'D={drive_d}', '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=600)
    shutil.rmtree(drive_c, ignore_errors=True)
    text = log_path.read_text(errors='replace')
    counts = {name: text.count(f'DOSVM windows program="C:\\WINDOWS\\{name}"') for name in ('NOTEPAD.EXE', 'CALC.EXE')}
    created = {name: text.count(f'CreateProcess application="" command=""C:\\WINDOWS\\{name}""')
               for name in ('NOTEPAD.EXE', 'CALC.EXE')}
    checks = {'NOTEPAD requested once': counts['NOTEPAD.EXE'] == 1,
              'CALC requested once': counts['CALC.EXE'] == 1,
              'each started once': all(v == 1 for v in created.values()),
              'host ended': result.returncode == 0}
    (out / 'results.json').write_text(json.dumps({'requests': counts, 'created': created,
                                                  'checks': checks}, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'BATCH START FAIL: requests={counts} created={created}; see {log_path}')
        return 1
    print(f'BATCH START PASS: requests={counts}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
