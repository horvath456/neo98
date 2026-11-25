#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DirectDraw gets the video memory of a released surface back.

The fixture makes and releases surfaces worth twice the video memory, one at
a time, and compares the free count before and after.
"""
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import win98_c


def main():
    out = ROOT / 'build/ddraw-video-memory'
    out.mkdir(parents=True, exist_ok=True)
    exe = out / 'DDVIDMEM.EXE'
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/ddraw_video_memory_fixture.c'),
                    '-O1', '-nostartfiles', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-lddraw', '-luser32', '-lkernel32', '-o', str(exe)], check=True)
    source = win98_c.use_pristine()
    drive = out / 'fixture-c'
    drive.mkdir(exist_ok=True)
    binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build/native-app/neo98-host'
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='30')
    result = subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON',
                             '--system-tree', str(source), '--exe', 'E:\\DDVIDMEM.EXE',
                             '--drive-readonly', 'E=' + str(out), '--drive-c', str(drive),
                             '--no-registry-file', '--quiet'], cwd=ROOT, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=45)
    (out / 'host.log').write_text(result.stdout)
    assert result.returncode == 0 and 'exited code=0' in result.stdout, result.stdout[-2200:]
    assert 'DDRAW VIDEO MEMORY returned' in result.stdout, result.stdout[-2200:]
    print('DDRAW VIDEO MEMORY PASS: released surfaces give their memory back')


if __name__ == '__main__':
    main()
