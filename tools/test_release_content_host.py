#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""packaged Notepad saves, opens and finds using the user's installed C:."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from native_app import ROOT
import win98_c
from test_mnemonic_host import alt


def main():
    out = ROOT / 'build/release-content-host'
    out.mkdir(exist_ok=True)
    binary = ROOT / 'build/Neo98.app/Contents/MacOS/Neo98'
    wizard_settings = out / 'wizard-settings'
    shutil.rmtree(wizard_settings, ignore_errors=True)
    wizard_env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                      NEO98_HOST_DEADLINE='120', NEO98_HOST_WIZARD='check;quit',
                      NEO98_SETTINGS_DIR=str(wizard_settings), NEO98_LOG_DIR=str(out / 'logs'))
    with (out / 'wizard.log').open('w') as log:
        wizard = subprocess.run([str(binary)], cwd=ROOT, env=wizard_env,
                                stdout=log, stderr=subprocess.STDOUT)
    wizard_log = (out / 'wizard.log').read_text(errors='replace')
    checks = {'setup without C:': wizard.returncode == 0 and
              'NEO98 WIZARD shown settings=' in wizard_log and
              'NEO98 WIZARD quit' in wizard_log and 'NEO98 HOST drive C:' not in wizard_log}
    drive = out / 'C'
    shutil.rmtree(drive, ignore_errors=True)
    source = win98_c.use_pristine()
    subprocess.run(['cp', '-c', '-R', str(source), str(drive)], check=True)
    registry = out / 'session.registry'
    shutil.copyfile(win98_c.journal(source), registry)
    (out / 'find.ppm').unlink(missing_ok=True)
    steps = [
        '0:wait=Notepad', '0:idle=1000', '1:type=Alpha alpha',
        *alt(1, '46'), '1:wait=#32768', '1:type=a', '1:wait=#32770|Save As', '1:idle=1000',
        '1:type=C:\\RELEASE.TXT', '1:key=0d', '1:idle=1000',
        *alt(2, '46'), '2:wait=#32768', '2:type=o', '2:wait=#32770|Open', '2:idle=1000',
        '2:type=C:\\RELEASE.TXT', '2:key=0d', '2:idle=1000',
        *alt(3, '53'), '3:wait=#32768', '3:type=f', '3:wait=#32770|Find', '3:idle=1000',
        '3:type=alpha', '3:key=0d', '3:idle=500', f'3:shot={out / "find.ppm"}',
        '4:key=1b', '4:menu=quit',
    ]
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_DEADLINE='120', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    with (out / 'host.log').open('w') as log:
        result = subprocess.run([str(binary), '-', r'C:\WINDOWS\FONTS\VGAFIX.FON',
                                 '--system-tree', '--exe', r'C:\WINDOWS\NOTEPAD.EXE',
                                 '--drive-c', str(drive), '--registry', str(registry), '--quiet',
                                 '--crash-report-dir', str(out / 'reports')],
                                cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
    text = (out / 'host.log').read_text(errors='replace')
    saved = drive / 'RELEASE.TXT'
    checks.update({name + ' opened': 'input wait=#32770|' + name + ' reached' in text
                   for name in ('Save As', 'Open', 'Find')})
    checks.update({'host exited': result.returncode == 0,
                   'no wait timed out': 'timed out' not in text,
                   'saved bytes': saved.is_file() and saved.read_bytes() == b'Alpha alpha',
                   'screenshot': (out / 'find.ppm').is_file()})
    # These start a fresh host too, so no previous Win32 process can have
    # populated the font catalogue for the Win16 or DOS entry path.
    for label, program, window in (
        ('win16', r'C:\WINDOWS\SYSTEM\SYSEDIT.EXE', '|System Configuration Editor'),
        ('dos', r'C:\WINDOWS\COMMAND\EDIT.COM', '|EDIT'),
    ):
        env['NEO98_HOST_INPUT'] = f'0:wait={window};1:idle=500;2:menu=quit'
        with (out / (label + '.log')).open('w') as log:
            cold = subprocess.run([str(binary), '-', r'C:\WINDOWS\FONTS\VGAFIX.FON',
                                   '--system-tree', '--exe', program, '--drive-c', str(drive),
                                   '--registry', str(registry), '--quiet',
                                   '--crash-report-dir', str(out / 'reports')],
                                  cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
        content = (out / (label + '.log')).read_text(errors='replace')
        checks[label + ' cold start'] = cold.returncode == 0 and f'input wait={window} reached' in content
    (out / 'results.json').write_text(json.dumps({'checks': checks}, indent=2) + '\n')
    shutil.rmtree(drive)
    for name, okay in checks.items():
        print(('PASS ' if okay else 'FAIL ') + name)
    assert all(checks.values()), out / 'host.log'
    print('BUNDLED NOTEPAD SAVE/OPEN/FIND PASS')


if __name__ == '__main__':
    main()
