#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Crilian's audio backlog during a 40-second run, on a private DOS/Windows copy.

--audio-driver dummy deliberately consumes audio slower than real time. The
queue must stay bounded even then. coreaudio checks the real macOS device.
The measured queue excludes the sound card's own output latency.
"""
import argparse
from test_support import ExternalInputParser
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import first_logon


def main():
    parser = ExternalInputParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/crilian-audio-host')
    parser.add_argument('--audio-driver', default='dummy', choices=['dummy', 'coreaudio'])
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    drive, registry = first_logon.clone(out / 'session')
    games = out / 'drive-d'
    games.mkdir(exist_ok=True)
    shutil.copyfile(args.game, games / 'CRILIAN.EXE')
    steps = ['0:wait=tty', '4:key=d', '14:key=d', f'16:shot={out}/play.ppm',
             f'35:shot={out}/later.ppm', '40:menu=quit']
    for t in (8, 18, 38):
        steps += [f'{t}:pause=1', f'{t + 0.1}:pause=0']
    steps.sort(key=lambda step: float(step.split(':', 1)[0]))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER=args.audio_driver,
               NEO98_HOST_INPUT=';'.join(steps), NEO98_TRACE_AUDIO='1', NEO98_HOST_DEADLINE='65',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'),
               NEO98_AUDIO_TAP=str(out / 'audio.tap'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_DOS_TEST_OPTIONS'):
        env.pop(key, None)
    try:
        with (out / 'host.log').open('w') as log:
            result = subprocess.run([str(args.host.resolve()), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                '--system-tree', str(ROOT / 'build/win98-c.setup'), '--drive-c', str(drive), '--registry', str(registry),
                '--drive', f'D={games}', '--exe', r'D:\CRILIAN.EXE', '--quiet'], env=env, cwd=ROOT,
                stdout=log, stderr=subprocess.STDOUT, timeout=90)
        text = (out / 'host.log').read_text(errors='replace')
        queues = [int(n) for n in re.findall(r'pause on \(script\).*dos-machines=1 dos-sound=(\d+)', text)]
        checks = {'normal exit': result.returncode == 0 and not any(x in text for x in
                  ('CRASHREPORT', 'NEO98 HOST failed', 'timed out', 'deadline reached')),
                  'Crilian running': 'Program: CRILIAN' in text,
                  'audio device opened': f'NEO98 AUDIO driver={args.audio_driver}' in text,
                  'three backlog measurements': len(queues) == 3,
                  'combined DOS queues at most 100 ms': bool(queues) and max(queues) <= 4410,
                  'later game picture': (out / 'later.ppm').is_file()}
        report = {'driver': args.audio_driver, 'host': str(args.host.resolve()), 'queued_frames': queues,
                  'queued_ms': [round(n / 44.1, 2) for n in queues],
                  'catchups': text.count('DOSVM audio catch-up'), 'checks': checks,
                  'status': 'PASS' if all(checks.values()) else 'FAIL'}
        (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
        return 0 if all(checks.values()) else 1
    finally:
        shutil.rmtree(out / 'session', ignore_errors=True)


if __name__ == '__main__':
    raise SystemExit(main())
