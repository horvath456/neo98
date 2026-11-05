#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The program collection as drive D:, seen by the unchanged Explorer.

The collection is mounted without a copy and without write access
(tools/programs_drive.py). This test starts the original EXPLORER.EXE on
`D:\\GAMES`, the way `explorer.exe /n,PFAD` starts it, and checks three things:

- Explorer opens a folder window whose caption is the folder name and whose
  address field reads `D:\\GAMES`.
- The list view of that window is painted and shows more than the background,
  so the folders of the collection really arrived in the guest.
- The run adds no file to the collection. A writable host volume writes an.neo98-index sidecar into every directory it lists, which is the reason the
  mount is read only.

    build/analysis-venv/bin/python tools/test_programs_drive.py
"""
from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import programs_drive
from native_app import ROOT, build_fast
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

WINDOW = re.compile(r'^PROBE_WINDOW (.*)$', re.M)


def fields(line):
    out = {}
    for part in line.split(' '):
        key, _, value = part.partition('=')
        if key in ('class', 'text'):
            try:
                value = bytes.fromhex(value).decode('cp1252', 'replace')
            except ValueError:
                value = ''
        out[key] = value
    return out


def pixels(path):
    """The PPM of the probe as (width, height, bytes)."""
    data = path.read_bytes()
    parts = data.split(b'\n', 3)
    if len(parts) < 4 or parts[0] != b'P6':
        raise SystemExit(f'{path} is not the expected PPM')
    width, height = (int(n) for n in parts[1].split())
    return width, height, parts[3]


def area(shot, x, y, w, h):
    """How much of one rectangle is not the background, and how many colours it has."""
    width, height, raw = pixels(shot)
    colours, other, counted = {}, 0, 0
    for row in range(max(y, 0), min(y + h, height)):
        for column in range(max(x, 0), min(x + w, width)):
            at = (row * width + column) * 3
            colour = raw[at:at + 3]
            colours[colour] = colours.get(colour, 0) + 1
            counted += 1
            if colour != b'\xff\xff\xff':
                other += 1
    return {'pixels': counted, 'colours': len(colours), 'not_background': other}


def cells(shot, x, y, w, h, gap=8):
    """The entry cells of a large-icon list view, as (left, right) columns.

    A large-icon view leaves the background between two entries. Every column
    that carries a pixel other than the background belongs to an entry, and two
    columns less than `gap` apart belong to the same one, because the caption
    under an icon is wider than the icon.
    """
    width, height, raw = pixels(shot)
    used = []
    for column in range(max(x, 0), min(x + w, width)):
        for row in range(max(y, 0), min(y + h, height)):
            at = (row * width + column) * 3
            if raw[at:at + 3] != b'\xff\xff\xff':
                used.append(column)
                break
    out = []
    for column in used:
        if out and column - out[-1][1] <= gap:
            out[-1][1] = column
        else:
            out.append([column, column])
    return [tuple(entry) for entry in out]


def content_rows(shot, x, y, w, h):
    """The first and the last row of a rectangle that carries content."""
    width, height, raw = pixels(shot)
    found = []
    for row in range(max(y, 0), min(y + h, height)):
        for column in range(max(x, 0), min(x + w, width)):
            at = (row * width + column) * 3
            if raw[at:at + 3] != b'\xff\xff\xff':
                found.append(row)
                break
    return (found[0], found[-1]) if found else (0, -1)


def sidecars(directory):
    return sorted(str(p) for p in Path(directory).rglob('.neo98-index'))


def my_computer_run(runner, tree, program, out, attempt):
    """One run of the Arbeitsplatz folder, with its window list and its shot."""
    shot = out / (f'my-computer-{attempt}.ppm' if attempt else 'my-computer.ppm')
    command = [str(runner), str(program), str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', str(tree),
               '--exe', 'C:\\WINDOWS\\EXPLORER.EXE',
               '--command', 'explorer.exe /n,::{20D04FE0-3AEA-1069-A2D8-08002B30309D}',
               '--drive-c', str(out / f'mycomputer-c{attempt or ""}'),
               *programs_drive.mount_arguments(),
               '--time-limit', '180', '--stall-limit', '20',
               '--screenshot', str(shot)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=900)
    (out / (f'my-computer-{attempt}.log' if attempt else 'my-computer.log')).write_text(
        result.stdout + result.stderr)
    return shot, result


def my_computer(runner, tree, program, out, check):
    """The Arbeitsplatz folder over the same two drives.

    Until the session had a shared area, the enumerating thread of SHELL32 hung
    for ever in `WNetGetConnectionA`, because MPR waited on `MPRInitEvent` and
    MPREXE.EXE never read the 1 in the shared word  ( and
    ). Since `608ddf4` publishes a global handle in the number space of
    the original, MPREXE's SetEvent reaches MPR's own event, the wait ends and
    the enumeration finishes. This phase checks with pixels that both drives
    stand in the list, each with its icon and its name.

    The web view of the folder carries under host load the template
    stops at an ActiveX warning and the view stays empty. A run that shows that
    message box says nothing about so it is repeated; only a run without
    the box counts, and a busy host that loses every attempt leaves the drive
    checks out with a line that says so.
    """
    attempt, shot, result, box = 0, None, None, True
    while attempt < 4 and box:
        shot, result = my_computer_run(runner, tree, program, out, attempt)
        box = any(w['class'] == '#32770' and w['visible'] == '1'
                  and w['text'] == 'Microsoft Internet Explorer'
                  for w in (fields(line) for line in WINDOW.findall(result.stdout)))
        attempt += 1
    if box:
        print(f'PROGRAMS DRIVE blocked my computer drive list: the ActiveX warning '
              f'stood in all {attempt} runs')
    windows = [fields(line) for line in WINDOW.findall(result.stdout)]
    started = re.search(r'^PROBE started=1 state=(\d+) code=\d+ calls=(\d+)', result.stdout, re.M)
    cabinet = next((w for w in windows if w['class'] == 'CabinetWClass' and w['visible'] == '1'), None)
    view = next((w for w in windows if w['class'] == 'SysListView32' and w['visible'] == '1'
                 and w['drawn'] == '1'), None)
    # MPREXE.EXE is the process MPR starts; the shared word tells it to report
    # back. Its own MPRSERV.dll has to get through its initialization on the
    # heap that MPR created with HEAP_SHARED in the other process.
    mprexe = 'MPRSERV.dll base=7fad0000' in result.stdout
    check('my computer run alive', bool(started) and started.group(1) != '3',
          started.group(0) if started else 'no PROBE line')
    check('my computer window', cabinet is not None and cabinet['text'] == 'My Computer',
          cabinet['text'] if cabinet else 'none')
    check('mprexe reports in', mprexe, 'MPRSERV mapped in the child' if mprexe else 'no MPRSERV')
    check('no child failed', 'DLL init refused' not in result.stdout,
          'no refused module' if 'DLL init refused' not in result.stdout else 'a child refused a module')
    painted, entries, drives = {}, [], []
    if view and not box:
        vx, vy, vw, vh = (int(view[key]) for key in ('x', 'y', 'w', 'h'))
        painted = area(shot, vx, vy, vw, vh)
        entries = cells(shot, vx, vy, vw, vh)
        top, bottom = content_rows(shot, vx, vy, vw, vh)
        # The first two cells are the two drives. Win98 gives each one a 32
        # pixel icon and a caption under it, so both bands of the cell have to
        # carry something.
        for left, right in entries[:2]:
            icon = area(shot, left, top, right - left + 1, 36)
            label = area(shot, left, top + 38, right - left + 1, max(bottom - top - 37, 1))
            drives.append({'left': left, 'right': right, 'icon': icon, 'label': label})
    check('my computer observation reached the folder view', not box,
          'folder view visible' if not box else f'ActiveX warning in all {attempt} runs')
    if not box:
        check('my computer list view', view is not None, view['hwnd'] if view else 'none')
        check('my computer lists the drives and the folders', len(entries) >= 7,
              f'{len(entries)} cells: {entries}')
        check('C: and D: carry a drive icon',
              len(drives) == 2 and all(d['icon']['not_background'] > 100 and d['icon']['colours'] > 3
                                       for d in drives),
              '; '.join(str(d['icon']) for d in drives) or 'no cells')
        check('C: and D: carry a name',
              len(drives) == 2 and all(d['label']['not_background'] > 40 for d in drives),
              '; '.join(str(d['label']) for d in drives) or 'no cells')
    return {'calls': int(started.group(2)) if started else 0, 'state': started.group(1) if started else '',
            'cabinet': cabinet, 'view': view, 'painted': painted, 'mprexe': mprexe,
            'entries': entries, 'drives': drives, 'runs': attempt, 'activex_warning': box}


def main():
    if not programs_drive.available():
        raise SystemExit(f'no program collection at {programs_drive.COLLECTION}')
    out = ROOT / 'build/programs-drive'
    out.mkdir(parents=True, exist_ok=True)
    tree = win98_c.FOLDER
    if not (tree / 'WINDOWS/SYSTEM.INI').is_file():
        raise SystemExit('no Win98 folder; run python3 neo98.py setup --iso ...')
    program = ROOT / 'build/programs/EXPLORER.EXE'
    if not program.is_file():
        raise SystemExit(f'{program} is missing; run python3 neo98.py host --app explorer once')
    # Runs before 19 September 2026 mounted the collection writable and left
    # sidecar files in it. They are not deleted here; the check is that this run
    # adds none. Remove them through the guest device manager.
    before = sidecars(programs_drive.COLLECTION)
    runner = build_fast('probe_application.cpp', 'probe-smoke')
    shot = out / 'games-folder.ppm'
    drive_c = out / 'drive-c'
    command = [str(runner), str(program), str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', str(tree),
               '--exe', 'C:\\WINDOWS\\EXPLORER.EXE',
               '--command', f'explorer.exe /n,{programs_drive.GAMES_PATH}',
               '--drive-c', str(drive_c),
               *programs_drive.mount_arguments(),
               '--time-limit', '120', '--stall-limit', '20',
               '--screenshot', str(shot)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=600)
    (out / 'games-folder.log').write_text(result.stdout + result.stderr)
    windows = [fields(line) for line in WINDOW.findall(result.stdout)]
    cabinet = next((w for w in windows if w['class'] == 'CabinetWClass' and w['visible'] == '1'), None)
    if not cabinet:
        raise SystemExit('Explorer opened no folder window over the mounted collection')
    address = next((w for w in windows if w['class'] == 'Edit'
                    and w['text'].upper() == programs_drive.GAMES_PATH.upper()), None)
    view = next((w for w in windows if w['class'] == 'SysListView32' and w['visible'] == '1'
                 and w['drawn'] == '1'), None)
    checks = []

    def check(name, ok, detail):
        checks.append({'check': name, 'ok': bool(ok), 'detail': detail})

    check('folder window', cabinet['text'].upper() == 'GAMES', cabinet['text'])
    check('address field', address is not None, address['text'] if address else 'none')
    check('list view painted', view is not None, view['hwnd'] if view else 'none')
    painted = {}
    if view:
        painted = area(shot, int(view['x']), int(view['y']), int(view['w']), int(view['h']))
        # The empty list view of a missing drive is one white rectangle. The
        # folders of the collection bring their icon colours with them.
        check('list view shows entries', painted['colours'] > 8 and painted['not_background'] > 1000,
              str(painted))
    after = sidecars(programs_drive.COLLECTION)
    check('collection unchanged', after == before,
          f'{len(before)} sidecar files before, {len(after)} after')
    mycomputer = my_computer(runner, tree, program, out, check)
    report = {'command': command, 'windows': len(windows), 'cabinet': cabinet, 'view': view,
              'painted': painted, 'my_computer': mycomputer, 'checks': checks}
    (out / 'results.json').write_text(json.dumps(report, indent=1) + '\n')
    for entry in checks:
        print(f"PROGRAMS DRIVE {'ok  ' if entry['ok'] else 'FAIL'} {entry['check']}: {entry['detail']}")
    if any(not entry['ok'] for entry in checks):
        raise SystemExit('PROGRAMS DRIVE FAILED')
    print(f"PROGRAMS DRIVE PASS, {programs_drive.COLLECTION} as "
          f"{programs_drive.LETTER}: read only")


if __name__ == '__main__':
    main()
