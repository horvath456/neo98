#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""GetClipboardFormatName of a 16-bit program in the host (USER.146).

tools/clipboard_name_win16_fixture.asm registers a format and asks for its
name twice: into a buffer of 5 characters and into one of 32. The count is
the word behind the far pointer of the buffer; read from the pointer itself
(the offset of the buffer) the short call got the whole name.

    build/analysis-venv/bin/python tools/test_win16_clipboard_name_host.py [--no-build]
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win16_fixture  # noqa: E402
import win98_c  # noqa: E402


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('win16-clipboard-name')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', '-c', '-R', str(pristine.resolve()), str(drive)], check=True)
    win16_fixture.assemble(ROOT / 'tools/clipboard_name_win16_fixture.asm', out / 'CF16.EXE', 'CF16',
                           neo98.tool_environment())
    shutil.copyfile(out / 'CF16.EXE', drive / 'CF16.EXE')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='60',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\CF16.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=180)
    written = win98_c.path('C:\\CF16OUT.TXT', drive)
    data = written.read_bytes() if written else b''
    shutil.rmtree(drive, ignore_errors=True)
    checks = {
        'the program wrote its results': len(data) == 34,
        'a buffer of 5 gets 4 characters and the end': data[:1] == b'E' and data[2:18] == b'NEO9\0' + b'.' * 11,
        'a buffer of 32 gets the whole name': data[1:2] == b'L' and data[18:30] == b'NEO98FORMAT\0',
    }
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    result:', data, '\n    log:', out / 'log.txt')
    print(f'win16 clipboard name host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
