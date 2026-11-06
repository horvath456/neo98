#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""German typing in the host reaches Notepad through TranslateMessage.

Notepad starts on the pristine C: with the German layout. The host types
umlauts, sharp s, AltGr characters (@, euro, backslash) and two dead keys; the
characters come from USER's TranslateMessage through KBDGR's
tables, not from the host. File, Save (a click on File, then S) saves the text as C:\\B2651.TXT (the path
itself is typed with AltGr+sharp s for the backslash), and the check reads the
file in Windows-1252.

    build/analysis-venv/bin/python tools/test_translate_message_host.py [--no-build]

Results in build/translate-message-host/results.json.
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

# ^ (DCh) then e gives e circumflex, the acute (DDh) then a gives a acute,
# the acute then space gives the acute itself.
EXPECTED = 'Grüße Öl, x@y € 1\\2 ê á ´!'.encode('cp1252')


def main():
    out = neo98.output_directory('translate-message-host')
    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    registry = out / 'session.registry'
    shutil.copyfile(win98_c.journal(win98_c.FOLDER), registry)
    shot = out / 'notepad.ppm'
    shot.unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    steps = ['0:wait=Notepad', '0:idle=1500',
             '1:type=Grüße Öl, x@y € 1\\2 ', '1:key=dc', '1:type=e ', '1:key=dd', '1:type=a ',
             '1:key=dd', '1:type= !', '1:idle=1000', f'2:shot={shot}',
             '3:click=85,86', '3:wait=#32768', '3:idle=500', '3:type=s', '3:wait=#32770|Save As', '3:idle=1000', '4:type=C:\\B2651.TXT', '4:key=0d',
             '4:idle=3000', '5:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_KEYMAP'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'C:\\WINDOWS\\NOTEPAD.EXE', '--quiet',
               '--drive-c', str(drive_c), '--registry', str(registry),
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=900)
    saved = next((p for p in drive_c.iterdir() if p.name.upper() == 'B2651.TXT'), None)
    content = saved.read_bytes() if saved else b''
    shutil.rmtree(drive_c, ignore_errors=True)
    text = log_path.read_text(errors='replace')
    checks = {
        'Notepad open': 'input wait=Notepad reached' in text,
        'Save As open': 'input wait=#32770|Save As reached' in text,
        'file saved': saved is not None,
        'text typed through the layout': content == EXPECTED,
        'screenshot written': shot.exists(),
        'host ended': result.returncode == 0,
    }
    (out / 'results.json').write_text(json.dumps({'steps': steps, 'checks': checks,
                                                 'content': content.decode('cp1252', 'replace')},
                                                indent=2, ensure_ascii=False) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'saved: {content!r}\nwanted: {EXPECTED!r}')
        print(f'TRANSLATE MESSAGE HOST FAIL: see {log_path}')
        return 1
    print(f'TRANSLATE MESSAGE HOST PASS: {shot}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
