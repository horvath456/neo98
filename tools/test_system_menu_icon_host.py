#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The system menu from the icon of the caption and from the taskbar.

A press on the icon of a caption drops the window's system menu below the
icon (DefWindowProc: WM_NCLBUTTONDOWN with HTSYSMENU becomes SC_MOUSEMENU
with HTSYSMENU in its low bits, and the menu mode starts with the system
menu open). A second press on the icon closes the menu; a second press
within the double-click time closes the window too (the menu loop sends
SC_CLOSE). The taskbar's message 0313h, which it sends for a right click on
a window's button with the point of the click, opens the system menu there
with Maximize as its default item. The press opened nothing, and 0313h was
not answered.

tools/system_menu_icon_fixture.c writes down what reaches its window.

    build/analysis-venv/bin/python tools/test_system_menu_icon_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)

import json
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

# The window stands at 40,40; the icon of its caption is inside the frame of
# four pixels, at the left of the caption.
ICON = (52, 52)
TASKBAR_POINT = (150, 150)


def pixels(path):
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    return int(size.split()[0]), body


def changed(first, second, box):
    (width, a), (_, b) = first, second
    left, top, right, bottom = box
    return sum(1 for y in range(top, bottom) for x in range(left, right)
               if a[(y * width + x) * 3:(y * width + x) * 3 + 3] != b[(y * width + x) * 3:(y * width + x) * 3 + 3])


# The MDI frame stands at 40,40: its bar begins below the caption, the icon of
# the maximized child is its first item and the child's three buttons are the
# last ones, at the right end.
MDI_ICON = (52, 72)
MDI_BUTTONS = (386, 64, 438, 82)


def mdi(binary, pristine, out):
    """A maximized MDI child: its icon and buttons in the frame's bar, its
    system menu from the icon, Close from that menu and the double click."""
    drive = out / 'mdi-c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    (drive / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/mdi_system_icon_fixture.c'), '-O1', '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32', '-o', str(drive / 'MDIICON.EXE')],
                   env=neo98.tool_environment(), check=True)
    x, y = MDI_ICON
    steps = ['0:wait=MdiIcon|MdiIcon - [Second]', f'1:shot={out}/mdi-start.ppm', f'1.5:click={x},{y}',
             f'2.5:shot={out}/mdi-open.ppm', '3:type=c', f'4.5:dblclick={x},{y}', '7:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US',
               NEO98_HOST_DEADLINE='30', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_SETTINGS_DIR=str(out / 'mdi-settings'), NEO98_LOG_DIR=str(out / 'mdi-logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    with (out / 'mdi-log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\MDIICON.EXE', '--quiet',
                        '--desktop', '640x480', '--system-tree', str(drive), '--drive-c', str(drive),
                        '--registry', str(out / 'mdi-c.registry'), '--crash-report-dir', str(out / 'reports')],
                       cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=120)
    written = win98_c.path('C:\\MDIICON.TXT', drive)
    lines = written.read_text().splitlines() if written else []
    shutil.rmtree(drive, ignore_errors=True)
    start = pixels(out / 'mdi-start.ppm') if (out / 'mdi-start.ppm').is_file() else None
    opened = pixels(out / 'mdi-open.ppm') if (out / 'mdi-open.ppm').is_file() else None

    def colours(picture, box):
        width, body = picture
        left, top, right, bottom = box
        return {bytes(body[(row * width + column) * 3:(row * width + column) * 3 + 3])
                for row in range(top, bottom) for column in range(left, right)}

    icon_colours = len(colours(start, (44, 64, 62, 82))) if start else 0
    button_colours = colours(start, MDI_BUTTONS) if start else set()
    def black(picture, box):
        width, body = picture
        left, top, right, bottom = box
        return sum(1 for row in range(top, bottom) for column in range(left, right)
                   if body[(row * width + column) * 3:(row * width + column) * 3 + 3] == b'\0\0\0')

    # The client of the frame is as grey as a menu: the menu shows by its text.
    below = black(opened, (44, 84, 170, 230)) - black(start, (44, 84, 170, 230)) if start and opened else 0
    close = ['frame command f060', 'child syscommand f060', 'child close']
    checks = {
        'MDI: the frame wrote what reached it when its last child had gone': len(lines) >= 8,
        f'MDI: the bar shows the icon of the maximized child: {icon_colours} colours': icon_colours >= 4,
        'MDI: the bar shows the child\'s caption buttons, black glyphs on button faces':
            b'\0\0\0' in button_colours and b'\xff\xff\xff' in button_colours,
        f'MDI: a press on the icon drops the child\'s system menu: {below} black pixels of text':
            below > 150 and lines[:1] == ['popup child system=0'],
        'MDI: Close of that menu closes the child': lines[1:4] == close,
        'MDI: a double click on the icon closes the next child': lines[4:5] == ['popup child system=0'] and
            lines[5:8] == close,
    }
    return lines, checks


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('system-menu-icon')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    (drive / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/system_menu_icon_fixture.c'), '-O1', '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32', '-o', str(drive / 'SYSICON.EXE')],
                   env=neo98.tool_environment(), check=True)
    x, y = ICON
    # Seconds from the start, after the window has appeared.
    steps = ['0:wait=SysIcon|SysIcon', f'1:shot={out}/start.ppm', f'1.5:click={x},{y}', f'2.5:shot={out}/open.ppm',
             '3:key=1b', '3.5:key=1b',
             # Two presses a second apart: the second only closes the menu.
             f'4.5:click={x},{y}', f'5.5:click={x},{y}', '6.5:key=1b',
             '7:type=p', f'8:shot={out}/taskbar.ppm', '8.5:key=1b',
             f'9.5:dblclick={x},{y}', '12:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US',
               NEO98_HOST_DEADLINE='40', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\SYSICON.EXE', '--quiet',
                        '--desktop', '640x480', '--system-tree', str(drive), '--drive-c', str(drive),
                        '--registry', str(out / 'c.registry'), '--crash-report-dir', str(out / 'reports')],
                       cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=150)
    written = win98_c.path('C:\\SYSICON.TXT', drive)
    lines = written.read_text().splitlines() if written else []
    shutil.rmtree(drive, ignore_errors=True)
    pictures = {name: pixels(out / f'{name}.ppm') if (out / f'{name}.ppm').is_file() else None
                for name in ('start', 'open', 'taskbar')}
    complete = all(pictures.values())
    below_icon = changed(pictures['start'], pictures['open'], (44, 64, 160, 150)) if complete else 0
    at_point = changed(pictures['start'], pictures['taskbar'], (151, 151, 260, 240)) if complete else 0
    defaults = [line.split('=')[1] for line in lines if line.startswith('popup system')]
    taskbar = lines.index('taskbar 0313h') if 'taskbar 0313h' in lines else -1
    commands = [line for line in lines if line.startswith('syscommand') and line != 'syscommand f090']
    presses = lines.count('syscommand f090')
    checks = {
        'the window wrote what reached it when it closed': bool(lines) and lines[-1] == 'close',
        f'the system menu was opened four times, the third by 0313h: {defaults}':
            len(defaults) == 4 and taskbar >= 0 and lines.index('taskbar 0313h') < [
                i for i, line in enumerate(lines) if line.startswith('popup system')][2],
        f'a press on the icon drops the menu below it: {below_icon} pixels changed': below_icon > 2000,
        f'each press on the icon is SC_MOUSEMENU: {presses} of 3': presses == 3,
        'its default item is Close': defaults[:2] == ['f060', 'f060'],
        'a second press a second later closes only the menu': commands == ['syscommand f060'] and
            lines.index('syscommand f060') > taskbar,
        f'0313h opens the system menu at its point: {at_point} pixels changed': at_point > 2000,
        'its default item there is Maximize': defaults[2:3] == ['f030'],
        'a double click on the icon sends SC_CLOSE and closes the window':
            lines[-2:] == ['syscommand f060', 'close'] and defaults[3:] == ['f060'],
    }
    mdi_lines, mdi_checks = mdi(binary, pristine, out)
    checks.update(mdi_checks)
    (out / 'results.json').write_text(json.dumps({'log': lines, 'mdi': mdi_lines, 'checks': checks}, indent=1))
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    log:', lines, '\n    MDI log:', mdi_lines, '\n    host logs in', out)
    print(f'system menu icon host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
