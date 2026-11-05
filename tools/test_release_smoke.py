#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Release smoke tests on private C: clones, with FEX and Unicorn.

Run under the shared test lock. --source is read only. --settings-source is
optional and is copied, with all drive paths replaced, before app migration.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(command, env, log):
    with log.open('w') as output:
        process = subprocess.Popen(list(map(str, command)), env=env, stdin=subprocess.DEVNULL,
                                   stdout=output, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=300)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait()  # No SIGKILL on ARM64 hosts.
            raise RuntimeError('Host exceeded smoke deadline; terminated')
    if code:
        raise RuntimeError(f'Host exited with {code}; see {log.name}')
    return log.read_text(errors='replace')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--settings-source', type=Path)
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    parser.add_argument('--app', type=Path, default=ROOT / 'build/Neo98.app/Contents/MacOS/Neo98')
    parser.add_argument('--engine', choices=('fex', 'unicorn'), action='append')
    parser.add_argument('--program', choices=('NOTEPAD.EXE', 'EXPLORER.EXE', 'MSPAINT.EXE', 'SYSEDIT.EXE', 'app'), action='append')
    args = parser.parse_args()
    out = ROOT / 'build/release-smoke'
    out.mkdir(parents=True, exist_ok=True)
    results = []
    for engine in (args.engine or ('fex', 'unicorn')):
        for program, window in (('NOTEPAD.EXE', 'Notepad'), ('EXPLORER.EXE', 'Shell_TrayWnd'), ('MSPAINT.EXE', 'MSPaintApp'), ('SYSEDIT.EXE', '|System Configuration Editor'), ('app', 'Shell_TrayWnd')):
            if args.program and program not in args.program:
                continue
            case = out / f'{engine}-{program}'
            if case.exists(): shutil.rmtree(case)
            case.mkdir()
            drive = case / 'c'
            subprocess.run(['cp', '-c', '-R', str(args.source.resolve()), str(drive)], check=True)
            assert drive.is_dir() and not drive.is_symlink()
            home = case / 'home'; home.mkdir()
            legacy = bytes((65, 82, 77, 57, 56)).decode()
            old = home / 'Library/Application Support' / legacy
            old.mkdir(parents=True)
            text = args.settings_source.read_text() if args.settings_source else 'scale = 2\n'
            # Retain UI settings, never the user's drive, game or external paths.
            retained = [line for line in text.splitlines() if line.split('=', 1)[0].strip() in
                        ('scale', 'fullscreen', 'pause.unfocused', 'wheel.focus', 'wheel.log')]
            (old / 'settings.ini').write_text('\n'.join(retained) + f'\nmachine = {drive}\nonline = 0\nseamless = 0\n')
            env = dict(os.environ, HOME=str(home), SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy',
                       NEO98_CPU=engine, NEO98_LOG_TO_FILE='0', NEO98_HOST_DEADLINE='240',
                       NEO98_HOST_INPUT=f'0:wait={window};1:idle=800;2:tops=1;2:shot={case}/screen.ppm;3:menu=quit')
            for key in ('NEO98_SETTINGS_DIR', 'NEO98_LOG_DIR', 'NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU',
                        'NEO98_SETTINGS_FILE', 'NEO98_HOST_INPUT_ONCE', 'NEO98_GAME_MACHINE'):
                env.pop(key, None)
            if program == 'app':
                command = [args.app]
            else:
                guest = {'MSPAINT.EXE': r'C:\Program Files\Accessories\MSPAINT.EXE', 'SYSEDIT.EXE': r'C:\WINDOWS\SYSTEM\SYSEDIT.EXE'}.get(program, 'C:\\WINDOWS\\' + program)
                command = [args.host, '-', r'C:\WINDOWS\FONTS\VGAFIX.FON', '--quiet',
                           '--system-tree', drive, '--exe', guest, '--drive-c', drive,
                           '--no-registry-file', '--crash-report-dir', case / 'reports']
            try:
                log = run(command, env, case / 'host.log')
                assert f'input wait={window} reached' in log, f'{window} did not appear'
                assert 'NEO98 HOST failed' not in log
                if program == 'app':
                    migrated = home / 'Library/Application Support/Neo98/settings.ini'
                    assert migrated.is_file() and old.is_symlink()
                    assert all(line in migrated.read_text() for line in retained)
                results.append({'engine': engine, 'program': program, 'passed': True})
                print(f'PASS {engine} {program}', flush=True)
            finally:
                shutil.rmtree(drive)
    (out / ('results-' + '-'.join(args.engine or ['all']) + '.json')).write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__': main()
