#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Half-Life's original WAV test on a disposable copy of an initialized desktop.

The source Windows tree and registry journal are only read. Audio uses SDL's
dummy output; this verifies drawing during playback, not audible sound.
"""
import argparse
from test_support import ExternalInputParser
import json
import os
from pathlib import Path
import shutil
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = ExternalInputParser()
    parser.add_argument('iso', type=Path)
    parser.add_argument('--windows', type=Path, required=True)
    parser.add_argument('--registry', type=Path, required=True)
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    parser.add_argument('--repeat', type=int, default=2)
    args = parser.parse_args()
    out = ROOT / 'build/half-life-wav-test'
    out.mkdir(parents=True, exist_ok=True)
    results = []
    for run in range(args.repeat):
        folder = out / str(run)
        folder.mkdir(exist_ok=True)
        drive = folder / 'c'
        shutil.rmtree(drive, ignore_errors=True)
        subprocess.run(['cp', '-cR', str(args.windows.resolve()), str(drive)], check=True)
        journal = folder / 'c.registry'
        shutil.copyfile(args.registry, journal)
        steps = ['0:wait=Shell_TrayWnd', '2:guestopen=D:\\Setup.exe', '2:wait=|Half-Life Setup']
        steps += [f'{t}:shot={folder}/{name}.ppm' for t, name in
                  [(6, 'wav'), (9, 'wav-later'), (16, 'question')]]
        steps += ['18:menu=quit']
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_DEADLINE='75',
                   NEO98_SETTINGS_DIR=str(folder / 'settings'), NEO98_LOG_DIR=str(folder / 'logs'))
        for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_TRACE_CALLS_FROM', 'NEO98_TRACE_PAINT'):
            env.pop(key, None)
        with (folder / 'host.log').open('w') as log:
            result = subprocess.run([str(args.host.resolve()), '-',
                str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree', str(drive),
                '--drive-c', str(drive), '--registry', str(journal), '--cdrom', str(args.iso.resolve()),
                '--desktop', '1024x768', '--exe', r'C:\WINDOWS\EXPLORER.EXE', '--quiet',
                '--crash-report-dir', str(folder / 'reports')], env=env, stdout=log,
                stderr=subprocess.STDOUT, timeout=100)
        log = (folder / 'host.log').read_text(errors='replace')
        assert result.returncode == 0 and 'WAVETEST.WAV flags=00000012 result=0' in log, log[-3000:]
        assert 'MessageBox caption=System Test text=Did you hear the sound file?' in log
        assert 'FAULT' not in log and 'CRASH' not in log and 'deadline reached' not in log, log[-3000:]
        counts = {}
        for name in ('wav', 'wav-later', 'question'):
            frame = Image.open(folder / (name + '.ppm')).convert('RGB')
            assert frame.size == (1024, 768), frame.size
            band = frame.crop((1, 744, 1023, 766))
            grey = sum(n for n, color in band.getcolors(30000)
                       if max(color) > 120 and max(color) - min(color) < 8)
            counts[name] = grey
            if name != 'question':
                assert grey < 32, (run, name, 'taskbar remnants', grey)
                popup = frame.crop((380, 350, 640, 415))
                assert sum(n for n, color in popup.getcolors(30000)
                           if color == (192, 192, 192)) > 10000, 'WAV popup missing'
            else:
                assert grey > 18000, ('taskbar did not return', grey)
            frame.save(folder / (name + '.png'))
        results.append(counts)
        shutil.rmtree(drive)
        journal.unlink()
    (out / 'results.json').write_text(json.dumps({'status': 'PASS', 'host': str(args.host.resolve()),
        'runs': results, 'checks': ['WAV playback succeeds', 'no taskbar remnants during WAV test',
            'taskbar returns for System Test question'], 'audio': 'dummy output; listening not tested'}, indent=2) + '\n')
    print('HALF-LIFE WAV PASS', results)


if __name__ == '__main__':
    main()
