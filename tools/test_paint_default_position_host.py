#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Paint opened by Explorer must resolve CW_USEDEFAULT coordinates."""
from ui_clipboard_test_support import ROOT, serial, machine, host
import re


def main():
    with serial():
        out = ROOT / 'build/paint-default-position'
        with machine(out) as drive:
            log = host(out, drive, r'C:\WINDOWS\EXPLORER.EXE',
                       steps=f'4:key=1b;8:key=1b;10:wait=Shell_TrayWnd;'
                             f'11:open={drive}/Program Files/Accessories/MSPAINT.EXE;'
                             '13:wait=MSPaintApp;15:tops=1', seconds=17)
            frames = re.findall(r'visible=1 frame=(-?\d+),(-?\d+),(-?\d+),(-?\d+).* class=MSPaintApp', log)
            assert frames, out / 'host.log'
            x, y, right, bottom = map(int, frames[-1])
            assert 0 <= x < right <= 1024 and 0 <= y < bottom <= 768, frames[-1]
            print('PASS Explorer-launched Paint frame', frames[-1])


if __name__ == '__main__':
    main()
