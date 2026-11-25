#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""File Manager of Windows 98 customizes its toolbar and keeps it.

WINFILE.EXE is a 16-bit program with the toolbar of the 16-bit COMMCTRL. It
saves the toolbar with TB_SAVERESTORE after every change and restores it at
its start; the 16-bit COMMCTRL keeps the buttons in the registry under
HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\ToolbarState,
value "Settings". Options > Customize Toolbar runs the dialog of COMMCTRL,
which talks to File Manager with the WM_COMMAND notifications of the 16-bit
toolbar (TBN_ADJUSTINFO for the buttons it offers, TBN_QUERYINSERT,
TBN_QUERYDELETE, TBN_TOOLBARCHANGE, TBN_RESET). Three sessions on neo98-kms
over one private copy of drive C:, with the picture of a 1024x768 desktop in
memory:

  start      the drive box on the toolbar shows drive C:, which File
             Manager draws itself (an owner-drawn combo box); the toolbar
             passed WM_DRAWITEM, WM_MEASUREITEM and WM_COMMAND of the combo
             box to DefWindowProc instead of its parent, and the box stayed
             empty
  customize  the dialog opens; Add puts File > Move at the end, Remove takes
             View > Sort by Type away, Move Up puts Sort by Size before Sort
             by Name, each on the toolbar at once; Close
  restart    File Manager comes back with the toolbar of the first session;
             Reset in the dialog brings the standard buttons back
  standard   after Reset, a new start shows the standard buttons again
  drag       File > Move dragged from the available buttons onto the first
             button of the toolbar's list goes on the toolbar; dragged back
             onto the available buttons it goes away again (the lists were
             no drag lists before). While it is held over the toolbar's list
             the pointer is the copy cursor of COMCTL32 (its cursor 104, the
             answer DL_COPYCURSOR of the dialog), as COMCTL32's own drag list
             shows it, and the arrow of DrawInsert (COMCTL32's icon 150)
             stands left of the list; the pointer was the arrow, and the
             icon never came from COMCTL32 read as a data file

Before, the dialog stopped the run (toolbar message 041Bh not ported), and
the toolbar answered that it neither saved nor restored.

The steps wait for the menu, for the dialog and, before every picture, for
a quiet machine; a loaded machine only makes the sessions longer.

    build/analysis-venv/bin/python tools/test_winfile_toolbar_host.py [--no-build] [--keep] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
import argparse
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402
from test_media_center_host import read_png  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/winfile-toolbar'
# The buttons of File Manager's toolbar on 1024x768, right of its drive box,
# the place of the Customize Toolbar dialog over it and a point of its
# caption.
TOOLBAR = (218, 44, 640, 66)
DIALOG = (20, 78, 560, 300)
DIALOG_CAPTION = (300, 88)
# The inside of the drive box at the left end of the toolbar.
DRIVE_BOX = (16, 47, 190, 62)
FACE = (192, 192, 192)
# Options > Customize Toolbar, and the controls of the dialog.
CUSTOMIZE = ['click=160,31', 'wait=#32768', 'click=200,86', 'wait=#32770|Customize Toolbar', 'idle=300']
MOVE, ADD, SORT_BY_TYPE, REMOVE = (85, 164), (250, 176), (370, 139), (250, 210)
SORT_BY_SIZE, MOVE_UP, CLOSE, RESET = (370, 164), (516, 228), (516, 118), (516, 145)
# The place left of the toolbar's list where DrawInsert's arrow stands.
INSERT_ARROW = (280, 118, 296, 145)
# The first item the toolbar's list shows, and a free place of the available
# buttons.
SORT_BY_NAME, AVAILABLE = (370, 139), (110, 200)


def area(picture, box):
    width, _, data = picture
    left, top, right, bottom = box
    return [bytes(data[(y * width + left) * 3:(y * width + right) * 3]) for y in range(top, bottom)]


def pixel(picture, x, y):
    width, _, data = picture
    return tuple(data[(y * width + x) * 3:(y * width + x) * 3 + 3])


def right_end(picture):
    """The rightmost column of the toolbar that is not the face colour."""
    left, top, right, bottom = TOOLBAR
    for x in range(right - 1, left, -1):
        if any(pixel(picture, x, y) != FACE for y in range(top + 2, bottom - 2)):
            return x
    return 0


def session(kms, drive, steps, limit):
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(WORK)))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    result = subprocess.run([str(kms), '--drive-c', str(drive), '--desktop', '1024x768',
                             '--exe', 'C:\\WINDOWS\\WINFILE.EXE', '--time-limit', str(limit)], cwd=ROOT, env=env,
                            capture_output=True, text=True, timeout=limit + 60)
    return result.stdout + result.stderr


def click(x_y):
    return f'click={x_y[0]},{x_y[1]}'


def drag(start, end):
    return f'drag={start[0]},{start[1]},{end[0]},{end[1]}'


def pe_resource(data, kind, ident):
    """The data of a resource of a PE file by its type and id, the first
    language."""
    pe = struct.unpack_from('<I', data, 60)[0]
    sections = [struct.unpack_from('<IIII', data, at + 8)
                for at in range(pe + 24 + struct.unpack_from('<H', data, pe + 20)[0],
                                pe + 24 + struct.unpack_from('<H', data, pe + 20)[0] +
                                40 * struct.unpack_from('<H', data, pe + 6)[0], 40)]
    def offset(rva):
        for size, virtual, raw_size, raw in sections:
            if virtual <= rva < virtual + max(size, raw_size):
                return rva - virtual + raw
        raise ValueError(hex(rva))
    root = offset(struct.unpack_from('<I', data, pe + 24 + 96 + 2 * 8)[0])
    def entry(directory, wanted):
        named, numbered = struct.unpack_from('<HH', data, directory + 12)
        for i in range(named + numbered):
            name, target = struct.unpack_from('<II', data, directory + 16 + 8 * i)
            if wanted is None or (not name & 0x80000000 and name == wanted):
                return target
        raise KeyError(wanted)
    language = root + (entry(root + (entry(root, kind) & 0x7fffffff), ident) & 0x7fffffff)
    leaf = root + entry(language, None)
    rva, size = struct.unpack_from('<II', data, leaf)
    return data[offset(rva):offset(rva) + size]


def cursor_pixels(dll, ident):
    """The pixels of a cursor of a DLL as Windows draws it on a screen: (x, y)
    from its hot spot -> 0 black, 1 white; transparent pixels are left out."""
    group = pe_resource(dll, 12, ident)
    member = struct.unpack_from('<H', group, 6 + 12)[0]
    blob = pe_resource(dll, 1, member)
    hot_x, hot_y = struct.unpack_from('<HH', blob, 0)
    header = blob[4:]
    size, width, height = struct.unpack_from('<Iii', header, 0)
    height //= 2
    stride = (width + 31) // 32 * 4
    xor = header[size + 8:size + 8 + stride * height]
    mask = header[size + 8 + stride * height:]
    pixels = {}
    for row in range(height):
        y = height - 1 - row
        for x in range(width):
            bit = lambda plane: plane[row * stride + x // 8] >> (7 - x % 8) & 1
            if not bit(mask):
                pixels[(x - hot_x, y - hot_y)] = bit(xor)
    return pixels


def shows_cursor(picture, at, cursor):
    """The share of the opaque pixels of a cursor that the picture shows at
    the point."""
    if not picture or not cursor:
        return 0.0
    hits = sum(pixel(picture, at[0] + dx, at[1] + dy) == ((255, 255, 255) if white else (0, 0, 0))
               for (dx, dy), white in cursor.items())
    return hits / len(cursor)


def black(picture, box):
    """The black pixels of a box of a picture."""
    left, top, right, bottom = box
    return sum(pixel(picture, x, y) == (0, 0, 0) for y in range(top, bottom) for x in range(left, right))


def shot(name):
    """A picture once the machine has been quiet for a moment."""
    return ['idle=300', f'shot={{out}}/{name}.png']


def script(*steps):
    """The steps with their times: an input or a picture 0.3 s after the
    one before, a wait at once. A wait holds the clock of the script until
    it is met, and the later seconds count from there."""
    timed, at = [], 0.0
    for step in steps:
        if not step.startswith(('wait=', 'idle=')):
            at += 0.3
        timed.append(f'{at:.1f}:{step}')
    return timed


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--keep', action='store_true', help='keep the copy of the drive')
    parser.add_argument('--kms', type=Path, default=BINARY)
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host()
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    drive = WORK / 'c'
    pristine = win98_c.use_pristine()
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), win98_c.journal(drive))
    start = ['wait=WFS_Frame', 'idle=500']
    try:
        log = session(args.kms, drive, script(*start, *shot('start'), *CUSTOMIZE, *shot('dialog'), click(MOVE),
                                              'idle=300', click(ADD), *shot('added'), click(SORT_BY_TYPE),
                                              'idle=300', click(REMOVE), *shot('removed'), click(SORT_BY_SIZE),
                                              'idle=300', click(MOVE_UP), *shot('moved'), click(CLOSE),
                                              *shot('closed'), 'quit=1'), 150)
        journal = win98_c.journal(drive)
        saved = journal.read_bytes() if journal.is_file() else b''
        log += session(args.kms, drive, script(*start, *shot('restored'), *CUSTOMIZE, click(RESET), *shot('reset'),
                                               click(CLOSE), *shot('reset-closed'), 'quit=1'), 120)
        log += session(args.kms, drive, script(*start, *shot('standard'), 'quit=1'), 90)
        # The drag into the toolbar's list is held for a picture of the
        # pointer before it goes down there.
        held = [f'down={MOVE[0]},{MOVE[1]}', *[f'move={MOVE[0] + (SORT_BY_NAME[0] - MOVE[0]) * i // 4},'
                                               f'{MOVE[1] + (SORT_BY_NAME[1] - MOVE[1]) * i // 4}' for i in (1, 2, 3, 4)],
                *shot('dragging'), f'up={SORT_BY_NAME[0]},{SORT_BY_NAME[1]}']
        log += session(args.kms, drive, script(*start, *CUSTOMIZE, *held, *shot('dragged'),
                                               drag(SORT_BY_NAME, AVAILABLE), *shot('dragged-back'), click(CLOSE),
                                               'idle=300', 'quit=1'), 120)
        comctl32 = win98_c.path('C:\\WINDOWS\\SYSTEM\\COMCTL32.DLL', drive)
        copy_cursor = cursor_pixels(comctl32.read_bytes(), 104) if comctl32 else {}
    finally:
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
    (WORK / 'log.txt').write_text(log)
    stops = [line for line in log.splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line]
    names = ('start', 'dialog', 'added', 'removed', 'moved', 'closed', 'restored', 'reset', 'reset-closed',
             'standard', 'dragging', 'dragged', 'dragged-back')
    pictures = {name: read_png(WORK / f'{name}.png') if (WORK / f'{name}.png').is_file() else None for name in names}
    present = all(pictures.values())
    bar = {name: area(picture, TOOLBAR) for name, picture in pictures.items() if picture}
    end = {name: right_end(picture) for name, picture in pictures.items() if picture}
    caption = pixel(pictures['dialog'], *DIALOG_CAPTION) if pictures['dialog'] else (0, 0, 0)
    drive_box = sum(pixel(pictures['start'], x, y) != (255, 255, 255) for y in range(DRIVE_BOX[1], DRIVE_BOX[3])
                    for x in range(DRIVE_BOX[0], DRIVE_BOX[2])) if pictures['start'] else 0
    timed_out = [line for line in log.splitlines() if 'timed out after' in line]
    checks = {
        'every session runs without a stop': not stops and present,
        f'every menu, dialog and quiet moment came: {timed_out}': not timed_out,
        f'start: the drive box shows drive C: ({drive_box} pixels drawn)': drive_box > 30,
        f'customize: Options > Customize Toolbar opens the dialog {caption}':
            present and caption[2] > caption[0] + 60 and area(pictures['dialog'], DIALOG) !=
            area(pictures['start'], DIALOG),
        f'customize: Add puts a button at the end of the toolbar {end.get("start")} -> {end.get("added")}':
            present and end['added'] > end['start'] + 16,
        f'customize: Remove takes one away {end.get("added")} -> {end.get("removed")}':
            present and end['removed'] < end['added'] - 16 and bar['removed'] != bar['start'],
        'customize: Move Up changes the order and keeps the buttons':
            present and bar['moved'] != bar['removed'] and end['moved'] == end['removed'],
        'customize: Close leaves the toolbar as customized':
            present and bar['closed'] == bar['moved'] and area(pictures['closed'], DIALOG) !=
            area(pictures['moved'], DIALOG),
        'customize: the toolbar is saved under ToolbarState, value Settings':
            b'CurrentVersion\\ToolbarState' in saved and b'Settings' in saved,
        'restart: File Manager comes back with the customized toolbar':
            present and bar['restored'] == bar['closed'],
        'restart: Reset brings back the standard buttons': present and bar['reset'] == bar['start'],
        'standard: after Reset a new start shows the standard buttons':
            present and bar['standard'] == bar['start'] and bar['reset-closed'] == bar['start'],
        f'drag: File > Move dragged into the toolbar\'s list goes on the toolbar '
        f'{end.get("standard")} -> {end.get("dragged")}':
            present and end['dragged'] > end['standard'] + 16,
        'drag: dragged back onto the available buttons it goes away':
            present and bar['dragged-back'] == bar['standard'],
        f'drag: held over the toolbar\'s list the pointer is the copy cursor of COMCTL32 '
        f'({shows_cursor(pictures["dragging"], SORT_BY_NAME, copy_cursor):.0%} of its pixels)':
            present and shows_cursor(pictures['dragging'], SORT_BY_NAME, copy_cursor) >= 0.98,
        f'drag: the arrow of DrawInsert stands left of the list while it is held, and goes after the drop '
        f'({black(pictures["dragging"], INSERT_ARROW)}, {black(pictures["dragged"], INSERT_ARROW)} black pixels)':
            present and black(pictures['dragging'], INSERT_ARROW) > 10 and
            black(pictures['dragged'], INSERT_ARROW) == 0 and black(pictures['dialog'], INSERT_ARROW) == 0,
    }
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    if stops:
        print('    stop:', stops[0])
    print(f'Pictures and log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'winfile toolbar: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
