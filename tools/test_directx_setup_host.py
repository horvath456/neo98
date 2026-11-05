#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the original Doom 3 disc's DirectX installer on a disposable Windows clone.

Stops at the requested restart; does not install into the user's machine.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT/'tools')]
import win98_c


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('iso', type=Path)
    parser.add_argument('--host', type=Path, default=ROOT/'build/native-app/neo98-host')
    args = parser.parse_args()
    assert args.iso.is_file()
    out = ROOT/'build/directx-setup-test'
    out.mkdir(exist_ok=True)
    drive = out/'c'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', '-cR', str(win98_c.use_pristine()), str(drive)], check=True)
    steps = ['0:wait=#32770', f'4:shot={out}/license.ppm',
             '5:click=256,339', '6:click=430,411', f'9:shot={out}/ready.ppm',
             '10:click=430,411', f'30:shot={out}/finished.ppm', '32:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_DEADLINE='50',
               NEO98_SETTINGS_DIR=str(out/'settings'), NEO98_LOG_DIR=str(out/'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_TRACE_CALLS_FROM'):
        env.pop(key, None)
    command = [str(args.host.resolve()), '-', str(ROOT/'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', str(drive), '--drive-c', str(drive), '--cdrom', str(args.iso.resolve()),
               '--exe', r'D:\Directx\dxsetup.exe', '--directory', r'D:\Directx',
               '--no-registry-file', '--quiet', '--crash-report-dir', str(out/'reports')]
    with (out/'host.log').open('w') as log:
        run = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=70)
    hostlog = (out/'host.log').read_text(errors='replace')
    assert run.returncode == 0, hostlog[-3000:]
    assert not any(word in hostlog for word in ('FAULT BOX', 'CRASHREPORT', 'staging slot', 'deadline')), hostlog[-3000:]
    install = drive/'WINDOWS/DirectX.log'
    text = install.read_text(errors='replace')
    assert 'Installation ended with value 1 = Installation succeeded need restart' in text, text[-3000:]
    shutil.copyfile(install, out/'DirectX.log')
    for name in ('license', 'ready', 'finished'):
        frame = Image.open(out/(name+'.ppm')).convert('RGB')
        assert frame.size == (800, 600)
        frame.save(out/(name+'.png'))
    (out/'results.json').write_text(json.dumps({'status':'PASS', 'host':str(args.host.resolve()),
        'checks':['original CD setup opens license page', 'advances to installation',
                  'installer reports successful installation and requests restart',
                  'no runtime failure or crash dialog'],
        'limits':['restart and installed DirectX 9 applications not tested']}, indent=2)+'\n')
    shutil.rmtree(drive)
    print('DIRECTX SETUP PASS: license, installation, successful completion requesting restart')


if __name__ == '__main__': main()
