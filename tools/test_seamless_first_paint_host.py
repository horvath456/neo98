#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Seamless: a program that shows its window before it draws it.

At the start of a program the user saw a window in the colour of the guest's
desktop for a moment: the program shows its window, loads, and paints later;
the client holds the pixels of the guest's desktop until then, and the host
made the window opaque a second after it appeared whether it had painted or
not. tools/host_slow_paint_fixture.c does that on purpose (2.5 seconds
between ShowWindow and its first WM_PAINT). The host runs headless with
--seamless and NEO98_SEAMLESS_TRACE=1, which reports for every picture how
much of a window the Mac shows is still the desktop's colour:

- no window of the fixture is on the Mac before it has painted: no trace line
  with half of it in the desktop's colour, and the mask is transparent there
  1.5 seconds after it appeared;
- the Mac shows it only after its first paint (the order of the log), and
  then the mask is opaque in its client and caption;
- with NEO98_SEAMLESS_GRACE=1000 the same window appears after a second,
  unpainted: a window that never paints does not stay invisible.

    build/analysis-venv/bin/python tools/test_seamless_first_paint_host.py [--no-build]

Results in build/seamless-first-paint-host/results.json.
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
from test_seamless_program_mode_host import fixture, alpha  # noqa: E402

IMPORTS = {'KERNEL32': {'ExitProcess': 4, 'GetModuleHandleA': 4, 'Sleep': 4},
           'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16, 'GetMessageA': 16,
                      'DispatchMessageA': 4, 'SetWindowTextA': 8, 'ShowWindow': 8, 'BeginPaint': 8, 'EndPaint': 8,
                      'FillRect': 12, 'GetClientRect': 8},
           'GDI32': {'CreateSolidBrush': 4}}


def main():
    out = neo98.output_directory('seamless-first-paint-host')
    for old in list(out.glob('*.pgm')) + list(out.glob('*.log')):
        old.unlink()
    exe = fixture(out, 'host_slow_paint_fixture.c', 'HSLOW.EXE', IMPORTS)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    results = {}
    for name, grace in (('default', None), ('grace-1000', '1000')):
        drive = out / 'drive-c'
        shutil.rmtree(drive, ignore_errors=True)
        subprocess.run(['cp', '-c', '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
        early, late = out / f'{name}-early.pgm', out / f'{name}-late.pgm'
        steps = ['0:wait=|slow', f'1.5:mask={early}', '1.5:wait=|slow painted', '2:idle=300', f'2:mask={late}',
                 '2.5:menu=quit']
        command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
                   '--exe', str(exe), '--quiet', '--drive-c', str(drive), '--no-registry-file',
                   '--crash-report-dir', str(out / 'reports'), '--seamless']
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
                   NEO98_HOST_SCREEN='1100x700', NEO98_HOST_DEADLINE='120', NEO98_SEAMLESS_TRACE='1')
        for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_SEAMLESS_GRACE'):
            env.pop(key, None)
        if grace:
            env['NEO98_SEAMLESS_GRACE'] = grace
        log_path = out / f'host-{name}.log'
        with log_path.open('w') as log:
            code = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                  stdin=subprocess.DEVNULL, timeout=180).returncode
        shutil.rmtree(drive, ignore_errors=True)
        text = log_path.read_text(errors='replace')
        waits = re.findall(r'^NEO98 HOST input wait=(\S.*) (reached|timed out) after', text, re.M)
        paints = [(float(t), int(share), title) for t, share, title in
                  re.findall(r'^NEO98 HOST seamless paint t=([0-9.]+) hwnd=\w+ 400x300 desktop=(\d+)% awaits=\d title=(.*)$',
                             text, re.M)]
        shows = [float(t) for t in re.findall(r'^NEO98 HOST seamless shows 1 t=([0-9.]+): \[\w+ 200,150,400x300 ', text, re.M)]
        first = text.find('NEO98 HOST seamless shows 1 ')
        painted = text.find('wait=|slow painted reached')
        checks = {'host ended': code == 0,
                  'every wait reached': bool(waits) and all(w[1] == 'reached' for w in waits),
                  'both masks written': early.exists() and late.exists()}
        if early.exists() and late.exists():
            early_at, late_at = alpha(early)[2], alpha(late)[2]
            checks['painted, the window is opaque in caption and client'] = late_at(400, 160) == 255 and late_at(400, 300) == 255
            if grace:
                checks['a window that has not painted appears after the grace'] = early_at(400, 300) == 255
            else:
                checks['not on the Mac 1.5 s after it appeared, unpainted'] = early_at(400, 300) == 0 and early_at(400, 160) == 0
                checks['never on the Mac in the colour of the desktop'] = bool(paints) and all(share < 50 for _, share, _ in paints)
                checks['the Mac shows it once, with its picture'] = len(shows) == 1
        results[name] = checks
        for check, okay in checks.items():
            print(f"{'ok  ' if okay else 'FAIL'} {name}: {check}")
    (out / 'results.json').write_text(json.dumps(results, indent=1))
    if not all(all(c.values()) for c in results.values()):
        print(f'SEAMLESS FIRST PAINT FAIL; see {out}')
        return 1
    print('SEAMLESS FIRST PAINT PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
