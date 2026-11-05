#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the original Win16 NFS2 demo extractor and verify every ZIP member.

The installer is supplied by the caller, never downloaded or bundled here.
Extraction runs on a disposable clone of setup's pristine Windows tree.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import win98_c


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('installer', type=Path)
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    args = parser.parse_args()
    installer = args.installer.resolve()
    out = ROOT / 'build/nfs2-sfx'
    out.mkdir(parents=True, exist_ok=True)
    drive = out / 'c'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', '-cR', str(win98_c.use_pristine()), str(drive)], check=True)
    steps = [
        '0:wait=|Enter Target Path', '1:keydown=11', '1.1:key=41', '1.2:keyup=11',
        r'1.5:type=C:\NFSDEMO', '2:button=OK', '3:wait=|Directory Not Found',
        '1:button=Yes',
    ]
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_DEADLINE='150', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    env.pop('NEO98_HOST_ATTACHED', None)
    env.pop('NEO98_HOST_MENU', None)
    command = [str(args.host.resolve()), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', str(drive), '--drive-c', str(drive),
               '--exe', 'F:\\' + installer.name, '--drive-readonly', 'F=' + str(installer.parent),
               '--directory', 'F:\\', '--no-registry-file', '--quiet',
               '--crash-report-dir', str(out / 'reports')]
    with (out / 'host.log').open('w') as log:
        result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=170)
    log = (out / 'host.log').read_text(errors='replace')
    assert result.returncode == 0 and 'NEO98 HOST exited code=0 ' in log, log[-4000:]
    members = []
    with zipfile.ZipFile(installer) as archive:
        for info in archive.infolist():
            if info.is_dir():
                continue
            path = drive / 'NFSDEMO' / info.filename.replace('\\', '/')
            data = path.read_bytes()
            assert len(data) == info.file_size and zlib.crc32(data) == info.CRC, info.filename
            members.append({'name': info.filename, 'bytes': len(data), 'crc32': f'{info.CRC:08x}'})
    assert members
    (out / 'results.json').write_text(json.dumps({'status': 'PASS', 'files': members}, indent=2) + '\n')
    print(f'NFS2 SFX PASS: {len(members)} files, every size and CRC matches')
    shutil.rmtree(drive)


if __name__ == '__main__':
    main()
