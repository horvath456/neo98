#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The disk tools of Win98 on drives that are host folders.

The host runs Notepad from a clone of the pristine drive C: under SDL's dummy
video driver and opens the three disk tools of the tree the way a double click
does:

- DEFRAG.EXE asks its drive questions through DPMI 0300h (real mode INT 21h
  440Dh): it shows "Select Drive"; after OK the lock of the drive is
  refused and DEFRAG shows its own message box "Error Defragmenting Drive C"
  with its own text, not the procedure of the drive dialog;
- DRVSPACE.EXE shows its window "DriveSpace 3" (4404h answers
  ERROR_INVALID_FUNCTION);
- SCANDSKW.EXE reaches SHELL.401 SHCheckDrive, the ScanDisk front end this
  port does not carry: the stop shows the error box of Win98 naming
  the program's call, and the host writes its report.

    build/analysis-venv/bin/python tools/test_disk_tools_host.py [--no-build]

Results in build/disk-tools-host/results.json.

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
import win98_c  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402


# String 353 of DEFRAG.EXE, "Error Defragmenting Drive %1 %2", with an empty %2.
ERROR_TITLE = 'Error Defragmenting Drive C '


def main():
    out = neo98.output_directory('disk-tools-host')
    for name in ('drive-c', 'reports'):
        shutil.rmtree(out / name, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    pristine = win98_c.use_pristine()
    drive_c = out / 'drive-c'
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(pristine) + '/', str(drive_c)], check=True)
    (drive_c / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    windows = next(p for p in drive_c.iterdir() if p.name.upper() == 'WINDOWS')

    def program(name):
        return next(p for p in windows.iterdir() if p.name.upper() == name)
    steps = ['0:wait=Notepad', '1:idle=1000',
             f'1:open={program("DEFRAG.EXE")}', '0:wait=#32770|Select Drive', '1:idle=1000',
             f'0:shot={out / "defrag-select.bmp"}', '0:key=0d',
             f'0:wait=#32770|{ERROR_TITLE}', '1:idle=1000', f'0:shot={out / "defrag-error.bmp"}',
             '0:key=0d', '0:wait=#32770|Select Drive', '1:idle=1000', '0:key=1b', '2:idle=2000',
             f'1:open={program("DRVSPACE.EXE")}', '0:wait=|DriveSpace 3', '1:idle=1000',
             f'0:shot={out / "drvspace.bmp"}', '0:keydown=12', '0:key=73', '0:keyup=12', '2:idle=2000',
             # Without a taskbar the open is served by Notepad, woken through
             # the foreground window (src/x86_host_open.cpp).
             '0:choose=Untitled - Notepad', '1:idle=500',
             f'1:open={program("SCANDSKW.EXE")}', '0:wait=#32770|Scandskw', '1:idle=1000',
             f'0:shot={out / "scandskw.bmp"}', '0:key=0d', '2:idle=1000',
             '0:wait=Notepad', '1:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_KEYMAP'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
               '--exe', 'C:\\WINDOWS\\NOTEPAD.EXE', '--drive-c', str(drive_c), '--no-registry-file',
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        try:
            code = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                  stdin=subprocess.DEVNULL, timeout=1200).returncode
        except subprocess.TimeoutExpired:
            code = 'timeout'
    text = log_path.read_text(errors='replace')
    waits = dict((w, s) for w, s in re.findall(r'^NEO98 HOST input wait=(.*) (reached|timed out)', text, re.M))
    boxes = re.findall(r'FAULT BOX (\w+): (.*)$', text, re.M)
    reports = re.findall(r'CRASHREPORT (\S+)', text)
    reasons = [Path(r, 'report.md').read_text(errors='replace') if Path(r, 'report.md').exists() else ''
               for r in reports]
    checks = {
        'DEFRAG shows its drive dialog': waits.get('#32770|Select Drive') == 'reached',
        'DEFRAG asks real mode INT 21h 440Dh and goes on':
            'for real mode INT 21h AX=440dh' not in text and 'Win16 DOS3Call function 44h' not in text,
        'after OK DEFRAG shows its own error box, not the drive dialog again':
            waits.get(f'#32770|{ERROR_TITLE}') == 'reached',
        'DRVSPACE shows its window': waits.get('|DriveSpace 3') == 'reached',
        'neither DEFRAG nor DRVSPACE stops': not any(t in ('Defrag', 'Drvspace') for t, _ in boxes),
        'SCANDSKW stops at SHELL.401 with the error box of Win98 at its call':
            any(t == 'Scandskw' and re.fullmatch(r'SCANDSKW caused an exception c0000002H in module SCANDSKW\.EXE '
                                                 r'at 0001:000005d3\.', m) for t, m in boxes),
        'Close ended it': text.count('FAULT BOX closed') == 1,
        'one report, naming the stop':
            len(reports) == 1 and 'Win16 SHELL ordinal 401 is not ported yet' in reasons[0],
        'Notepad is still there and the host ended in order':
            waits.get('Notepad') == 'reached' and code == 0 and 'NEO98 HOST failed' not in text,
    }
    shutil.rmtree(drive_c, ignore_errors=True)
    shutil.rmtree(out / 'reports', ignore_errors=True)
    (out / 'results.json').write_text(json.dumps({'checks': checks, 'boxes': boxes, 'waits': waits,
                                                  'log': str(log_path)}, indent=2) + '\n')
    failed = [name for name, okay in checks.items() if not okay]
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if failed:
        print(f'{len(failed)} of {len(checks)} checks failed; log {log_path}')
        return 1
    print(f'disk tools: {len(checks)} checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
