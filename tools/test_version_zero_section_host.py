#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""zero VirtualSize version resource with FEX and Unicorn. Use serial.py."""
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc', 'i686-w64-mingw32-windres'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]


def guard():
    subprocess.run(['df', '-h', ('/System/Volumes/Data' if __import__('sys').platform == 'darwin' else '/')], check=True, stdin=subprocess.DEVNULL)
    if shutil.disk_usage(ROOT).free < 8 * 1024**3:
        raise RuntimeError('STOP: fewer than 8 GiB free')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    parser.add_argument('--cpu', choices=('fex', 'unicorn'), action='append')
    args = parser.parse_args()
    guard()
    out = ROOT / 'build/version-zero-section'
    out.mkdir(exist_ok=True)
    exe = out / 'VSZERO.EXE'
    if shutil.disk_usage(ROOT).free < 8.6 * 1024**3:
        raise RuntimeError('STOP build: fewer than 8.6 GiB free')
    resource = out / 'version.o'
    subprocess.run(['i686-w64-mingw32-windres', str(ROOT/'tools/version_zero_section_fixture.rc'),
                    '-o',str(resource)],check=True,stdin=subprocess.DEVNULL)
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/version_zero_section_fixture.c'),
                    '-O1', '-nostartfiles', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-lversion', '-lkernel32', str(resource),
                    '-o', str(exe)], check=True, stdin=subprocess.DEVNULL)
    import pefile, struct
    pe=pefile.PE(str(exe)); raw=bytearray(exe.read_bytes())
    section=next(x for x in pe.sections if x.Name.rstrip(b'\0')==b'.rsrc')
    struct.pack_into('<I',raw,section.get_file_offset()+8,0)
    exe.write_bytes(raw)
    for engine in args.cpu or ('fex', 'unicorn'):
        guard()
        active = subprocess.run(['ps', '-axo', 'pid=,comm='], capture_output=True,
                                text=True, stdin=subprocess.DEVNULL)
        active.check_returncode()
        hosts = [row for row in active.stdout.splitlines() if row.split() and Path(row.split()[-1]).name in ('neo98-host', 'sweep-before-b6279')]
        if hosts:
            __import__('test_support').skip('exclusive host access is unavailable: another neo98-host is active: ' + '; '.join(hosts))
        drive = out / f'c-{engine}'
        assert not drive.exists() and not drive.is_symlink()
        process = None
        try:
            subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str((__import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or __import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or ROOT / 'build/win98-c'))).resolve()) + '/',
                            str(drive)], check=True, stdin=subprocess.DEVNULL)
            assert drive.is_dir() and not drive.is_symlink()
            guard()
            env = dict(os.environ, NEO98_CPU=engine, SDL_VIDEODRIVER='dummy',
                       SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='20',
                       NEO98_HOST_INPUT='15:menu=quit', NEO98_HOST_MENU='',
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
            if args.baseline:
                assert 'NEO98 HOST exited code=11 ' in log, log[-4000:]
                print(f'PASS {engine}: reproduced missing version resource with zero VirtualSize', flush=True)
            else:
                assert process.returncode == 0 and 'NEO98 HOST exited code=0 ' in log, log[-4000:]
                print(f'PASS {engine}: VERSION.DLL reads the raw-backed resource with zero VirtualSize', flush=True)
        finally:
            if process is None or process.poll() is not None:
                assert not drive.is_symlink()
                if drive.exists():
                    shutil.rmtree(drive)


if __name__ == '__main__':
    main()
