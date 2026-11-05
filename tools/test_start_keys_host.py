#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""the shell receives Start shortcuts while Notepad has focus."""
from ui_clipboard_test_support import ROOT, serial, machine, host
import re


def main():
    with serial():
        out = ROOT / 'build/start-keys'
        with machine(out) as drive:
            log = host(out, drive, r'C:\WINDOWS\EXPLORER.EXE',
                       steps=f'4:key=1b;8:key=1b;10:wait=Shell_TrayWnd;11:open={drive}/WINDOWS/NOTEPAD.EXE;'
                             '13:wait=Notepad;14:tops=1;15:keydown=11;15.1:key=1b;15.2:keyup=11;'
                             '17:tops=1;18:key=1b;19:keydown=5b;19.2:keyup=5b;21:tops=1;'
                             '22:key=1b;23:keydown=5c;23.1:key=41;23.2:keyup=5c;25:tops=1', seconds=27)
            snapshots = re.findall(r'^NEO98 HOST tops foreground=.*$(?:\n^NEO98 HOST (?:top|child) .*$)*', log, re.M)
            assert len(snapshots) == 4, len(snapshots)
            assert 'class=Notepad' in snapshots[0]
            for snapshot in snapshots[1:3]:
                assert re.search(r'visible=1 .* class=BaseBar', snapshot), snapshot
            assert not re.search(r'visible=1 .* class=BaseBar', snapshots[3]), snapshots[3]
        print('PASS Ctrl+Esc and Windows key from Notepad')


if __name__ == '__main__':
    main()
