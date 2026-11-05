#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""selecting a desktop submenu command removes its windows and pixels.

Run through a shared serial test lock. --user-tree reproduces on a private clone
of the installed C:; otherwise the test uses the pristine installation.
All writable files stay in build/fix-desktop-menu-scratch/.
"""
from pathlib import Path
import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from native_app import build_host
import win98_c
from win98_folder import journal_record


def snapshots(text):
    groups = []
    for line in text.splitlines():
        if line.startswith('NEO98 HOST tops foreground='):
            groups.append([])
        match = re.match(r'NEO98 HOST top id=(\w+) visible=(\d) frame=([\d,-]+) '
                         r'.* class=(.*?) title=(.*)', line)
        if match and groups:
            handle, visible, rect, klass, title = match.groups()
            groups[-1].append(dict(handle=handle, visible=visible == '1',
                                   rect=list(map(int, rect.split(','))), klass=klass, title=title))
    return groups


def pixels(path):
    magic, size, depth, body = path.read_bytes().split(b'\n', 3)
    width, height = map(int, size.split())
    assert magic == b'P6' and depth == b'255' and len(body) == width * height * 3
    return width, height, body


def changed(first, second, rect, exclude=()):
    width, height, a = first
    assert second[:2] == (width, height)
    b = second[2]
    left, top, right, bottom = rect
    samples = differences = 0
    for y in range(max(0, top), min(height, bottom)):
        for x in range(max(0, left), min(width, right)):
            if any(l <= x < r and t <= y < bot for l, t, r, bot in exclude):
                continue
            at = (y * width + x) * 3
            samples += 1
            differences += a[at:at + 3] != b[at:at + 3]
    return dict(samples=samples, changed=differences)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--user-tree', action='store_true')
    parser.add_argument('--prime-properties', action='store_true')
    parser.add_argument('--wallpaper', action='store_true', help='keep the installed wallpaper')
    parser.add_argument('--label', default='desktop-menu-close')
    args = parser.parse_args()
    if not re.fullmatch(r'[a-zA-Z0-9_-]+', args.label):
        parser.error('--label must be a simple directory name')
    if shutil.disk_usage(ROOT).free < 8 * 1024**3:
        raise SystemExit('STOP: fewer than 8 GiB free')
    out = ROOT / 'build/fix-desktop-menu-scratch' / args.label
    out.mkdir(parents=True, exist_ok=True)
    source = (ROOT / 'build/win98-c').resolve() if args.user_tree else win98_c.use_pristine().resolve()
    drive = out / 'drive-c'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', '-c', '-R', str(source), str(drive)], check=True)
    registry = out / 'session.registry'
    shutil.copyfile(win98_c.journal(source), registry)
    if not args.wallpaper:
        key = r'HKEY_USERS\.Default\Control Panel\Desktop'
        with registry.open('ab') as journal:
            for name in ('Wallpaper', 'Pattern'):
                journal.write(journal_record(3, key, name) + struct.pack('<II', 1, 1) + b'\0')
    binary = args.binary.resolve() if args.binary else (
        ROOT / 'build/native-app/neo98-host' if args.no_build else build_host())
    names = ('before', 'root', 'submenu', 'dialog', 'hover', 'closed', 'redraw')
    shots = {name: out / (name + '.ppm') for name in names}
    for path in shots.values():
        path.unlink(missing_ok=True)

    def capture(name):
        return [f'0:shot={shots[name]}', '0:tops=2']

    steps = ['0:wait=Shell_TrayWnd', '0:idle=3000']
    if args.prime_properties:
        steps += ['0:rclick=262,542', '0:idle=600', '0:click=320,718',
                  '0:wait=#32770|Display Properties', '0:idle=1200', '0:key=1B', '0:idle=800']
    steps += ['0:move=740,540', *capture('before'),
             '0:rclick=262,542', '0:idle=600', *capture('root'),
             '0:down=312,553', '0:idle=200', '0:up=312,553', '0:idle=800', *capture('submenu'),
             '0:down=440,570', '0:idle=200', '0:up=440,570',
             '0:wait=#32770|Display Properties', '0:idle=1500',
             '0:move=740,540', *capture('dialog'),
             '0:move=440,553', '0:idle=500', '0:move=740,540', *capture('hover'),
             '0:key=1B', '0:idle=800', *capture('closed'),
             '0:redraw=1', '0:idle=800', *capture('redraw'), '0:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_DEADLINE='120', NEO98_SETTINGS_DIR=str(out / 'settings'),
               NEO98_LOG_DIR=str(out / 'logs'), NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', str(drive), '--exe', r'C:\WINDOWS\EXPLORER.EXE', '--quiet',
               '--desktop', '1024x768',
               '--drive-c', str(drive), '--registry', str(registry),
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        process = subprocess.Popen(command, env=env, stdin=subprocess.DEVNULL,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=150)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=30)
            raise
    text = log_path.read_text(errors='replace')
    groups = snapshots(text)
    checks = {'host ended': code == 0,
              'desktop and dialog waits reached': text.count(' reached') >= 2 and 'timed out' not in text,
              'Customize opens the Web page': 'desk.cpl,,@Web' in text,
              'all screenshots and window snapshots': all(p.exists() for p in shots.values()) and len(groups) == len(names)}
    figures = {}
    if checks['all screenshots and window snapshots']:
        windows = dict(zip(names, groups))
        menus = {name: [w for w in group if w['visible'] and w['klass'] == '#32768']
                 for name, group in windows.items()}
        checks['one root menu, then a submenu'] = len(menus['root']) == 1 and len(menus['submenu']) == 2
        checks['no menu windows after the command'] = not menus['dialog'] and not menus['hover']
        dialogs = [w for w in windows['dialog'] if w['visible'] and w['title'] == 'Display Properties']
        checks['Display Properties is visible'] = len(dialogs) == 1
        checks['Escape reaches the dialog'] = not any(w['visible'] and w['title'] == 'Display Properties' for w in windows['closed'])
        frames = {name: pixels(path) for name, path in shots.items()}
        if checks['one root menu, then a submenu'] and dialogs:
            for menu in menus['submenu']:
                name = 'root' if menu['handle'] == menus['root'][0]['handle'] else 'submenu'
                rect = menu['rect']
                opened = changed(frames['before'], frames['submenu'], rect)
                checks[name + ' was painted'] = opened['changed'] > opened['samples'] // 2
                for stage in ('dialog', 'hover', 'closed'):
                    value = changed(frames['before'], frames[stage], rect,
                                    [dialogs[0]['rect']] if stage != 'closed' else [])
                    figures[name + '-' + stage] = value
                    checks[name + ' background restored at ' + stage] = value['samples'] > 100 and value['changed'] == 0
        # A full repaint must not be necessary to remove stale bottom-edge pixels.
        width, height, _ = frames['closed']
        value = changed(frames['closed'], frames['redraw'], (0, height - 3, width, height))
        figures['bottom-edge-after-redraw'] = value
        checks['bottom edge unchanged by full repaint'] = value['changed'] == 0
    report = dict(checks=checks, figures=figures, source=str(source), command=command, steps=steps)
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    shutil.rmtree(drive)
    for name, okay in checks.items():
        print(('ok  ' if okay else 'FAIL ') + name)
    print('DESKTOP MENU CLOSE ' + ('PASS' if all(checks.values()) else 'FAIL') + ': ' + str(out / 'results.json'))
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
