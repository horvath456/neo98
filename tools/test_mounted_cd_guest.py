#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Insert an ISO mounted outside NEO98; retain it through errors/eject/quit."""
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98
import win98_c
import test_media_change_guest as media

OUT = ROOT / 'build/mounted-cd'

def mounted_images():
    return plistlib.loads(subprocess.check_output(['hdiutil', 'info', '-plist'])).get('images', [])

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    media.OUT = OUT
    fixture = media.build_fixture(neo98.tool_environment())
    external = media.iso('neo98external', {'README.TXT': b'external disc\r\n'})
    owned = media.iso('neo98owned', {'README.TXT': b'owned disc\r\n'})
    first = OUT / 'first-disc'
    first.mkdir(exist_ok=True)
    (first / 'README.TXT').write_text('first disc\n')
    invalid = OUT / 'invalid.iso'
    invalid.write_bytes(b'not a disk image\n')
    bad_cue = OUT / 'invalid.cue'
    bad_cue.write_text('FILE "absent.bin" BINARY\n TRACK 01 MODE1/2048\n INDEX 01 00:00:00\n')
    win98_c.use_pristine()
    drive = OUT / 'drive-c'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', '-c', '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
    attached = plistlib.loads(subprocess.check_output([
        'hdiutil', 'attach', '-readonly', '-nobrowse', '-noverify', '-plist', str(external)]))
    mount = next(Path(e['mount-point']) for e in attached['system-entities'] if e.get('mount-point'))
    try:
        steps = [f'5:cdrom-insert={external}', f'1:cdrom-insert={invalid}',
                 f'1:cdrom-insert={OUT / "missing.iso"}', '1:describe',
                 f'1:cdrom-insert={external}', '1:cdrom-eject',
                 f'1:cdrom-insert={owned}', f'1:cdrom-insert={bad_cue}', '1:describe',
                 # The fixture quits after its eighth device event, so the
                 # reinsertion that produces it comes last.
                 f'1:cdrom-insert={owned}', '1:quit']
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   NEO98_HOST_MENU=';'.join(steps), NEO98_HOST_INPUT='')
        env.pop('NEO98_HOST_ATTACHED', None)
        result = subprocess.run([str(ROOT / 'build/native-app/neo98-host'), str(fixture), str(media.FONT),
                                 '--system-tree', '--quiet', '--drive-c', str(drive),
                                 '--no-registry-file', '--cdrom', str(first)],
                                cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=90)
        log = result.stdout
        (OUT / 'host.log').write_text(log)
        events = (drive / 'DEVCHG.LOG').read_text()
        arrivals = [line for line in events.splitlines() if line.startswith('event=00008000')]
        checks = {
            'external ISO is readable in the guest': sum('label=NEO98EXTERN ' in line and 'current-read=00000000 current-byte=00000065' in line for line in arrivals) == 2,
            'owned ISO is readable after reinsertion': sum('label=NEO98OWNED ' in line and 'current-read=00000000 current-byte=0000006f' in line for line in arrivals) == 2,
            'external mount is reused twice': log.count(f'hdiutil reuse {external}') == 2,
            'owned mount is reused without losing ownership': f'hdiutil reuse {owned}' in log,
            'invalid ISO, missing file and bad CUE show notices': log.count('HOST notice Could not mount CD-ROM') == 3,
            'failed inserts retain the menu medium': 'CD-ROM D: NEO98EXTERNAL' in log and 'CD-ROM D: NEO98OWNED' in log,
            'external mount remains readable after eject and quit': (mount / 'README.TXT').read_bytes() == b'external disc\r\n',
            'only the owned image is detached': log.count('HOST hdiutil detach ') == 1 and '(quit)' in log,
            'owned image is absent after quit': not any(Path(i['image-path']).resolve() == owned.resolve() for i in mounted_images()),
            'host ends normally': result.returncode == 0,
        }
        (OUT / 'results.json').write_text(json.dumps({'checks': checks, 'events': events.splitlines()}, indent=2) + '\n')
        for name, ok in checks.items():
            print(('ok   ' if ok else 'FAIL ') + name)
        assert all(checks.values()), str(OUT / 'host.log')
        print(f'MOUNTED CD PASS: {len(checks)} checks')
    finally:
        subprocess.run(['hdiutil', 'detach', '-force', str(mount)], check=True)

if __name__ == '__main__':
    main()
