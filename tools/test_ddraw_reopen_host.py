#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Cached session HAL callbacks must work in later processes.

The parent retains a DirectDraw primary surface. Three children in succession
create windowed primary surfaces with clippers, fill a system-memory back
buffer, blit it and verify the actual green client pixel with GetPixel.
Every child releases its objects and closes its window. Before the fix the
first child's Blt failed because its process had no local HAL attachment.
"""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import win98_c


def main():
    out = ROOT / 'build/ddraw-reopen'
    out.mkdir(parents=True, exist_ok=True)
    exe = out / 'DDREUSE.EXE'
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/ddraw_reopen_fixture.c'),
                    '-O1', '-nostartfiles', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-lddraw', '-lgdi32', '-luser32', '-lkernel32', '-o', str(exe)], check=True)
    source = win98_c.use_pristine()
    drive = out / 'fixture-c'
    drive.mkdir(exist_ok=True)
    binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build/native-app/neo98-host'
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT='', NEO98_HOST_MENU='8:quit', NEO98_HOST_DEADLINE='15')
    result = subprocess.run([str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                             '--system-tree', str(source), '--exe', 'E:\\DDREUSE.EXE',
                             '--drive-readonly', 'E=' + str(out), '--drive-c', str(drive),
                             '--no-registry-file', '--quiet'], cwd=ROOT, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=25)
    (out / 'host.log').write_text(result.stdout)
    assert result.returncode == 0 and 'exited code=0' in result.stdout, result.stdout[-2200:]
    assert result.stdout.count('DDREOPEN child pixels PASS') == 3, result.stdout[-2200:]
    print('DDRAW REOPEN PASS: three later processes draw verified pixels through the cached HAL')


if __name__ == '__main__':
    main()
