#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""An EDIT subclassed to draw its own border keeps it.

On Windows the frame of an edit control is painted through the window, as
any frame is: WM_NCPAINT in its thread, DefWindowProc paints the border,
and what a subclass draws over it afterwards through GetWindowDC stays until
the frame has to be painted again (docs/FRAME-PAINT.md). Neo98's composition
painted the frame of its own controls itself, so such a border was gone
with the next composition and the subclass got no WM_NCPAINT to draw it
again.

tools/edit_frame_fixture.c subclasses an edit to frame it in red after
WM_NCPAINT and reads the screen at the frame after its first paint, after a
popup over it went away, after a redraw of the parent with RDW_FRAME, after
new text and after the focus came; EXPECTED is what Windows 95 OSR2 and
Windows 98 SE answer, measured with the same program in a virtual machine.

    build/analysis-venv/bin/python tools/test_edit_frame_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture is built with it; docs/BUILDING.md
        print('SKIP: needs i686-w64-mingw32-gcc')
        _sys.exit(77)

import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402

# Windows: the border red at every step; WM_NCPAINT for the first paint, the
# part that came into view and RDW_FRAME, none for text and focus.
EXPECTED = {}
for step, ncpaint in (('first', 1), ('uncovered', 1), ('redrawn', 1), ('text', 0), ('focus', 0)):
    EXPECTED.update({f'{step}.left': '1', f'{step}.top': '1', f'{step}.ncpaint': str(ncpaint)})
EXPECTED['done'] = '1'


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('edit-frame')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', '-a', '--reflink=auto', f'{pristine}/.', str(drive)], check=True)
    if drive.is_symlink():
        raise SystemExit(f'{drive} is a link')
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/edit_frame_fixture.c'),
                    '-ffreestanding', '-fno-builtin', '-nostartfiles', '-nostdlib', '-Wl,-e,_mainCRTStartup',
                    '-mwindows', '-luser32', '-lkernel32', '-lgdi32', '-o', str(drive / 'EDITFRM.EXE')], check=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='90',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\EDITFRM.EXE', '--quiet',
                        '--desktop', '640x480', '--system-tree', str(drive), '--drive-c', str(drive),
                        '--registry', str(out / 'c.registry'), '--crash-report-dir', str(out / 'reports')],
                       cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=240)
    written = win98_c.path('C:\\EDITFRM.TXT', drive)
    got = dict(line.split('=', 1) for line in (written.read_text(errors='replace').splitlines() if written else [])
               if '=' in line)
    shutil.rmtree(drive, ignore_errors=True)
    checks = {f'{key}: {got.get(key)} (Windows: {value})': got.get(key) == value for key, value in EXPECTED.items()}
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    log:', out / 'log.txt')
    print(f'edit frame host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
