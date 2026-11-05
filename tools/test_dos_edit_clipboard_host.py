#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""the DOS toolbar pastes into EDIT and copies OEM screen text."""
from ui_clipboard_test_support import ROOT, serial, machine, host
from test_host_clipboard import dump
import sys


def main():
    with serial():
        out = ROOT / 'build/dos-edit-clipboard'
        out.mkdir(parents=True, exist_ok=True)
        text = 'HALLO DOS 123' + (' äöü ÄÖÜ ß' if '--oem' in sys.argv else '')
        (out / 'input.txt').write_text(text + '\n')
        with machine(out) as drive:
            log = host(out, drive, r'C:\WINDOWS\COMMAND\EDIT.COM',
                       steps=f'3:tops=1;4:pbtext={out}/input.txt;5:click=198,91;'
                             '8:click=152,91;9:down=69,107;10:move=1024,502;'
                             f'11:up=1024,502;12:click=175,91;13:pbdump={out}/copy.txt', seconds=15)
            assert 'DOSVM toolbar button=Paste' in log and 'DOSVM toolbar button=Copy' in log
            copied = dump(out / 'copy.txt')[0] or ''
            assert text in copied, copied
            if '--oem' in sys.argv:
                assert '\u2500' in copied and '\u2502' in copied and '\u00c4\u00c4' not in copied, copied
                print('PASS EDIT screen borders on the Mac pasteboard')
            else:
                print('PASS toolbar paste reaches EDIT.COM')


if __name__ == '__main__':
    main()
