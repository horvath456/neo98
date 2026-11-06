#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""WinHelp's structure command preserves every payload byte."""
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: compares the FEX and Unicorn engines of macOS')  # macOS only; skipped elsewhere
        _sys.exit(77)
import os
import re
from ui_clipboard_test_support import ROOT, fixture, machine, host

imports = {'KERNEL32': {'ExitProcess': 4}, 'USER32': {
    'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16,
    'DestroyWindow': 4, 'WinHelpA': 16}}
out = ROOT / 'build/winhelp-structure'
exe = fixture(out / 'fixture', ROOT / 'tools/winhelp_structure_fixture.c', imports)
for cpu in ('fex', 'unicorn'):
    os.environ['NEO98_CPU'] = cpu
    with machine(out / cpu) as drive:
        log = host(out / cpu, drive, r'E:\TEST.EXE', seconds=12,
                   extra=['--drive-readonly', 'E=' + str(exe.parent)])
        assert re.search(r'NEO98 HOST exited code=0\b', log), log[-3000:]
    print(f'PASS {cpu}: HELP_MULTIKEY preserves 24, 4097 and 65500 bytes')
