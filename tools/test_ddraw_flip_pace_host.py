#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A flip with DDFLIP_NOVSYNC keeps the clock of the display.

Expendable flips with DDFLIP_NOVSYNC without looking at the caps, and its
menu counts a thousand frames until the demo begins: a second when the flag
took the retrace away, about ten on a machine of the game's time. The driver
does not report DDCAPS2_FLIPNOVSYNC, and 120 such flips take the two seconds
they take at 60 pictures a second.

Run through a shared serial test lock with an already built host.
"""
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)
import os
import re
import shutil
import subprocess
import native_app
from ui_clipboard_test_support import ROOT, machine, host

FLIPNOVSYNC = 0x00400000


def main():
    out = ROOT / 'build/ddraw-flip-pace'
    out.mkdir(parents=True, exist_ok=True)
    exe = out / 'FLIPPACE.EXE'
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/ddraw_flip_pace_fixture.c'),
                    '-O1', '-ffreestanding', '-fno-builtin', '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-lddraw', '-luser32', '-lkernel32', '-o', str(exe)], check=True)
    for cpu in native_app.test_engines():
        if shutil.disk_usage(ROOT).free < 8 * 1024**3:
            raise RuntimeError('STOP: fewer than 8 GiB free')
        os.environ['NEO98_CPU'] = cpu
        folder = out / cpu
        try:
            with machine(folder) as drive:
                log = host(folder, drive, r'E:\FLIPPACE.EXE', seconds=20, extra=('--drive-readonly', 'E=' + str(out)))
                assert 'NEO98 HOST exited code=0 ' in log, log[-3000:]
                report = next(drive.glob('[Ff][Ll][Ii][Pp][Pp][Aa][Cc][Ee].[Tt][Xx][Tt]')).read_text()
        finally:
            (folder / 'c.registry').unlink(missing_ok=True)
        found = re.fullmatch(r'caps2=([0-9a-f]{8}) frames=(\d+) ms=(\d+)\s*', report)
        assert found, report
        caps2, frames, elapsed = int(found[1], 16), int(found[2]), int(found[3])
        assert not caps2 & FLIPNOVSYNC, f'the driver reports DDCAPS2_FLIPNOVSYNC: {caps2:08x}'
        # 60 pictures a second; a loaded machine only makes it slower.
        assert elapsed >= frames * 1000 // 60 * 9 // 10, f'{frames} flips with DDFLIP_NOVSYNC took {elapsed} ms'
        print(cpu, f'PASS {frames} flips in {elapsed} ms, caps2 {caps2:08x}', flush=True)


if __name__ == '__main__':
    main()
