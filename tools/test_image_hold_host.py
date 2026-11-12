#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The file of a running program and of a loaded DLL cannot be written or deleted.

KERNEL32 keeps those files open. InstallShield 5 depends on it: its deleter
_ISDEL.EXE opens Setup's program (C:\\WINDOWS\\TEMP\\_INS5176._MP) for writing
every three seconds and deletes Setup's temporary files, the helper DLL of the
program groups among them, as soon as the open succeeds. Without the hold the
Re-Volt installer lost _WUTL951.DLL in the middle of the installation and
called through a null pointer.

The fixture checks its own file, a DLL before, while and after it is loaded,
and a second program while it runs and after it has ended.

Run through a shared serial test lock with an already built host.
"""
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)
import os
import shutil
import subprocess
import native_app
from ui_clipboard_test_support import ROOT, machine, host


def main():
    out = ROOT / 'build/image-hold'
    out.mkdir(parents=True, exist_ok=True)
    exe = out / 'IMGHOLD.EXE'
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/image_hold_fixture.c'),
                    '-O1', '-ffreestanding', '-fno-builtin', '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-luser32', '-lkernel32', '-o', str(exe)], check=True)
    for cpu in native_app.test_engines():
        if shutil.disk_usage(ROOT).free < 8 * 1024**3:
            raise RuntimeError('STOP: fewer than 8 GiB free')
        os.environ['NEO98_CPU'] = cpu
        folder = out / cpu
        report = ''
        try:
            with machine(folder) as drive:
                log = host(folder, drive, r'E:\IMGHOLD.EXE', seconds=40, extra=('--drive-readonly', 'E=' + str(out)))
                for path in drive.glob('[Ii][Mm][Gg][Hh][Oo][Ll][Dd].[Tt][Xx][Tt]'):
                    report = path.read_text()
                left = sorted(path.name for path in drive.glob('[Ii][Mm][Gg][Hh][Oo][Ll][Dd]*'))
        finally:
            (folder / 'c.registry').unlink(missing_ok=True)
        assert 'NEO98 HOST exited code=0 ' in log, report + log[-3000:]
        for line in ('self write refused=1 error=32', 'self delete refused=1 error=5', 'self rename refused=1 error=5',
                     'loaded dll refused=1 error=32', 'freed dll deleted=1', 'running program refused=1 error=32',
                     'ended program deleted=1'):
            assert line in report, report
        # The DLL and the second program are gone; the first program ran until the end.
        assert [name.upper() for name in left] == ['IMGHOLD.TXT', 'IMGHOLD1.EXE'], left
        print(cpu, 'PASS', flush=True)


if __name__ == '__main__':
    main()
