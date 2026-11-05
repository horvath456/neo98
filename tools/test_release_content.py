#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""resource and UI regressions using the Mac release compile flags.

Uses private runners, never launches a window or creates a disk image.
Build Neo98.app separately; audit_bundle.py checks the finished bundle.
"""
import os
from pathlib import Path
import subprocess
import sys

from native_app import ROOT, build_host
import neo98
import win98_c


def main():
    out = ROOT / 'build/release-content-tests'
    out.mkdir(exist_ok=True)
    env = dict(neo98.tool_environment(), SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_DEADLINE='120')
    dll = win98_c.path(r'C:\WINDOWS\SYSTEM\COMDLG32.DLL')
    font = win98_c.path(r'C:\WINDOWS\FONTS\SSERIFE.FON')
    notepad = win98_c.path(r'C:\WINDOWS\NOTEPAD.EXE')
    fixed = ROOT / 'build/generated/notepad-fixedsys.fnt'
    def run(name, command):
        with (out / (name + '.log')).open('w') as log:
            result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env,
                                    stdout=log, stderr=subprocess.STDOUT, timeout=600)
        if result.returncode:
            print((out / (name + '.log')).read_text()[-6000:])
        result.check_returncode()
        print(name + ' PASS', flush=True)
    for source, name, arguments in (
        ('test_runtime_content.cpp', 'test-runtime-content', [dll, font]),
        ('test_notepad_find.cpp', 'test-notepad-find-runtime', [notepad, fixed, out / 'find.ppm', dll, font]),
    ):
        binary = build_host(source, name, runtime_content=True)
        run(name, [binary, *arguments])
    run('file-dialog-fixture', [sys.executable, ROOT / 'tools/test_file_dialog_guest.py', '--out', out / 'file-dialog'])
    binary = build_host('test_file_dialog_guest.cpp', 'test-file-dialog-runtime', runtime_content=True)
    run('test-file-dialog-runtime', [binary, out / 'file-dialog/FILEDLG.EXE', fixed, dll, font])
    print('RELEASE CONTENT PASS', flush=True)


if __name__ == '__main__':
    main()
