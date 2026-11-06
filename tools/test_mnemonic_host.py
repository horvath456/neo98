#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Test keyboard mnemonics in Notepad and SYSEDIT through the host.

Check menu underlines, Save As focus transfer, file creation, message-box
mnemonics, and MDI menu forwarding. Use a private copy of the setup drive.
Results and screenshots are written to build/mnemonic-host.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

FIRST, SECOND = b'abc', b'z'
MENU_GREY, HIGHLIGHT, BLACK = 0xc0c0c0, 0x000080, 0x000000
# Notepad's window stands at the same place in every run: the F of File and
# the item Help of its menu bar in screen pixels.
UNDERLINE_ROW, UNDERLINE_X = 92, range(76, 82)
HELP_PIXEL = (190, 84)


def alt(step, vk):
    return [f'{step}:keydown=a4', f'{step}:key={vk}', f'{step}:keyup=a4']


def pixels(path):
    """A P6 PPM as a function of x and y."""
    data = path.read_bytes()
    fields, at = [], 0
    while len(fields) < 4:
        while data[at:at + 1].isspace():
            at += 1
        if data[at:at + 1] == b'#':
            at = data.index(b'\n', at) + 1
            continue
        end = at
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[at:end])
        at = end
    width = int(fields[1])
    at += 1

    def pixel(x, y):
        o = at + (y * width + x) * 3
        return (data[o] << 16) | (data[o + 1] << 8) | data[o + 2]
    return pixel


def run(out, exe, steps, drive_c, registry, binary):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_KEYMAP'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', exe, '--quiet',
               '--drive-c', str(drive_c), '--registry', str(registry),
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / f'host-{exe.rsplit(chr(92), 1)[-1].lower()}.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=900)
    return result.returncode, log_path.read_text(errors='replace'), log_path


def main():
    out = neo98.output_directory('mnemonic-host')
    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    registry = out / 'session.registry'
    shutil.copyfile(win98_c.journal(win98_c.FOLDER), registry)
    shots = {name: out / f'{name}.ppm' for name in ('bar', 'name', 'box', 'about', 'mdi')}
    for shot in shots.values():
        shot.unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    notepad = ['0:wait=Notepad', '0:idle=1500', f'0:shot={shots["bar"]}', '1:type=abc', '1:idle=500',
               # File, Save As from the keyboard.
               *alt(2, '46'), '2:wait=#32768', '2:idle=500', '2:type=a',
               '2:wait=#32770|Save As', '2:idle=1500',
               # TAB away from the file name, ALT+N back, the name replaces the
               # selected text, ALT+S saves.
               '3:key=09', '3:idle=500', *alt(3, '4e'), '3:idle=500', '3:type=C:\\B2690.TXT',
               '3:idle=500', f'3:shot={shots["name"]}', *alt(3, '53'), '3:idle=3000',
               # More text, File, New: the message box asks; N is "&No".
               '4:type=q', '4:idle=500', *alt(4, '46'), '4:wait=#32768', '4:idle=500', '4:type=n',
               '4:wait=#32770|Notepad', '4:idle=1500', f'4:shot={shots["box"]}', '4:type=n', '4:idle=2000',
               # The new document gets its own text and is saved with ENTER.
               '5:type=z', '5:idle=500', *alt(5, '46'), '5:wait=#32768', '5:idle=500', '5:type=a',
               '5:wait=#32770|Save As', '5:idle=1500', '6:type=C:\\B2690B.TXT', '6:key=0d',
               '6:idle=3000',
               # Help, About: the bar item is no longer selected meanwhile.
               *alt(7, '48'), '7:wait=#32768', '7:idle=500', '7:type=a', '7:wait=#32770|About Notepad',
               '7:idle=1500', f'7:shot={shots["about"]}', '7:key=0d', '7:idle=1000', '8:menu=quit']
    sysedit = ['0:wait=|System Configuration Editor', '0:idle=3000', *alt(1, '46'), '1:wait=#32768',
               '1:idle=800', f'1:shot={shots["mdi"]}', '1:key=1b', '1:key=1b', '1:idle=800', '2:menu=quit']
    code_notepad, text, log_notepad = run(out, 'C:\\WINDOWS\\NOTEPAD.EXE', notepad, drive_c, registry, binary)
    code_sysedit, text_sysedit, _ = run(out, 'C:\\WINDOWS\\SYSTEM\\SYSEDIT.EXE', sysedit, drive_c, registry,
                                        binary)

    def saved(name):
        path = next((p for p in drive_c.iterdir() if p.name.upper() == name), None)
        return path.read_bytes() if path else None

    first, second = saved('B2690.TXT'), saved('B2690B.TXT')
    shutil.rmtree(drive_c, ignore_errors=True)
    underline = help_pixel = None
    if shots['bar'].exists():
        bar = pixels(shots['bar'])
        underline = all(bar(x, UNDERLINE_ROW) == BLACK for x in UNDERLINE_X)
    if shots['about'].exists():
        help_pixel = pixels(shots['about'])(*HELP_PIXEL)
    checks = {
        'Notepad open': 'input wait=Notepad reached' in text,
        'the F of File is underlined without ALT': underline is True,
        'Save As opened twice': text.count('input wait=#32770|Save As reached') == 2,
        'ALT+N and ALT+S saved under the typed name': first == FIRST,
        'the message box came': 'input wait=#32770|Notepad reached' in text,
        'N pressed "No": the next file holds only the new text': second == SECOND,
        'About came from Help': 'input wait=#32770|About Notepad reached' in text,
        'Help is no longer selected while About is open': help_pixel == MENU_GREY,
        'SYSEDIT: ALT+F in a child dropped the frame\'s File': 'input wait=#32768 reached' in text_sysedit,
        'screenshots written': all(shot.exists() for shot in shots.values()),
        'hosts ended': code_notepad == 0 and code_sysedit == 0,
    }
    (out / 'results.json').write_text(json.dumps({'notepad': notepad, 'sysedit': sysedit, 'checks': checks,
                                                 'first': repr(first), 'second': repr(second),
                                                 'help_pixel': help_pixel},
                                                indent=2, ensure_ascii=False) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'first: {first!r} second: {second!r} help pixel: {help_pixel}')
        print(f'MNEMONIC HOST FAIL: see {log_notepad}')
        return 1
    print(f'MNEMONIC HOST PASS: {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
