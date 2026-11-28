#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A file the host writes into the drive under a name the guest deleted.

Drive C: is a folder of the host, and other programs of the host write into
it while Windows runs: a file copied in by hand, a file a device of the host
writes for a Windows program. The guest has to see such a file the next time
it asks for the name, also when it deleted a file of that name before and
when it asked for the name in vain before.

tools/host_recreated_file_fixture.c goes through five cases, each a name the
guest then looks at with GetFileAttributes, FindFirstFile (by name and by a
pattern), CreateFile and ReadFile after the test wrote the file on the host:

  1  a name that never existed, asked for in vain
  2  a file the guest wrote and deleted
  3  a file that was there at the start, deleted by the guest
  4  as 2, in a folder the guest made
  5  as 3, in C:\\WINDOWS

once with drive C: as the Windows folder itself and once with a folder of
its own above the Windows folder (the layer that keeps a deleted file of the
folder below as a tombstone in its sidecar). The fixture also measures what
2000 calls of GetFileAttributes and 500 of FindFirstFile cost.

    build/analysis-venv/bin/python tools/test_host_recreated_file.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with it; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)

import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402

CASES = {1: 'HRNEW1.TXT', 2: 'HRNEW2.TXT', 3: 'HRNEW3.TXT', 4: 'HRDIR/HRNEW4.TXT', 5: 'WINDOWS/HRNEW5.TXT'}
CASE_NAMES = {1: 'a name asked for in vain', 2: 'a file the guest wrote and deleted',
              3: 'a file of the start the guest deleted', 4: 'a deleted file in a folder the guest made',
              5: 'a deleted file of the start in C:\\WINDOWS'}


def copy_tree(source, target):
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(Path(source).resolve()), str(target)], check=True)


def session(binary, out, tree, drive, program):
    """Runs the fixture and plays the host's part; the result file's lines."""
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='150',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('ab') as log:
        host = subprocess.Popen([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', program, '--quiet',
                                 '--system-tree', str(tree), '--drive-c', str(drive),
                                 '--registry', str(out / 'c.registry'),
                                 '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                                stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        try:
            for n, name in CASES.items():
                deadline = time.monotonic() + 90
                while not win98_c.path(f'C:\\HRSTEP{n}.MRK', drive):
                    if host.poll() is not None or time.monotonic() > deadline:
                        return None
                    time.sleep(0.05)
                target = drive / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(f'host {n}'.encode())
                (drive / f'HRGO{n}.MRK').write_bytes(b'')
            host.wait(timeout=90)
        finally:
            if host.poll() is None:
                host.kill()
                host.wait()
    written = win98_c.path('C:\\HRRESULT.TXT', drive)
    return written.read_text(errors='replace').splitlines() if written else None


def run(binary, out, pristine, layered):
    """One session: drive C: as the Windows folder itself, or a layer above it."""
    where = out / ('layer' if layered else 'folder')
    shutil.rmtree(where, ignore_errors=True)
    where.mkdir(parents=True)
    tree = where / 'c'
    copy_tree(pristine, tree)
    drive = where / 'upper' if layered else tree
    drive.mkdir(exist_ok=True)
    # The files of the start (cases 3 and 5) are in the Windows folder, the
    # tree below the layer when there is one.
    (tree / 'HRNEW3.TXT').write_bytes(b'start 3')
    windows = win98_c.path('C:\\WINDOWS', tree)
    (windows / 'HRNEW5.TXT').write_bytes(b'start 5')
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/host_recreated_file_fixture.c'), '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-lkernel32', '-luser32',
                    '-o', str(tree / 'HRFIX.EXE')], check=True)
    try:
        lines = session(binary, where, tree, drive, 'C:\\HRFIX.EXE')
    finally:
        shutil.rmtree(tree, ignore_errors=True)
        if layered:
            shutil.rmtree(drive, ignore_errors=True)
    label = 'layer' if layered else 'folder'
    checks = {f'{label}: the fixture went through all cases': lines is not None and len(lines) == len(CASES) + 1}
    cost = None
    for line in lines or []:
        parts = line.split()
        if parts and parts[0] == 'cost':
            cost = [int(p) for p in parts[1:4]]
            continue
        n = int(parts[0])
        attributes, by_name, by_pattern, opened = parts[1], parts[2], parts[3], parts[4]
        content = ' '.join(parts[5:])
        checks[f'{label} {n}, {CASE_NAMES[n]}: GetFileAttributes {attributes}, FindFirstFile by name {by_name}, '
               f'by pattern {by_pattern}, CreateFile {opened}, read "{content}"'] = (
            attributes != 'ffffffff' and by_name == '1' and by_pattern == '1' and opened == '1' and
            content == f'host {n}')
    return checks, cost


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('host-recreated-file')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    checks = {}
    for layered in (False, True):
        result, cost = run(binary, out, pristine, layered)
        checks.update(result)
        if cost:
            print(f"{'layer' if layered else 'folder'}: 2000 GetFileAttributes on a file {cost[0]} ms, "
                  f"on a missing file {cost[1]} ms, 500 FindFirstFile {cost[2]} ms", flush=True)
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    logs:', out)
    print(f'host recreated file: {sum(map(bool, checks.values()))} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
