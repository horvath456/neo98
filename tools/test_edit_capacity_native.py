#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""The original Notepad with a file over 4.095 characters,

The native edit control used to hold 4.095 characters, so every program that
showed a real file showed a cut one. USER.EXE keeps the text of an edit control
in a movable block of a local heap and the only limit is the one EM_LIMITTEXT
sets, so the buffer of this port grows with the text: 32 KiB for a single line
control and 64 KiB for a multi line one on the 16-bit side, and what
EM_SETLIMITTEXT names on the 32-bit side.

The evidence is the unchanged original NOTEPAD.EXE of the tree, started on a
file of 20.000 characters. The edit control of its window has to carry every
one of them. The negative probe is the same run against the old ceiling: a text
of 4.095 characters would be a cut file and the test says so.

    build/analysis-venv/bin/python tools/test_edit_capacity_native.py
"""
import re
import subprocess
import sys

from native_app import ROOT, build_fast
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

OUT = ROOT / 'build/edit-capacity'
LINES = 500
COLUMNS = 39  # 39 characters and a line break makes 40 per line, 20.000 in all


def write_file(drive):
    text = ''.join(f'{n:04d} ' + 'abcdefghijklmnopqrstuvwxyz0123456' + '\r\n' for n in range(LINES))
    windows = drive / 'WINDOWS'
    windows.mkdir(parents=True, exist_ok=True)
    path = windows / 'BIG520.TXT'
    path.write_bytes(text.encode('cp1252'))
    # The characters the control holds: the file without the carriage returns.
    return len(text) - text.count('\r')


def windows_of(log):
    found = {}
    for line in re.finditer(r'^PROBE_WINDOW hwnd=(\d+) parent=(\d+) owner=\d+ visible=(\d+) '
                            r'x=(-?\d+) y=(-?\d+) w=(-?\d+) h=(-?\d+) class=([0-9a-f]*) text=([0-9a-f]*)',
                            log, re.M):
        found[int(line[1])] = {'parent': int(line[2]),
                               'class': bytes.fromhex(line[8]).decode('cp1252'),
                               'text': bytes.fromhex(line[9]).decode('cp1252')}
    return found


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    drive = OUT / 'c'
    expected = write_file(drive)
    runner = build_fast('probe_application.cpp', 'probe-edit-capacity')
    command = [str(runner), str(win98_c.FOLDER / 'WINDOWS/NOTEPAD.EXE'),
               str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--drive-c', str(drive),
               '--exe', 'C:\\WINDOWS\\NOTEPAD.EXE',
               '--command', 'NOTEPAD.EXE C:\\WINDOWS\\BIG520.TXT',
               '--current-directory', 'C:\\WINDOWS', '--time-limit', '20']
    result = subprocess.run(command, capture_output=True, text=True, timeout=300, cwd=ROOT)
    log = result.stdout + result.stderr
    (OUT / 'notepad.log').write_text(log)
    stop = re.search(r'^PROBE .*error=(.*)$', log, re.M)
    assert stop and not stop[1], f'Notepad stopped at {stop[1] if stop else "an unknown place"}'
    found = windows_of(log)
    edits = [w for w in found.values() if w['class'].lower() == 'edit']
    assert len(edits) == 1, f'Notepad has {len(edits)} edit controls'
    held = len(edits[0]['text'])
    assert held != 4095, 'the edit control still stops at 4.095 characters'
    assert held == expected, f'the edit control holds {held} of {expected} characters'
    print(f'EDIT capacity: original NOTEPAD.EXE holds all {held} characters of BIG520.TXT')
    return 0


if __name__ == '__main__':
    sys.exit(main())
