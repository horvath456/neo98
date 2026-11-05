#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Test context menus on the desktop, icons, taskbar and Notepad caption.

Check visible #32768 windows, TrackPopupMenu, hover highlighting, command execution and dismissal by Escape or outside clicks. Compare images before and after dismissal. For Notepad, verify system-menu geometry and that client-area right clicks do not open it."""
import re
import shutil
import subprocess
import sys
from pathlib import Path

from native_app import ROOT, build_fast
from app_programs import application
import first_logon
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

# Click targets: desktop center, the upper-left icon and the taskbar gap
# between Quick Launch and the window list.
DESKTOP = (400, 300)
ICON = (40, 40)
TASKBAR = (162, 585)
# Empty desktop space below and to the right of the desktop menu.
OUTSIDE = (700, 520)


def pixels(path):
    magic, dimensions, maximum, body = path.read_bytes().split(b'\n', 3)
    assert magic == b'P6', magic
    return dimensions.decode(), body


def differing(a, b):
    first, one = pixels(a)
    second, two = pixels(b)
    assert first == second, (first, second)
    return sum(1 for x, y in zip(one, two) if x != y)


def visible_menus(log):
    """Visible menu windows from the final window report."""
    found = []
    for line in log.splitlines():
        if not line.startswith('PROBE_WINDOW'):
            continue
        klass = re.search(r'class=([0-9a-f]*)', line)
        if not klass or re.search(r'visible=(\d)', line).group(1) != '1':
            continue
        if bytes.fromhex(klass.group(1)).decode('latin1') == '#32768':
            found.append(line)
    return found


# Headless Notepad occupies 64,55 with size 672x480; its client starts at
# y=98. This point hits the caption between the system menu and caption
# buttons.
NOTEPAD_CAPTION = (400, 66)
NOTEPAD_CLIENT = (400, 300)


def window_rects(log, want):
    """Return visible windows of a class from the final window report."""
    found = []
    for line in log.splitlines():
        if not line.startswith('PROBE_WINDOW'):
            continue
        fields = dict(part.split('=', 1) for part in line.split(' ') if '=' in part)
        if fields.get('visible') != '1':
            continue
        if bytes.fromhex(fields.get('class', '')).decode('latin1') != want:
            continue
        found.append({k: int(fields[k]) for k in ('hwnd', 'x', 'y', 'w', 'h')})
    return found


def caption_menu():
    """Right-clicking Notepad's title bar opens the system menu."""
    out = ROOT / 'build/context-menu-caption'
    out.mkdir(exist_ok=True)
    drive = out / 'drive-c'
    shutil.rmtree(drive, ignore_errors=True)
    drive.mkdir(parents=True)
    shots = {name: out / (name + '.ppm') for name in ('before', 'menu', 'closed', 'client')}
    x, y = NOTEPAD_CAPTION
    cx, cy = NOTEPAD_CLIENT
    lines = ['wait-idle', 'wait 2000', f'shot {shots["before"]}',
             f'press {x} {y} 2', f'release {x} {y} 2', 'wait 2000', f'shot {shots["menu"]}',
             'key 0x1b', 'wait 2000', f'shot {shots["closed"]}',
             # The same button in the client area must not open a system
             # menu.
             f'press {cx} {cy} 2', f'release {cx} {cy} 2', 'wait 2000', f'shot {shots["client"]}',
             # Leave the final menu open so the window report includes its
             # position and dimensions.
             f'press {x} {y} 2', f'release {x} {y} 2', 'wait 2000']
    script = out / 'clicks.txt'
    script.write_text('\n'.join(lines) + '\n')
    runner = build_fast('probe_application.cpp', 'probe-smoke')
    command = [str(runner), str(ROOT / 'build/programs' / application('notepad')['program']),
               str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--drive-c', str(drive),
               '--time-limit', '60', '--click-script', str(script), '--click-deadline', '30000']
    result = subprocess.run(command, capture_output=True, text=True, timeout=900)
    log = result.stdout + result.stderr
    (out / 'run.log').write_text(log)
    result.check_returncode()
    if 'PROBE_SCRIPT_FAILED' in log:
        raise SystemExit('\n'.join(l for l in log.splitlines() if 'PROBE_SCRIPT' in l))
    counts = [int(re.search(r'windows=(\d+)', line).group(1))
              for line in log.splitlines()
              if line.startswith('PROBE_SCRIPT index=') and ' step=shot ' in line]
    if len(counts) != 4:
        raise SystemExit(f'Expected four frames, got {len(counts)}: {counts}')
    baseline, opened, closed, client = counts
    menus = window_rects(log, '#32768')
    report = {'windows': counts, 'menus_at_end': menus,
              'changed': {'open': differing(shots['before'], shots['menu']),
                          'closed': differing(shots['before'], shots['closed']),
                          'client': differing(shots['before'], shots['client'])}}
    if opened != baseline + 1:
        raise SystemExit(f'No menu after right-clicking the title bar: {counts}')
    if closed != baseline:
        raise SystemExit(f'Escape did not dismiss the menu: {counts}')
    if client != baseline:
        raise SystemExit(f'Right-clicking the client area unexpectedly opens a menu: {counts}')
    if report['changed']['open'] < 3000:
        raise SystemExit(f"No menu visible, only {report['changed']['open']} pixels")
    if report['changed']['closed'] or report['changed']['client']:
        raise SystemExit(f"the image was not restored: {report['changed']}")
    if len(menus) != 1:
        raise SystemExit(f'expected exactly one menu at the end: {menus}')
    menu = menus[0]
    # The system menu has seven rows: Restore, Move, Size, Minimize,
    # Maximize, separator, Close. At Windows 98 row heights it exceeds 100
    # pixels.
    if menu['h'] < 100:
        raise SystemExit(f'menu is too short for the system entries: {menu}')
    if not (x - 8 <= menu['x'] <= x + 8) or not (y - 8 <= menu['y'] <= y + 8):
        raise SystemExit(f'menu is not at the pointer position {(x, y)}: {menu}')
    print('CONTEXT MENU CAPTION PASS ' + repr(report))
    (out / 'results.json').write_text(repr(report) + '\n')


def main():
    out = ROOT / 'build/context-menu-shell'
    out.mkdir(exist_ok=True)
    tree = win98_c.FOLDER
    if not (tree / 'WINDOWS/SYSTEM.INI').is_file():
        raise SystemExit('no Win98 folder; run python3 neo98.py setup --iso ...')
    drive, registry = first_logon.clone(out)

    shots = {name: out / (name + '.ppm') for name in
             ('before', 'desktop', 'desktop-closed', 'icon', 'icon-closed', 'taskbar', 'taskbar-closed',
              'command-menu', 'command-hover', 'command-done', 'outside-menu', 'outside-done')}
    lines = ['wait-window Shell_TrayWnd', 'wait-window SysListView32', 'wait 4000', f'shot {shots["before"]}']
    for name, (x, y) in (('desktop', DESKTOP), ('icon', ICON), ('taskbar', TASKBAR)):
        lines += [f'press {x} {y} 2', f'release {x} {y} 2', 'wait 2500', f'shot {shots[name]}',
                  # 1Bh is Escape; it must dismiss the menu.
                  'key 0x1b', 'wait 2000', f'shot {shots[name + "-closed"]}']
    # Left clicks activate menu entries or dismiss the menu outside it.
    # Refresh is the sixth desktop entry, 86 pixels below the initial click.
    # Use movement rather than point, which performs a complete click.
    x, y = DESKTOP
    item_x, item_y = x + 40, y + 86
    lines += [f'press {x} {y} 2', f'release {x} {y} 2', 'wait 2500', f'shot {shots["command-menu"]}',
              f'move {item_x} {item_y}', 'wait 1200', f'shot {shots["command-hover"]}',
              f'press {item_x} {item_y}', f'release {item_x} {item_y}', 'wait 3000',
              f'shot {shots["command-done"]}',
              f'press {x} {y} 2', f'release {x} {y} 2', 'wait 2500', f'shot {shots["outside-menu"]}',
              f'press {OUTSIDE[0]} {OUTSIDE[1]}', f'release {OUTSIDE[0]} {OUTSIDE[1]}', 'wait 2500',
              f'shot {shots["outside-done"]}']
    lines += ['stop']
    script = out / 'clicks.txt'
    script.write_text('\n'.join(lines) + '\n')

    runner = build_fast('probe_application.cpp', 'probe-smoke')
    entry = application('explorer')
    command = [str(runner), str(ROOT / 'build/programs' / entry['program']),
               str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', str(tree), '--drive-c', str(drive),
               '--registry', str(registry), '--no-idle-stop',
               # Allow more than twenty seconds for desktop startup and all
               # three menu captures.
               '--time-limit', '300',
               '--click-script', str(script), '--click-deadline', '60000']
    result = subprocess.run(command, capture_output=True, text=True, timeout=1800)
    log = result.stdout + result.stderr
    (out / 'run.log').write_text(log)
    result.check_returncode()
    if 'PROBE_SCRIPT_FAILED' in log:
        raise SystemExit('\n'.join(l for l in log.splitlines() if 'PROBE_SCRIPT' in l))

    # Opening the menu adds one window; Escape restores the previous count.
    shot_lines = [line for line in log.splitlines()
                  if line.startswith('PROBE_SCRIPT index=') and ' step=shot ' in line]
    counts = [int(re.search(r'windows=(\d+)', line).group(1)) for line in shot_lines]
    calls = [int(re.search(r'calls=(\d+)', line).group(1)) for line in shot_lines]
    if len(counts) != 12:
        raise SystemExit(f'{len(counts)} Bilder statt zwoelf: {counts}')
    baseline = counts[0]
    tracks = sum(1 for line in log.splitlines() if line.strip().startswith('TrackPopupMenu'))
    report = {'track_popup_menu': tracks, 'windows': counts,
              'menus_open_at_end': len(visible_menus(log)), 'changed': {}}
    for index, name in enumerate(('desktop', 'icon', 'taskbar')):
        opened = differing(shots['before'], shots[name])
        closed = differing(shots['before'], shots[name + '-closed'])
        report['changed'][name] = {'open': opened, 'closed': closed}
        if counts[1 + index * 2] != baseline + 1:
            raise SystemExit(f'{name}: No menu window, {counts} versus {baseline}')
        if counts[2 + index * 2] != baseline:
            raise SystemExit(f'{name}: Escape did not close the window, {counts}')
        if opened < 20000:
            raise SystemExit(f'{name}: No menu visible, only {opened} changed pixels')
        if closed > opened // 10:
            raise SystemExit(f'{name}: Escape did not close the menu ({closed} of {opened})')
    # Test an entry click and an outside click.
    menu_open = differing(shots['before'], shots['command-menu'])
    highlight = differing(shots['command-menu'], shots['command-hover'])
    hover_open = differing(shots['before'], shots['command-hover'])
    done = differing(shots['before'], shots['command-done'])
    outside_open = differing(shots['before'], shots['outside-menu'])
    outside_done = differing(shots['before'], shots['outside-done'])
    command_calls = calls[9] - calls[8]
    report['command'] = {'open': menu_open, 'highlight': highlight, 'hover': hover_open,
                         'done': done, 'calls': command_calls,
                         'outside_open': outside_open, 'outside_done': outside_done,
                         'outside_calls': calls[11] - calls[10]}
    if counts[7] != baseline + 1 or counts[8] != baseline + 1:
        raise SystemExit(f'entry click: menu is not open, {counts}')
    if menu_open < 20000 or hover_open < 20000:
        raise SystemExit(f'entry click: no menu is visible, {menu_open} and {hover_open}')
    if highlight < 100:
        raise SystemExit(f'pointer movement did not highlight the entry: {highlight}')
    if counts[9] != baseline:
        raise SystemExit(f'entry click did not close the menu, {counts}')
    if done > menu_open // 10:
        raise SystemExit(f'Menu remains visible after clicking an item: {done}')
    # Refresh executes over four hundred guest calls; Escape and outside
    # dismissal use fewer than fifty.
    if command_calls < 200:
        raise SystemExit(f'entry command did not reach the guest: {command_calls} calls')
    if counts[10] != baseline + 1 or outside_open < 20000:
        raise SystemExit(f'Second menu is missing: {counts}, {outside_open}')
    if counts[11] != baseline:
        raise SystemExit(f'outside click did not close the menu, {counts}')
    if outside_done > outside_open // 10:
        raise SystemExit(f'Menu remains visible after clicking outside: {outside_done}')
    if not tracks:
        raise SystemExit('No TrackPopupMenu during the run')
    if report['menus_open_at_end']:
        raise SystemExit('A menu window remains open at the end')
    print('CONTEXT MENU SHELL PASS ' + repr(report))
    (out / 'results.json').write_text(repr(report) + '\n')


if __name__ == '__main__':
    caption_menu()
    sys.exit(main())
