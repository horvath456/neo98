#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Original Doom 3 CD's DemoShield launcher: startup, Extras, clean Quit."""
import argparse
from test_support import ExternalInputParser
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from PIL import Image, ImageChops
ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import win98_c

def main():
    parser = ExternalInputParser()
    parser.add_argument('iso', type=Path)
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/doom3-launcher-test')
    args = parser.parse_args()
    out = args.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    drive = out / 'c'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.use_pristine()), str(drive)], check=True)
    command = [str(args.host.resolve()), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
        '--system-tree', str(drive), '--drive-c', str(drive), '--cdrom', str(args.iso.resolve()),
        '--exe', r'D:\Setup\rsrc\demo32.exe', '--command', r'D:\Setup\rsrc\demo32.exe -q Doom3.dbd',
        '--directory', r'D:\Setup\rsrc', '--no-registry-file', '--quiet',
        '--crash-report-dir', str(out / 'reports')]
    try:
        for name, steps in (
            ('navigation', [f'3:shot={out}/main.ppm', '4:click=550,156',
                            f'7:shot={out}/extras.ppm', '9:menu=quit']),
            ('quit', ['4:click=580,373']),
        ):
            env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'),
                NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_DEADLINE='25')
            for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_TRACE_CALLS_FROM', 'NEO98_TRACE_PAINT'):
                env.pop(key, None)
            with (out / (name + '.log')).open('w') as log:
                result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=40)
            log = (out / (name + '.log')).read_text(errors='replace')
            assert result.returncode == 0, log[-3000:]
            assert not any(s in log for s in ('FAULT BOX', 'CRASHREPORT', 'NEO98 HOST failed', 'deadline reached')), log[-3000:]
            if name == 'quit': assert 'NEO98 HOST exited code=0 ' in log, log[-3000:]
        frames = []
        for name in ('main', 'extras'):
            frame = Image.open(out / (name + '.ppm')).convert('RGB')
            assert frame.size == (800, 600), frame.size
            frame.save(out / (name + '.png'))
            frames.append(frame.crop((80, 60, 720, 540)))
        assert len(frames[0].getcolors(640 * 480)) > 100, 'launcher did not render'
        delta = ImageChops.difference(*frames).convert('L')
        changed = 640 * 480 - delta.histogram()[0]
        assert changed > 2000, changed
        (out / 'results.json').write_text(json.dumps({'status': 'PASS', 'host': str(args.host.resolve()),
            'checks': ['original DemoShield startup', 'Extras navigation', 'clean Quit', 'no crash dialog'],
            'navigation_changed_pixels': changed,
            'limits': ['Doom 3 installation and gameplay not tested']}, indent=2) + '\n')
        print('DOOM 3 LAUNCHER PASS: startup, Extras, clean Quit')
    finally:
        shutil.rmtree(drive, ignore_errors=True)

if __name__ == '__main__': main()
