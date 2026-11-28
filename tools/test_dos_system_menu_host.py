#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The system menu of the MS-DOS window: WINOLDAP's Edit, Toolbar and Properties.

WINOLDAP gives the MS-DOS window's system menu its own items: "&Close" of
its strings (without Alt+F4, which goes to the program), the popup "&Edit"
with Mark, Copy, Paste and Scroll, "&Toolbar" (checked while the strip
shows) and, after a separator, "&Properties" (docs/DOS-WINDOW-MENU.md).
Neo98's window had USER's menu only; the strip's buttons did the work.

tools/dos_system_menu_fixture.c opens C:\\WINDOWS\\COMMAND.COM, reads the
window's system menu from its own process and sends the items' commands:
Toolbar twice, Mark and Escape, Mark with a block and Enter, Scroll and
Escape, Properties, and Paste of "exit" and Enter. EXPECTED is what Windows
95 OSR2 and Windows 98 SE answer, measured with the same program in a virtual
machine. Not compared:

- the states of USER's own items, which Windows sets when the menu opens and
  Neo98 keeps up to date all the time;
- the window's title: Windows calls the window of COMMAND.COM started by
  WinExec "MS-DOS Prompt", Neo98 "COMMAND"; the titles of Mark and Scroll
  are compared with the window's own;
- the text Copy puts on the clipboard: WINOLDAP marks with the mouse of the
  system and ignores the posted clicks, so in Windows the clipboard stays
  empty; here the block holds the prompt;
- what follows the "exit" pasted: Windows keeps the window as "Finished -",
  Neo98 closes it; either shows that the paste reached the program;
- Alt+Space, which opens the system menu unless the Misc page leaves the key
  to the program: the fixture posts it, which is no measure of Windows, and
  only Neo98's answer is checked.

    build/analysis-venv/bin/python tools/test_dos_system_menu_host.py [--no-build]
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

# Windows 95 OSR2 and Windows 98 SE; {title} is the window's own title.
EXPECTED = {
    'started': '1',
    'window': '1',
    'menu.count': '11',
    'menu.0': 'id=f120,"&Restore"',
    'menu.1': 'id=f010,"&Move"',
    'menu.2': 'id=f000,"&Size"',
    'menu.3': 'id=f020,"Mi&nimize"',
    'menu.4': 'id=f030,"Ma&ximize"',
    'menu.5': 'id=0,""',
    'menu.6': 'id=f060,"&Close"',
    'menu.7': 'id=ffffffff,state=10,text="&Edit"',
    'menu.7.count': '4',
    'menu.7.0': 'id=e002,"Mar&k"',
    'menu.7.1': 'id=e000,"Cop&y|Enter"',
    'menu.7.2': 'id=e001,"&Paste"',
    'menu.7.3': 'id=e004,"Scro&ll"',
    'menu.8': 'id=e00e,state=8,text="&Toolbar"',
    'menu.9': 'id=0,""',
    'menu.10': 'id=e005,state=0,text="&Properties"',
    'toolbar1': '-28,state=0',
    'toolbar2': '0,state=8',
    'mark': '"Mark - {title}"',
    'unmarked': '"{title}"',
    'selected': '"Mark - {title}"',
    'copied': '"{title}"',
    'scroll': '"Scroll - {title}"',
    'unscrolled': '"{title}"',
    'properties': '"{title} Properties"',
    'properties.closed': '1',
    'done': '1',
}


def compared(key, value):
    """A line without the parts that are not compared: the state of USER's
    own items."""
    if not key.startswith('menu.') or key.endswith('count') or key in ('menu.7', 'menu.8', 'menu.10'):
        return value
    fields = dict(part.split('=', 1) for part in value.split(',', 2))
    return f'id={fields.get("id")},{fields.get("text")}'


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('dos-system-menu')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', '-a', '--reflink=auto', f'{pristine}/.', str(drive)], check=True)
    if drive.is_symlink():
        raise SystemExit(f'{drive} is a link')
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/dos_system_menu_fixture.c'),
                    '-ffreestanding', '-fno-builtin', '-nostartfiles', '-nostdlib', '-Wl,-e,_mainCRTStartup',
                    '-mwindows', '-luser32', '-lkernel32', '-lgdi32', '-o', str(drive / 'DOSMENU.EXE')], check=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='150',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\DOSMENU.EXE', '--quiet',
                        '--desktop', '800x600', '--system-tree', str(drive), '--drive-c', str(drive),
                        '--registry', str(out / 'c.registry'), '--crash-report-dir', str(out / 'reports')],
                       cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=300)
    written = win98_c.path('C:\\DOSMENU.TXT', drive)
    got = dict(line.split('=', 1) for line in (written.read_text(errors='replace').splitlines() if written else [])
               if '=' in line)
    shutil.rmtree(drive, ignore_errors=True)
    title = got.get('title', '""').strip('"')
    checks = {}
    for key, value in EXPECTED.items():
        value = value.replace('{title}', title)
        mine = got.get(key)
        same = mine is not None and compared(key, mine) == value
        label = f'{key}: as in Windows'
        checks[label if same else f'{label}\n      got      {mine}\n      expected {value}'] = same
    # Alt+Space: WINOLDAP keeps the key for the system menu unless the Misc
    # page leaves it to the program. Posted keys are no measure of what
    # Windows does with the key of the keyboard, so this is Neo98's own check.
    checks[f'Alt+Space opens the system menu ({got.get("altspace.menu")})'] = got.get('altspace.menu') == '1'
    clipboard = got.get('clipboard', '')
    checks[f'Mark and Enter copy the block with the prompt: {clipboard}'] = clipboard.startswith('"C:\\WINDOWS>')
    pasted = got.get('pasted.closed') == '1' or got.get('pasted', '').startswith('"Finished - ')
    checks[f'Paste typed "exit": the window closed or is finished ({got.get("pasted.closed")}, '
           f'{got.get("pasted")})'] = pasted
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    log:', out / 'log.txt')
    print(f'dos system menu host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
