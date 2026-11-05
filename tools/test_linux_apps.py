#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check original Linux guest startup, windows and rendered content."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import re
import subprocess

from native_app import ROOT, build_fast
import win98_c

APPS = {
    'notepad': r'C:\WINDOWS\NOTEPAD.EXE',
    'calc': r'C:\WINDOWS\CALC.EXE',
    'winver': r'C:\WINDOWS\WINVER.EXE',
    'regedit': r'C:\WINDOWS\REGEDIT.EXE',
    'taskman': r'C:\WINDOWS\TASKMAN.EXE',
    'wordpad': r'C:\Program Files\Accessories\WORDPAD.EXE',
    'mspaint': r'C:\Program Files\Accessories\MSPAINT.EXE',
    'spider': r'C:\Program Files\PLUS!\Spider\spider.exe',
    'wonderland': r'C:\Program Files\Wonderland\Wonderland.exe',
    'stinky-test': r'C:\Stinky Test\gametest.exe',
    'budredhead': r'C:\Program Files\Bud Redhead\BudRedhead.exe',
    'driving-school': r'C:\Program Files\3D Driving-School\academy.exe',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path)
    parser.add_argument('--only', nargs='+', choices=APPS)
    parser.add_argument('--seconds', type=int, default=10)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/native-linux/apps')
    args = parser.parse_args()
    folder = win98_c.use_pristine()
    runner = args.runner or build_fast('probe_application.cpp', 'neo98-probe')
    out = args.out
    out.mkdir(parents=True, exist_ok=True)

    def run(name):
        guest = APPS[name]
        source = folder / guest[3:].replace('\\', '/')
        if not source.is_file():
            return {'app': name, 'status': 'UNAVAILABLE', 'guest': guest}
        command = [str(runner), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                   '--system-tree', '--exe', guest, '--current-directory', guest.rsplit('\\', 1)[0],
                   '--time-limit', str(args.seconds), '--screenshot', str(out / f'{name}.ppm')]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=args.seconds + 40)
        log = result.stdout + result.stderr
        (out / f'{name}.log').write_text(log)
        summary = re.search(r'^PROBE started=(\d+) state=(\d+) code=(\d+) calls=(\d+) callbacks=(\d+) windows=(\d+) error=(.*)$', log, re.M)
        windows = [line for line in log.splitlines() if line.startswith('PROBE_WINDOW ') and 'visible=1 ' in line]
        content = any(re.search(r'colours=(?:64\+|[2-9]\d*|1\d+)$', line) for line in windows)
        passed = result.returncode == 0 and summary and summary[1] == '1' and summary[2] in ('0', '1') and not summary[7] and content
        game = name in ('wonderland', 'stinky-test', 'budredhead', 'driving-school')
        status = 'STARTUP_ONLY' if passed and game else 'PASS' if passed else 'FAIL'
        row = {'app': name, 'status': status, 'guest': guest,
               'scope': 'startup and rendered window content, gameplay unverified' if game else 'startup and rendered window content', 'summary': summary[0] if summary else log[-1500:]}
        print(name, row['status'], row['summary'], flush=True)
        return row

    with ThreadPoolExecutor(max_workers=args.jobs) as workers:
        rows = list(workers.map(run, args.only or APPS))
    assert not win98_c.pristine_problems(folder), 'The test modified its source tree'
    (out / 'results.json').write_text(json.dumps(rows, indent=2) + '\n')
    assert all(row['status'] != 'FAIL' for row in rows), f'See {out}/results.json'


if __name__ == '__main__':
    main()
