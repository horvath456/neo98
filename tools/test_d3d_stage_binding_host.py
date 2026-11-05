#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Device3 texture binding and legacy blend state with FEX and Unicorn. Use serial.py."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import struct


def guard(minimum=8):
    subprocess.run(['df', '-h', '/System/Volumes/Data'], check=True, stdin=subprocess.DEVNULL)
    if shutil.disk_usage(ROOT).free < minimum * 1024**3:
        raise RuntimeError(f'STOP: fewer than {minimum} GiB free')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--reference-rgb', action='store_true')
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    parser.add_argument('--cpu', choices=('fex', 'unicorn'), action='append')
    args = parser.parse_args()
    guard(8.6)
    print('host sha256:', hashlib.sha256(args.host.read_bytes()).hexdigest(), flush=True)
    out = ROOT / ('build/d3d-stage-binding-rgb' if args.reference_rgb else 'build/d3d-stage-binding')
    out.mkdir(exist_ok=True)
    exe = out / 'EXECUTE.EXE'
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/d3d_stage_binding_fixture.c'),
                    *(['-DREFERENCE_RGB'] if args.reference_rgb else []), '-O1', '-nostartfiles', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-lddraw', '-ldxguid', '-luuid', '-luser32', '-lkernel32',
                    '-o', str(exe)], check=True, stdin=subprocess.DEVNULL)
    for engine in args.cpu or ('fex', 'unicorn'):
        guard()
        active = subprocess.run(['ps', '-axo', 'pid=,comm='], capture_output=True,
                                text=True, stdin=subprocess.DEVNULL)
        active.check_returncode()
        hosts = [row for row in active.stdout.splitlines() if row.split() and Path(row.split()[-1]).name in ('neo98-host', 'sweep-before-b6276')]
        if hosts:
            __import__('test_support').skip('exclusive host access is unavailable: another neo98-host is active: ' + '; '.join(hosts))
        drive = out / f'c-{engine}'
        assert not drive.exists() and not drive.is_symlink()
        (out / 'EXECUTE.BIN').unlink(missing_ok=True)
        process = None
        try:
            subprocess.run(['cp', '-c', '-R', str((ROOT / 'build/win98-c').resolve()) + '/',
                            str(drive)], check=True, stdin=subprocess.DEVNULL)
            assert drive.is_dir() and not drive.is_symlink()
            guard()
            (out / 'runtime-source').mkdir(exist_ok=True)
            for name in ('DDRAW.DLL', 'D3DIM.DLL'):
                source = next(p for p in drive.rglob('*') if p.is_file() and str(p.relative_to(drive)).replace('\\', '/').lower() == 'windows/system/' + name.lower())
                shutil.copyfile(source, out / 'runtime-source' / name)
            env = dict(os.environ, NEO98_CPU=engine, SDL_VIDEODRIVER='dummy',
                       SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='10',
                       NEO98_HOST_INPUT='3:key=0d;5:menu=quit', NEO98_HOST_MENU='',
                       NEO98_SETTINGS_DIR=str(out / f'settings-{engine}'),
                       NEO98_LOG_DIR=str(out / f'logs-{engine}'), NEO98_HOST_PASTEBOARD='unique')
            log_path = out / (engine + ('-before' if args.baseline else '-after') + '.log')
            with log_path.open('w') as log:
                process = subprocess.Popen([str(args.host.resolve()), '-',
                    'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'E:\\' + exe.name,
                    '--system-tree', str(drive), '--drive-c', str(drive),
                    '--drive', 'E=' + str(exe.parent), '--no-registry-file',
                    '--quiet', '--offline', '--crash-report-dir', str(out / f'reports-{engine}')],
                    cwd=ROOT, env=env, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=20)
                    raise AssertionError(f'{engine}: host deadline exceeded')
            log = log_path.read_text(errors='replace')
            rows = list(struct.iter_unpack('<2I', (out / 'EXECUTE.BIN').read_bytes()))
            print(f'{engine}: {rows}', flush=True)
            import json
            result = {'host_sha256': hashlib.sha256(args.host.read_bytes()).hexdigest(),
                      'fixture_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
                      'reference_rgb': args.reference_rgb, 'cpu': engine, 'rows': rows,
                      'source_sha256': hashlib.sha256((ROOT/'tools/d3d_stage_binding_fixture.c').read_bytes()).hexdigest(),
                      'runtime_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (out/'runtime-source').glob('*.DLL')}}
            (out / (engine + ('-before.json' if args.baseline else '-after.json'))).write_text(json.dumps(result, indent=2)+'\n')
            assert process.returncode == 0 and 'NEO98 HOST exited code=0 ' in log, log[-4000:]
            assert len(rows) == 8, rows
            expected = [0x7e0,0x7e0,0xf800,0x7e0]
            got = [dict(rows)[200+i] for i in range(4)]
            assert [dict(rows)[100+i] for i in range(4)] == [4] * 4, rows
            if args.baseline:
                assert got == [0xf800] * 4, rows
                print('BASELINE', engine, got, flush=True)
            else:
                assert got == expected, rows
                print('PASS', engine, got, flush=True)
        finally:
            if process is None or process.poll() is not None:
                assert not drive.is_symlink()
                if drive.exists():
                    shutil.rmtree(drive)


if __name__ == '__main__':
    main()
