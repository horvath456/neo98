#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The activation goes back to the owner when a window closes.

Notepad starts on the pristine C: with the original COMDLG32. Each case
opens one window from the keyboard, closes it and presses ALT+F. In Win98
the window that goes hands the activation on (DestroyWindow,
ShowWindow, EndDialog through ActivateWindow ): an
owned pop-up to its owner, any other window to the previous active window
or the next top-level window. Notepad is active again, its caption
is drawn active, and ALT+F opens the File menu.

    find Search, Find (modeless, owned), ESC
    about Help, About (modal ShellAbout), ENTER
    saveas File, Save As (modal, COMDLG32), ESC
    message text, File, New: "save changes?" (MessageBox), ESC

    build/analysis-venv/bin/python tools/test_activation_handoff_host.py [--no-build] [case...]

Results in build/activation-handoff-host/results.json, screenshots beside it.
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

# A point in Notepad's caption left of the title text: the active caption is
# COLOR_ACTIVECAPTION (000080), the inactive one COLOR_INACTIVECAPTION (808080).
CAPTION = (300, 62)
# Win98 SE draws captions as a gradient (COLOR_ACTIVECAPTION #000080 to
# COLOR_GRADIENTACTIVECAPTION #1084D0); the inactive one runs grey.
def active_caption(rgb):
    r, b = rgb >> 16 & 255, rgb & 255
    return rgb is not None and r <= 0x10 and b >= 0x80 and b - r >= 0x70


def alt(step, vk):
    return [f'{step}:keydown=a4', f'{step}:key={vk}', f'{step}:keyup=a4']


def menu(step, vk, item):
    """ALT plus the menu's mnemonic, then the item's mnemonic."""
    return [*alt(step, vk), f'{step}:wait=#32768', f'{step}:idle=500', f'{step}:type={item}']


CASES = {
    'find': [*menu(1, '53', 'f'), '1:wait=#32770|Find', '1:idle=1500', '2:type=abc', '2:idle=500',
             '3:key=1b'],
    'about': [*menu(1, '48', 'a'), '1:wait=#32770|About Notepad', '1:idle=1500', '3:key=0d'],
    'saveas': [*menu(1, '46', 'a'), '1:wait=#32770|Save As', '1:idle=1500', '3:key=1b'],
    'message': ['1:type=abc', '1:idle=500', *menu(1, '46', 'n'), '1:wait=#32770|Notepad', '1:idle=1500',
                '3:key=1b'],
}


def image(path):
    data = path.read_bytes()
    fields, at = [], 0
    while len(fields) < 4:
        while data[at:at + 1].isspace():
            at += 1
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


def run(out, name, steps, drive_c, registry, binary):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_KEYMAP'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'C:\\WINDOWS\\NOTEPAD.EXE', '--quiet',
               '--drive-c', str(drive_c), '--registry', str(registry),
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / f'host-{name}.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=1200)
    return result.returncode, log_path.read_text(errors='replace')


def main():
    arguments = [a for a in sys.argv[1:] if not a.startswith('--')]
    names = arguments or list(CASES)
    out = neo98.output_directory('activation-handoff-host')
    drive_c = out / 'drive-c'
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    checks, results = {}, {}
    for name in names:
        shutil.rmtree(drive_c, ignore_errors=True)
        subprocess.run(['cp', '-c', '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
        registry = out / 'session.registry'
        shutil.copyfile(win98_c.journal(win98_c.FOLDER), registry)
        closed, opened = out / f'{name}-closed.ppm', out / f'{name}-menu.ppm'
        for shot in (closed, opened):
            shot.unlink(missing_ok=True)
        steps = ['0:wait=Notepad', '0:idle=1500', *CASES[name], '3:idle=1500', f'3:shot={closed}',
                 *alt(4, '46'), '4:wait=#32768', '4:idle=500', f'4:shot={opened}', '5:key=1b', '5:idle=300',
                 '6:menu=quit']
        code, text = run(out, name, steps, drive_c, registry, binary)
        caption = image(closed)(*CAPTION) if closed.exists() else None
        results[name] = {'steps': steps, 'caption': caption, 'exit': code}
        before, _, after = text.partition(f'screenshot {closed}')
        checks[f'{name}: window opened and closed'] = bool(after) and 'timed out' not in before
        checks[f'{name}: Notepad caption active after closing'] = caption is not None and active_caption(caption)
        checks[f'{name}: ALT+F opens File'] = 'input wait=#32768 reached' in after
        checks[f'{name}: host ended'] = code == 0
    shutil.rmtree(drive_c, ignore_errors=True)
    (out / 'results.json').write_text(json.dumps({'checks': checks, 'results': results},
                                                indent=2, ensure_ascii=False) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'ACTIVATION HANDOFF HOST FAIL: see {out}')
        return 1
    print(f'ACTIVATION HANDOFF HOST PASS: {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
