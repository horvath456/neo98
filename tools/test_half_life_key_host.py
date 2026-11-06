#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Original Half-Life CD-key dialog: text, bitmap buttons, hover and Cancel.

Supply an installed game folder containing hl.exe. Runs on a disposable
pristine Windows clone; no product key is supplied or read from the user hive.
"""
import argparse
from test_support import ExternalInputParser
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import win98_c


def main():
    parser = ExternalInputParser()
    parser.add_argument('game', type=Path)
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    args = parser.parse_args()
    assert (args.game / 'hl.exe').is_file()
    out = ROOT / 'build/half-life-key'
    out.mkdir(parents=True, exist_ok=True)
    drive = out / 'c'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.use_pristine()), str(drive)], check=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(args.game.resolve()), str(drive / 'HALFLIFE')], check=True)
    steps = ['0:wait=#32770', f'3:shot={out}/dialog.ppm', '4:move=500,375',
             f'6:shot={out}/hover.ppm', '7:down=500,375', '7.3:up=500,375']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_DEADLINE='45',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_TRACE_CALLS_FROM', 'NEO98_TRACE_PAINT'):
        env.pop(key, None)
    command = [str(args.host.resolve()), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', str(drive), '--drive-c', str(drive),
               '--exe', r'C:\HALFLIFE\hl.exe', '--directory', r'C:\HALFLIFE',
               '--no-registry-file', '--quiet', '--crash-report-dir', str(out / 'reports')]
    with (out / 'host.log').open('w') as log:
        result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=65)
    log = (out / 'host.log').read_text(errors='replace')
    assert result.returncode == 0 and 'NEO98 HOST exited code=0 ' in log, log[-3000:]
    counts = {}
    for name in ('dialog', 'hover'):
        frame = Image.open(out / (name + '.ppm')).convert('RGB')
        assert frame.size == (800, 600), frame.size
        for label, box, minimum in (
                ('explanation', (210, 215, 580, 333), 500),
                ('ok', (368, 363, 474, 389), 30),
                ('cancel', (474, 363, 580, 389), 50)):
            colors = frame.crop(box).getcolors(800 * 600)
            orange = sum(n for n, (r, g, b) in colors if r > 150 and g > 70 and b < 100)
            assert orange >= minimum, (name, label, orange)
            counts[name + '_' + label] = orange
        frame.save(out / (name + '.png'))
    assert counts['hover_cancel'] > counts['dialog_cancel'], counts
    (out / 'results.json').write_text(json.dumps({'status': 'PASS', 'host': str(args.host.resolve()),
        'checks': ['explanation persists', 'both bitmap buttons visible', 'Cancel hover',
                   'Cancel exits without a key'], 'orange_pixels': counts}, indent=2) + '\n')
    print('HALF-LIFE KEY PASS: explanation, OK, Cancel, hover and clean cancellation')
    shutil.rmtree(drive)


if __name__ == '__main__':
    main()
