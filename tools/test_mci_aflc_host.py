#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""unsupported AFLC open matches Win98; the next MCI open still works.

Run through a shared serial test lock. Requires the pinned Virtual Chess shareware
corpus, or --film pointing to its unmodified AVI/TITUS.AVI. No codecs or game
files are changed. Every engine gets a temporary private C: clone.
"""
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: compares the FEX and Unicorn engines of macOS')  # macOS only; skipped elsewhere
        _sys.exit(77)
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import neo98


from external_paths import external_path
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    parser.add_argument('--film', type=Path)
    args = parser.parse_args()
    args.film = args.film or external_path('NEO98_GAME_CORPUS') / 'prepared/virtualchess/VCWIN95/AVI/TITUS.AVI'
    from test_support import require_file
    require_file(args.film)
    out = ROOT / 'build/mci-aflc'
    out.mkdir(exist_ok=True)
    film_hash = hashlib.sha256(args.film.read_bytes()).hexdigest()
    assert film_hash == '8ea308bc8377109cc0237e158b7d575a61a7b0caa260304847c6093f45464085', \
        'Expected the unmodified Virtual Chess TITUS.AVI'
    exe = out / 'PROBE.EXE'
    subprocess.run(['i686-w64-mingw32-gcc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-nostdlib', '-Wl,--entry,_mainCRTStartup', '-Wl,--subsystem,windows:4.0',
                    str(ROOT / 'tools/mci_aflc_fixture.c'), '-o', str(exe),
                    '-lwinmm', '-luser32', '-lkernel32'], env=neo98.tool_environment(), check=True)
    records = []
    for cpu in ('fex', 'unicorn'):
        if shutil.disk_usage(out).free < 6 * 1024**3:
            raise RuntimeError('STOP: fewer than 6 GiB free')
        with tempfile.TemporaryDirectory(prefix='private-', dir=out) as temporary:
            drive = Path(temporary) / 'c'
            source = (__import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or __import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or ROOT / 'build/win98-c'))).resolve()
            subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(source), str(drive)], check=True)
            assert not drive.is_symlink() and drive.resolve() != source
            shutil.copyfile(args.film, drive / 'TITUS.AVI')
            shutil.copyfile(exe, drive / 'PROBE.EXE')
            env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                       NEO98_CPU=cpu, NEO98_HOST_DEADLINE='30',
                       NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
            for key in ('NEO98_HOST_INPUT', 'NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
                env.pop(key, None)
            command = [str(args.host.resolve()), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                       '--exe', r'C:\PROBE.EXE', '--quiet', '--system-tree', str(drive),
                       '--drive-c', str(drive), '--no-registry-file']
            log_path = out / (cpu + '.log')
            with log_path.open('w') as log:
                process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
                try:
                    code = process.wait(timeout=50)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=20)
                    raise
            text = log_path.read_text(errors='replace')
            report = (drive / 'AFLC.TXT').read_text()
            (out / (cpu + '.txt')).write_text(report)
            values = dict(line.split('=', 1) for line in report.splitlines())
            assert values == {'vidc.aflc': '<absent>', 'command': '6', 'string': '6',
                              'empty': '0', 'close': '0'}, values
            assert code == 0 and 'NEO98 HOST exited code=0' in text, text[-2000:]
            records.append({'cpu': cpu, 'status': 'PASS', 'values': values})
            print(cpu, 'PASS: command/string return 6; subsequent open/close return 0', flush=True)
    assert hashlib.sha256(args.film.read_bytes()).hexdigest() == film_hash
    (out / 'results.json').write_text(json.dumps({
        'host_sha256': hashlib.sha256(args.host.read_bytes()).hexdigest(),
        'film_sha256': film_hash, 'fixture_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
        'tests': records}, indent=2) + '\n')


if __name__ == '__main__':
    main()
