#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""original ListView masks, matching desktop colour, move and removal."""
from ui_clipboard_test_support import ROOT, serial, fixture, machine, host
from test_host_clipboard import IMPORTS
from test_seamless_host import image


def main():
    imports = {dll: dict(names) for dll, names in IMPORTS.items()}
    imports['USER32'].update(CreateIcon=28, DestroyIcon=4, SendMessageA=16, SetShellWindow=4,
                             GetSystemMetrics=4, InvalidateRect=12)
    imports['COMCTL32'] = dict(InitCommonControls=0, ImageList_Create=20, ImageList_ReplaceIcon=12)
    with serial():
        out = ROOT / 'build/desktop-icon-mask'
        exe = fixture(out / 'fixture', ROOT / 'tools/desktop_icon_mask_fixture.c', imports, entry='fixture_entry')
        exe.with_suffix('.OUT').unlink(missing_ok=True)
        with machine(out / 'run') as drive:
            host(out / 'run', drive, r'E:\TEST.EXE',
                 steps=f'2:mask={out}/first.pgm;2:shot={out}/first.ppm;'
                       f'6:mask={out}/moved.pgm;6:shot={out}/moved.ppm;10:mask={out}/empty.pgm',
                 seconds=12, extra=['--drive', 'E=' + str(exe.parent), '--seamless', '--seamless-icons'])
        rows = [[int(n) for n in line.split()] for line in exe.with_suffix('.OUT').read_text().splitlines()]
        assert len(rows) == 2, rows
        for name, row in zip(('first', 'moved'), rows):
            w, h, at = image(out / (name + '.pgm'))
            left, top, right, bottom, ll, lt, lr, lb = row
            icon_x, icon_y = left + (right - left - 32) // 2 + 3, top + (bottom - top - 32) // 2 + 3
            if name == 'moved':
                _, _, pixel = image(out / (name + '.ppm'))
                painted = {(x, y) for y in range(top, top + (bottom - top - 32) // 2 + 32) for x in range(left, right)
                           if pixel(x, y) == '008080'}
                assert painted == {(icon_x, icon_y)}, ('original icon position', painted, (icon_x, icon_y))
            expected = {(icon_x, icon_y)} | {(x, y) for y in range(lt, lb) for x in range(ll, lr)}
            actual = {(x, y) for y in range(h) for x in range(w) if at(x, y) == 'ff'}
            assert actual == expected, (name, len(actual), len(expected), sorted(actual ^ expected)[:30], row)
        w, h, at = image(out / 'empty.pgm')
        assert not any(at(x, y) == 'ff' for y in range(h) for x in range(w)), 'background is opaque'
        with machine(out / 'explorer') as drive:
            host(out / 'explorer', drive, r'C:\WINDOWS\EXPLORER.EXE',
                 steps='4:key=1b;8:key=1b;10:wait=Shell_TrayWnd;'
                       f'12:mask={out}/explorer-on.pgm;14:menu=seamless-icons;16:mask={out}/explorer-off.pgm',
                 seconds=18, extra=['--seamless', '--seamless-icons'])
        _, _, on = image(out / 'explorer-on.pgm')
        w, h, off = image(out / 'explorer-off.pgm')
        added = sum(on(x, y) == 'ff' and off(x, y) == '00' for y in range(h) for x in range(w))
        assert 100 < added < 60000, ('Explorer icon coverage', added)
        print('PASS exact icon pixel and label, background independence, movement and removal')


if __name__ == '__main__':
    main()
