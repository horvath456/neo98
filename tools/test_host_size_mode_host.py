#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The host's own size as the stored display mode gives the desktop back to the host.

neo98-kms gives the desktop the size 1024x700 (--desktop, as seamless mode
gives it the monitor's), over a private copy of drive C:. The child process
of tools/host_size_mode_fixture.c stores 800x600 with
ChangeDisplaySettings(CDS_UPDATEREGISTRY) and then 1024x700 again and ends;
its parent then finds the screen and the mode of the registry at the host's
1024x700 again, not at 800x600 with the host's size as a mode of the child.

    build/analysis-venv/bin/python tools/test_host_size_mode_host.py [--no-build]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
if __name__ == '__main__':
    import shutil as _shutil
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)
import argparse
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import neo98  # noqa: E402
import native_app  # noqa: E402
import win98_c  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/host-size-mode-test'


def rows(path):
    blob = path.read_bytes() if path.is_file() else b''
    if len(blob) < 4:
        return None, []
    return struct.unpack_from('<I', blob)[0], list(struct.iter_unpack('<3I', blob[4:]))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host()
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    exe = WORK / 'HOSTSIZE.EXE'
    subprocess.run(['i686-w64-mingw32-gcc', '-O1', '-ffreestanding', '-fno-builtin', '-nostdlib',
                    '-Wl,--entry,_mainCRTStartup', '-Wl,--no-insert-timestamp', '-o', str(exe),
                    str(ROOT / 'tools/host_size_mode_fixture.c'), '-luser32', '-lkernel32'],
                   env=neo98.tool_environment(), check=True, stdin=subprocess.DEVNULL)
    drive = WORK / 'c'
    pristine = win98_c.use_pristine()
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
    journal = win98_c.journal(pristine)
    if journal.is_file():
        shutil.copy2(journal, win98_c.journal(drive))
    shutil.copyfile(exe, drive / 'HOSTSIZE.EXE')
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KMS_OUTPUT='image:1024x700')
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD', 'NEO98_HOST_INPUT'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(BINARY), '--drive-c', str(drive), '--desktop', '1024x700',
                                 '--exe', 'C:\\HOSTSIZE.EXE', '--time-limit', '40'], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=100)
        (WORK / 'log.txt').write_text(result.stdout + result.stderr)
        child_code, child = rows(drive / 'HOSTSIZ2.BIN')
        code, parent = rows(drive / 'HOSTSIZE.BIN')
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        win98_c.journal(drive).unlink(missing_ok=True)
    named = dict(zip(['screen before', 'child started', 'child waited for', 'child ended well',
                      'screen width after', 'screen height after', 'registry mode read',
                      'registry width', 'registry height'], parent))
    checks = {
        'the child stored 800x600 and then the host size': child_code == 0 and len(child) == 5,
        'the desktop starts at the host size': named.get('screen before', (0, 0, 1))[1] == 1024,
        'the child ran and ended': all(named.get(k, (0, 0, 1))[1] == named.get(k, (0, 1, 0))[2]
                                       for k in ('child started', 'child waited for', 'child ended well')),
        'the desktop has the host size again after the child':
            all(named.get(k, (0, 0, 1))[1] == named.get(k, (0, 1, 0))[2]
                for k in ('screen width after', 'screen height after')),
        'the mode of the registry is the host size':
            all(named.get(k, (0, 0, 1))[1] == named.get(k, (0, 1, 0))[2]
                for k in ('registry mode read', 'registry width', 'registry height')),
    }
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    if not all(checks.values()):
        print('    parent:', code, parent, '\n    child:', child_code, child, '\n    log:', WORK / 'log.txt')
        raise SystemExit(f'{sum(not v for v in checks.values())} of {len(checks)} checks failed')
    print(f'host size mode: all {len(checks)} checks passed')


if __name__ == '__main__':
    main()
