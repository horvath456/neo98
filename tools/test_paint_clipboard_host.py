#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""WMF bitmap records and Paint selection copy on a private pasteboard."""
from ui_clipboard_test_support import ROOT, serial, fixture, machine, host
from gdi_metafile_components import IMPORTS
from test_host_clipboard import png, dump_image
import re


def main():
    with serial():
        out = ROOT / 'build/paint-clipboard'
        exe = fixture(out / 'fixture', ROOT / 'tools/gdi_metafile_fixture.c', IMPORTS,
                      defines=['-DGDI_METAFILE_STANDALONE', '-DGDI_METAFILE_STRETCH_ONLY'])
        with machine(out / 'wmf') as drive:
            log = host(out / 'wmf', drive, r'E:\TEST.EXE', seconds=12,
                       extra=['--drive-readonly', 'E=' + str(exe.parent)])
            assert re.search(r'NEO98 HOST exited code=0\b', log), log[-2000:]
        paint = out / 'paint'
        paint.mkdir(exist_ok=True)
        picture = paint / 'input.png'
        picture.write_bytes(png(40, 30, [(255, 0, 0)] * 1200))
        with machine(paint) as drive:
            log = host(paint, drive, r'C:\Program Files\Accessories\MSPAINT.EXE',
                       steps=f'0:wait=MSPaintApp;2:pbimage={picture};3:menu=edit-paste;'
                             f'5:menu=edit-copy;7:pbdump={paint}/copy.txt;8:tops=1', seconds=10)
            assert 'class=MSPaintApp' in log
            copied = dump_image(paint / 'copy.txt')
            assert copied and copied[:2] == (40, 30), copied
        print('PASS WMF records, playback, Paint selection copy')


if __name__ == '__main__':
    main()
