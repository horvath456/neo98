#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""WinHelpA starts WINHLP32 with the topic (src/x86_winhelp.cpp).

The shell's warning for a program APPS.INF names ("This program runs on
Windows 98. However,...") calls WinHelpA(hwnd, "apps.hlp>Proc4",
HELP_CONTEXT, 480h) for its Details button. The host gave up there with
"WinHelp viewer not implemented". APPS.INF knows One Must Fall by OMF.EXE with
OMF-END.BIN beside it, so a DOS program of five bytes under that name brings
the dialog up without the game. Details must open the APPS.HLP topic on PIF
files in the narrow procedure window Proc4 of
Windows Help (MS_WINTOPIC_SECONDARY), over a clone of setup's C:, without a
window.

    build/analysis-venv/bin/python tools/test_winhelp_host.py [--no-build]
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
from test_device_manager_host import failures, host  # noqa: E402
from test_wheel_host import Steps  # noqa: E402


def dos_program():
    """An MZ program that ends at once: mov ax,4C00h / int 21h."""
    code = bytes((0xb8, 0x00, 0x4c, 0xcd, 0x21))
    size = 32 + len(code)
    header = struct.pack('<2s13H', b'MZ', size % 512, (size + 511) // 512, 0, 2, 0, 0xffff, 0, 0x100, 0, 0, 0, 0x1c, 0)
    return header.ljust(32, b'\0') + code


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n', 1)[0])
    parser.add_argument('--no-build', action='store_true')
    arguments = parser.parse_args()
    pristine = win98_c.use_pristine()
    journal = win98_c.journal(pristine)
    out = neo98.output_directory('winhelp-host')
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if arguments.no_build else build_host()
    game = out / 'staged' / 'OMF'
    game.mkdir(parents=True, exist_ok=True)
    (game / 'OMF.EXE').write_bytes(dos_program())
    (game / 'OMF-END.BIN').write_bytes(b'\0' * 16)
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)
        print(('ok  ' if condition else 'FAIL'), name, flush=True)

    steps = Steps('|OMF.EXE', 3.0)
    steps.add('tops=3', 1.0).add('button=Details', 1.0).add('wait=MS_WINTOPIC_SECONDARY', 3.0).add('tops=4')
    code, text, run = host(binary, out, 'details', 'C:\\WINDOWS\\COMMAND\\START.EXE', 'START.EXE E:\\OMF\\OMF.EXE',
                           steps.steps, int(steps.time) + 25, pristine, journal, drives=(f'E={out / "staged"}',),
                           environment={'NEO98_TRACE_TEXT': '1'})
    check('the shell warns about One Must Fall', 'wait=|OMF.EXE reached' in text)
    check('Details opens the topic in the procedure window apps.hlp>Proc4',
          'wait=MS_WINTOPIC_SECONDARY reached' in text and 'class=MS_WINTOPIC_SECONDARY' in text)
    check('Windows Help shows the APPS.HLP topic 480h on PIF files',
          'This setup program created PIF files' in text)
    check('no crash or call the host does not carry',
          code == 0 and not failures(text) and 'CRASHREPORT' not in text)

    # HELP_CONTEXTMENU: a right click on Cancel of Notepad's Find dialog asks
    # "What's This?"; W chooses it, and the WINDOWS.HLP popup of
    # the control ID 2 (6F1Ch) appears in a window of MS_POPUPHELP.
    steps = Steps('Notepad', 3.0)
    steps.add('key=72', 1.0).add('wait=#32770|Find', 1.0).add('rclick=426,287', 1.0).add('tops=3', 0.5)
    steps.add('key=57', 1.0).add('wait=MS_WINNOTE', 2.0).add('tops=4')
    code, text, run = host(binary, out, 'whats-this', 'C:\\WINDOWS\\NOTEPAD.EXE', 'NOTEPAD.EXE',
                           steps.steps, int(steps.time) + 25, pristine, journal, environment={'NEO98_TRACE_TEXT': '1'})
    check('a right click in a dialog shows the menu "What\'s This?" at the pointer',
          'frame=426,287,540,310 style=90000000 process=1 props=0 class=#32768' in text)
    check('What\'s This? shows the popup of Cancel from WINDOWS.HLP',
          'wait=MS_WINNOTE reached' in text and 'Closes the dialog box without saving' in text)
    check('What\'s This?: no crash or call the host does not carry',
          code == 0 and not failures(text) and 'CRASHREPORT' not in text)
    print(f'{sum(checks.values())}/{len(checks)} checks pass, log {run / "log.txt"}')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
