#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""render, destroy, and viewer callbacks cross process boundaries."""
from ui_clipboard_test_support import ROOT, serial, fixture, machine, host
from test_host_clipboard import IMPORTS
import shutil


def main():
    imports = {dll: dict(names) for dll, names in IMPORTS.items()}
    imports['KERNEL32']['CreateProcessA'] = 40
    imports['USER32'].update(SetClipboardViewer=4, ChangeClipboardChain=8, GetClipboardOwner=0, PostMessageA=16)
    with serial():
        out = ROOT / 'build/clipboard-cross-process'
        exe = fixture(out / 'fixture', ROOT / 'tools/clipboard_cross_process_fixture.c', imports, entry='fixture_entry')
        shutil.copyfile(exe, exe.with_name('READ.EXE'))
        for name in ('TEST.OUT', 'READ.OUT'):
            exe.with_name(name).unlink(missing_ok=True)
        with machine(out / 'run') as drive:
            host(out / 'run', drive, r'E:\TEST.EXE', seconds=10, extra=['--drive', 'E=' + str(exe.parent)])
        assert 'PASS reader' in exe.with_name('READ.OUT').read_text()
        assert 'PASS owner' in exe.with_name('TEST.OUT').read_text()
        print('PASS WM_RENDERFORMAT, WM_DESTROYCLIPBOARD, WM_DRAWCLIPBOARD')


if __name__ == '__main__':
    main()
