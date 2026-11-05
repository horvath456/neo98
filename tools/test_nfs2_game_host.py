#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the original NFS II demo menu and race on a private Windows tree.

The caller supplies nfs2demo.exe. Its ZIP payload is unpacked independently;
test_nfs2_sfx_host.py covers execution of the original Win16 extractor.
"""
import argparse
from test_support import ExternalInputParser
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import win98_c


def main():
    parser = ExternalInputParser()
    parser.add_argument('installer', type=Path)
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    args = parser.parse_args()
    out = ROOT / 'build/nfs2-game'
    out.mkdir(parents=True, exist_ok=True)
    drive = out / 'c'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', '-cR', str(win98_c.use_pristine()), str(drive)], check=True)
    with zipfile.ZipFile(args.installer) as archive:
        for member in archive.infolist():
            relative = Path(member.filename.replace('\\', '/'))
            assert not relative.is_absolute() and '..' not in relative.parts
            target = drive / 'NFSDEMO' / relative
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.read(member))
    steps = [f'18:shot={out}/menu.ppm', '19:move=164,390', '20:down=164,390', '20.4:up=164,390',
             f'30:shot={out}/race-start.ppm', '35:keydown=26',
             f'45:shot={out}/race-driving.ppm', '46:keyup=26', '47:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_DEADLINE='90', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_TRACE_CALLS_FROM'):
        env.pop(key, None)
    command = [str(args.host.resolve()), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', str(drive), '--drive-c', str(drive),
               '--exe', r'C:\NFSDEMO\nfsw.exe', '--directory', r'C:\NFSDEMO',
               '--no-registry-file', '--quiet', '--crash-report-dir', str(out / 'reports')]
    with (out / 'host.log').open('w') as log:
        result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=110)
    assert result.returncode == 0, (out / 'host.log').read_text(errors='replace')[-4000:]
    frames = {}
    for name in ('menu', 'race-start', 'race-driving'):
        frame = Image.open(out / (name + '.ppm')).convert('RGB')
        assert frame.size == (800, 600), (name, frame.size)
        colors = frame.getcolors(800 * 600)
        assert len(colors) > 200, (name, len(colors))
        frame.save(out / (name + '.png'))
        frames[name] = frame
    # Stable cockpit HUD pixels identify a race rather than another animated
    # menu. These remain fixed while the road, clock and speed change.
    for name in ('race-start', 'race-driving'):
        for point, expected in (((280, 40), (156, 18, 18)),
                                ((450, 10), (86, 86, 86)),
                                ((750, 10), (86, 86, 86))):
            actual = frames[name].getpixel(point)
            assert max(abs(a - b) for a, b in zip(actual, expected)) <= 8, (name, point, actual)
    for box in ((80, 40, 180, 78), (325, 4, 414, 40)):
        assert ImageChops.difference(frames['race-start'].crop(box),
                                    frames['race-driving'].crop(box)).getbbox(), box
    # Menus and a moving race must produce different pictures. The original
    # failure returned only the black console and its white text cursor.
    for before, after in (('menu', 'race-start'), ('race-start', 'race-driving')):
        difference = ImageChops.difference(frames[before], frames[after])
        changed = sum(count for count, color in difference.getcolors(800 * 600) if color != (0, 0, 0))
        assert changed > 800 * 600 // 10, (before, after, changed)
    (out / 'results.json').write_text(json.dumps({'status': 'PASS', 'host': str(args.host.resolve()),
        'checks': ['console-subsystem executable displays its GUI', 'menu and cockpit HUD pixels', 'speedometer and clock advance',
                   'race image changes while accelerating'], 'frames': list(frames)}, indent=2) + '\n')
    print('NFS II PASS: menu, race and changing image while accelerating')
    shutil.rmtree(drive)


if __name__ == '__main__':
    main()
