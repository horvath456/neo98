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
- SCANDSKW.EXE hands its drives to SHELL.401 SHCheckDrive, which says in
  ScanDisk's own words (SHELL.DLL string 10320 or 10351) that the drives,
  host folders without a block driver, cannot be checked; SCANDSKW ends
  without a stop and without a report.
- DRVSPACE.EXE once more, closed with Alt+F4, leaves no window in front;
  SCANDSKW.EXE opened then, with nothing chosen, still starts at once: the
  open wakes the first program even without a foreground window.

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
             f'1:open={program("SCANDSKW.EXE")}', '0:wait=#32770|ScanDisk', '1:idle=1000',
             f'0:shot={out / "scandskw.bmp"}', '0:key=0d', '2:idle=1000',
             f'0:open={program("DRVSPACE.EXE")}', '0:wait=|DriveSpace 3', '1:idle=1000',
             '0:keydown=12', '0:key=73', '0:keyup=12', '2:idle=2000',
             '0:tops=1', f'0:open={program("SCANDSKW.EXE")}', '0:wait=#32770|ScanDisk', '1:idle=1000',
             '0:key=0d', '2:idle=1000',
             '0:wait=Notepad', '1:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_KEYMAP'):
        env.pop(key, None)
    # The system font of the drive itself: build/generated is not in every
    # checkout.
    command = [str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--system-tree',
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
    found = re.findall(r'^NEO98 HOST input wait=(.*) (reached|timed out)', text, re.M)
    waits, first = dict(found), {}
    for name, state in found:
        first.setdefault(name, state)
    boxes = re.findall(r'FAULT BOX (\w+): (.*)$', text, re.M)
    reports = re.findall(r'CRASHREPORT (\S+)', text)
    checks = {
        'DEFRAG shows its drive dialog': waits.get('#32770|Select Drive') == 'reached',
        'DEFRAG asks real mode INT 21h 440Dh and goes on':
            'for real mode INT 21h AX=440dh' not in text and 'Win16 DOS3Call function 44h' not in text,
        'after OK DEFRAG shows its own error box, not the drive dialog again':
            waits.get(f'#32770|{ERROR_TITLE}') == 'reached',
        'DRVSPACE shows its window': first.get('|DriveSpace 3') == 'reached',
        'neither DEFRAG nor DRVSPACE stops': not any(t in ('Defrag', 'Drvspace') for t, _ in boxes),
        'SCANDSKW says that ScanDisk cannot check the drives': first.get('#32770|ScanDisk') == 'reached',
        'opened again after DRVSPACE ended, with nothing chosen, SCANDSKW says it at once':
            text.count('NEO98 HOST input wait=|DriveSpace 3 reached') == 2
            and text.count('NEO98 HOST input wait=#32770|ScanDisk reached') == 2,
        'no program stops and no report': not boxes and not reports,
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
