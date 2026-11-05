#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The mouse wheel and the trackpad in the host, without a window.

A wheel sends whole notches, WM_MOUSEWHEEL with WHEEL_DELTA each. A trackpad
sends many small steps in points of the fingers' way and goes on after the
fingers left (momentum). The host turns the points into units of the wheel in
proportion to the way (tools/host_wheel.h), and USER hands them on in whole
lines to the window under the pointer (User::RouteWheel,
"Mouse wheel"). The steps of NEO98_HOST_INPUT: wheel=N are notches,
scroll=UP,RIGHT[,begin|momentum] a step of the trackpad in points.

1. Notepad over setup's C: with a text of long lines: a notch scrolls three
   lines; a step of a third of a notch scrolls one line and three of them
   what the notch did; steps too small for a line add up; a change of
   direction forgets what was collected; steps sideways scroll with the
   horizontal bar and back; momentum that arrives when the pointer has left
   the window scrolls nothing; with the Find dialog in the focus the text
   under the pointer scrolls, and with NEO98_WHEEL_FOCUS=1 (the setting
   "wie Win98") it does not.
2. A list box that does not know the wheel (the 16-bit File Manager,
   WINFILE.EXE): DefWindowProc turns the unhandled message into WM_VSCROLL
   for the window with the scroll bar, line by line.
3. A program that only knows whole notches (a fixture window that divides the
   delta by WHEEL_DELTA): it gets the notches it missed and whole notches
   from then on.
4. RetroZilla, where the user's folder has it (build/win98-c, cloned first
   and only read): Gecko divides by the lines of SPI_GETWHEELSCROLLLINES, so
   a notch, single lines and a run with momentum scroll the page and the
   same way back shows the top.

    build/analysis-venv/bin/python tools/test_wheel_host.py [--no-build]

The tree view of the Device Manager under the wheel is part of
tools/test_device_manager_host.py, Explorer's list view of
tools/test_host_wheel.py. Results in build/wheel-host/results.json.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
from test_device_manager_host import failures, host  # noqa: E402

PAGE = ('<html><head><title>Wheel</title></head><body bgcolor=white>'
        + ''.join(f'<p>line {i:03d} of the wheel test</p>\n' for i in range(300)) + '</body></html>')


def picture(run, name, box):
    """The pixels of a screenshot inside `box`, hashed: what the window shows."""
    data = (run / f'{name}.ppm').read_bytes()
    parts = data.split(b'\n', 3)
    width, _height = map(int, parts[1].split())
    left, top, right, bottom = box
    rows = b''.join(parts[3][(y * width + left) * 3:(y * width + right) * 3] for y in range(top, bottom))
    return hashlib.sha256(rows).hexdigest()


class Steps:
    """Steps at rising times after a wait for a window."""

    def __init__(self, window, start=4.0):
        self.steps, self.time = [f'0:wait={window}'], start

    def add(self, action, pause=0.5, before=0.0):
        self.time += before
        self.steps.append(f'{self.time:.1f}:{action}')
        self.time += pause
        return self


# The text of Notepad without the column of the caret and the scroll bars.
TEXT = (120, 105, 700, 520)


def boxes(text):
    """The first visible row of every visible list box at each lists=1: [[first]]."""
    out = []
    for line in text.splitlines():
        if line.startswith('NEO98 HOST input lists'):
            out.append([])
        match = re.match(r'NEO98 HOST listbox hwnd=\w+ rows=(\d+) first=(\d+) horizontal=\d+ visible=1$', line)
        if match and out and int(match[1]) > 1:
            out[-1].append(int(match[2]))
    return out


def prepare_retrozilla_profile(run):
    # A previous user's browser crash must not prompt before the test page.
    # Session Manager removes this flag on an ordinary browser shutdown.
    profiles = win98_c.path(r'C:\WINDOWS\Application Data\RetroZilla\Profiles', run / 'c')
    if profiles:
        for prefs in profiles.rglob('prefs.js'):
            lines = prefs.read_bytes().splitlines(keepends=True)
            prefs.write_bytes(b''.join(line for line in lines if not re.match(
                rb'\s*user_pref\("extensions\.sessionmanager\._running",', line)))
            # Crash Recovery also stores its shutdown state outside prefs.js.
            recovery = prefs.parent / 'crashrecovery.dat'
            if recovery.is_file():
                recovery.write_bytes(re.sub(rb'(?m)^state=(?:running|crashed)(\r?)$',
                                            rb'stopped\1', recovery.read_bytes(), count=1))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n', 1)[0])
    parser.add_argument('--no-build', action='store_true')
    arguments = parser.parse_args()
    live = win98_c.live_folder()
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('wheel-host')
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if arguments.no_build else build_host()
    checks, report = {}, {}

    def check(name, condition):
        checks[name] = bool(condition)
        print(('ok  ' if condition else 'FAIL'), name, flush=True)

    # 1. Notepad: the multiline edit control.
    staged = out / 'text'
    staged.mkdir(exist_ok=True)
    (staged / 'LONG.TXT').write_bytes(''.join(
        f'line {i:03d} ' + ' '.join(f'word{i:03d}x{k:02d}' for k in range(20)) + '\r\n' for i in range(150)).encode())
    names = ('top', 'notch', 'line', 'lines', 'small', 'small-more', 'turned', 'find-line', 'momentum-away',
             'find', 'find-scrolled')

    def notepad(name, focus_mode=False):
        steps = Steps('Notepad')

        def gesture(*points, then=None, momentum=0):
            """The steps of one gesture a tenth of a second apart, then a picture."""
            for i, step in enumerate(points):
                steps.add(f'scroll={step}' + (',begin' if not i else ',momentum' if i >= len(points) - momentum else ''), 0.1)
            if then:
                steps.add(f'shot={{out}}/{then}.ppm', 0.5, before=1.0)

        steps.add('click=400,300').add('key=24').add('move=400,300').add('shot={out}/top.ppm', 0.5)
        steps.add('wheel=-1', 1.2).add('shot={out}/notch.ppm', 0.5).add('wheel=1', 1.2)
        gesture('-13.4,0', then='line')
        gesture('-13.4,0', '-13.4,0', then='lines')
        gesture('40.2,0')                                           # the top again
        steps.time += 1.0
        gesture('-9,0', then='small')                               # 27 units: no line yet
        gesture('-9,0', '-9,0', then='small-more')                  # 54: one line, 14 kept
        # 27 more would be a second line with the 14 kept; back and forth they are gone.
        gesture('13.4,0', '-9,0', '9,0', '-9,0', then='turned')     # the top, then 27 each way: nothing
        # The Find dialog takes the focus (F3 without a search text); the pointer is over the text.
        steps.add('key=72', 1.0).add('wait=|Find', 1.0).add('move=600,450', 0.5).add('shot={out}/find.ppm', 0.5)
        gesture('-13.4,0', then='find-line')
        gesture('13.4,0')
        # Momentum that arrives when the pointer is over another window, the dialog.
        steps.add('scroll=-13.4,0,begin', 0.2, before=1.0).add('move=300,280', 0.2)
        for _ in range(6):
            steps.add('scroll=-13.4,0,momentum', 0.1)
        steps.add('move=600,450', 0.5, before=0.8).add('shot={out}/momentum-away.ppm', 0.5)
        gesture('13.4,0')
        gesture('-40.2,0', then='find-scrolled')
        environment = {'NEO98_WHEEL_FOCUS': '1'} if focus_mode else {}
        os.environ.update(environment)
        try:
            return host(binary, out, name, 'C:\\WINDOWS\\NOTEPAD.EXE', 'NOTEPAD.EXE E:\\LONG.TXT', steps.steps,
                        int(steps.time) + 20, pristine, win98_c.journal(pristine), drives=(f'E={staged}',))
        finally:
            for key in environment:
                os.environ.pop(key, None)

    code, text, run = notepad('notepad')
    if all((run / f'{name}.ppm').is_file() for name in names):
        # The window without the caret's line of the screen's edge and the pointer.
        shots = {name: picture(run, name, TEXT) for name in names}
        report['notepad'] = shots
        check('Notepad: a notch scrolls the text', shots['notch'] != shots['top'])
        check('Notepad: a step of a third of a notch scrolls a line', shots['line'] not in (shots['top'], shots['notch']))
        check('Notepad: three such steps scroll what a notch does', shots['lines'] == shots['notch'])
        check('Notepad: a step too small for a line scrolls nothing', shots['small'] == shots['top'])
        check('Notepad: the next one adds up to a line', shots['small-more'] == shots['line'])
        check('Notepad: a change of direction forgets what was collected', shots['turned'] == shots['top'])
        find = {name: picture(run, name, (120, 360, 560, 520)) for name in ('find', 'find-line', 'momentum-away', 'find-scrolled')}
        check('Notepad: with the Find dialog in the focus the text under the pointer scrolls',
              len({find['find'], find['find-line'], find['find-scrolled']}) == 3)
        check('Notepad: momentum does not run on when the pointer is over another window by then',
              find['momentum-away'] == find['find-line'])
    else:
        check('Notepad: the screenshots were taken', False)
    check('Notepad: no call the host does not carry', not failures(text))
    code, text, run = notepad('notepad-focus', focus_mode=True)
    if all((run / f'{name}.ppm').is_file() for name in ('find', 'find-scrolled', 'top', 'notch')):
        find = {name: picture(run, name, (120, 360, 560, 520)) for name in ('find', 'find-scrolled')}
        check('wheel.focus (wie Win98): with the Find dialog in the focus the text does not scroll',
              find['find'] == find['find-scrolled'])
        check('wheel.focus: a notch scrolls the focused text as before',
              picture(run, 'notch', TEXT) != picture(run, 'top', TEXT))
    else:
        check('wheel.focus: the screenshots were taken', False)

    # 2. A list box under the wheel, in a 16-bit program: File Manager.
    steps = Steps('WFS_Frame', 6.0)
    # Reset only the viewport; keep the selected directory's file columns.
    steps.add('move=100,250', 0.5).add('wheel=100', 1.0).add('lists=1', 0.5)
    steps.add('scroll=-13.4,0,begin', 1.0).add('lists=1', 0.5)
    steps.add('scroll=-26.8,0,begin', 1.0).add('lists=1', 0.5)
    steps.add('wheel=-1', 1.0).add('lists=1', 0.5)
    steps.add('wheel=2', 1.0).add('lists=1', 0.5)
    steps.add('move=400,250', 0.5)                            # over the files, columns side by side
    steps.add('scroll=0,53.6,begin', 1.0).add('lists=1', 0.5)
    steps.add('scroll=0,-53.6,begin', 1.0).add('lists=1', 0.5).add('shot={out}/winfile.ppm')
    def winfile_tree(run):
        # Keep six rows of scrolling available regardless of the imported C:.
        for index in range(32):
            (run / 'c' / f'WHEEL{index:03d}').mkdir(exist_ok=True)

    code, text, run = host(binary, out, 'winfile', 'C:\\WINDOWS\\WINFILE.EXE', 'WINFILE.EXE',
                           steps.steps, int(steps.time) + 25, pristine, win98_c.journal(pristine),
                           prepare=winfile_tree)
    firsts = boxes(text)
    report['winfile'] = firsts
    columns = list(zip(*firsts)) if len(firsts) == 7 and len({len(row) for row in firsts}) == 1 else []
    check('File Manager (16-bit): its list boxes were read seven times', bool(columns))
    check('File Manager: the tree under the pointer scrolls by one line, two more, three more and back to the top',
          any(column[:5] == (0, 1, 3, 6, 0) and column[5:] == (0, 0) for column in columns))
    check('File Manager: steps to the right scroll the columns of the files, and back',
          any(column[:5] == (0, 0, 0, 0, 0) and column[5] > 0 and column[6] == 0 for column in columns))
    check('File Manager: no call the host does not carry', not failures(text))

    # 3. A program that only knows whole notches.
    fixture = out / 'fixture'
    fixture.mkdir(exist_ok=True)
    env = neo98.tool_environment()
    libraries = []
    for dll, names in {'KERNEL32': {'ExitProcess': 4}, 'USER32': {
            'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16, 'GetMessageA': 16,
            'DispatchMessageA': 4, 'SetWindowTextA': 8, 'SetForegroundWindow': 4}}.items():
        definition, library = fixture / (dll + '.def'), fixture / (dll + '.lib')
        definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' + ''.join(f'{n}@{size}\n' for n, size in names.items()))
        subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)], env=env, check=True)
        libraries.append(str(library))
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-c', str(ROOT / 'tools/wheel_notch_fixture.c'), '-o', str(fixture / 'fixture.obj')],
                   env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0', '/nodefaultlib',
                    '/machine:x86', '/out:' + str(fixture / 'NOTCH.EXE'), str(fixture / 'fixture.obj'), *libraries],
                   env=env, check=True)
    steps = Steps('WHEEL_NOTCH|notches ready', 2.0)
    steps.add('move=200,200', 0.5)
    for i in range(6):                       # two notches in six lines: nothing happens
        steps.add('scroll=-13.4,0' + (',begin' if not i else ''), 0.1)
    steps.add('tops=1', 0.6, before=0.4)
    steps.add('scroll=-13.4,0,begin', 0.8).add('tops=1', 0.5)    # the next step shows it: the two notches come
    for i in range(3):                       # and a third, whole, out of three lines
        steps.add('scroll=-13.4,0' + (',begin' if not i else ''), 0.1)
    steps.add('tops=1', 0.5, before=0.6)
    steps.add('wheel=1', 0.8).add('tops=1', 0.5)
    code, text, run = host(binary, out, 'notches', 'E:\\NOTCH.EXE', 'NOTCH.EXE', steps.steps, int(steps.time) + 15,
                           pristine, win98_c.journal(pristine), drives=(f'E={fixture}',))
    titles = re.findall(r'class=WHEEL_NOTCH title=(.*)', text)
    report['notches'] = titles
    check('a program of whole notches: small steps alone do nothing', titles[:1] == ['notches ready'])
    check('a program of whole notches: it gets the two notches it missed in one message',
          titles[1:2] == ['notches -2 in 1 small 6'])
    check('a program of whole notches: whole notches from then on', titles[2:3] == ['notches -3 in 2 small 6'])
    check('a program of whole notches: a notch of a wheel as ever', titles[3:4] == ['notches -2 in 3 small 6'])
    check('a program of whole notches: no call the host does not carry', not failures(text))

    # 4. RetroZilla of the user's folder.
    program = 'C:\\Program Files\\RetroZilla\\retrozilla.exe'
    if win98_c.path(program, live) is None:
        print('skip RetroZilla: not in', live)
        report['retrozilla'] = 'skipped'
    else:
        steps = Steps('|Wheel - RetroZilla', 8.0)
        steps.add('move=500,400', 1.0).add('shot={out}/top.ppm', 1.0)
        steps.add('wheel=-1', 3.0).add('shot={out}/notch.ppm', 1.0).add('wheel=1', 3.0)
        steps.add('scroll=-13.4,0,begin', 2.5).add('shot={out}/line.ppm', 1.0)
        steps.add('scroll=-13.4,0', 0.2).add('scroll=-13.4,0', 2.5).add('shot={out}/lines.ppm', 1.0)
        for i in range(15):                                     # a swipe that runs out
            steps.add('scroll=-20,0' + (',momentum' if i > 5 else ''), 0.1)
        steps.add('shot={out}/trackpad.ppm', 3.0, before=3.0)
        for i in range(17):
            steps.add('scroll=20.01,0' + (',begin' if not i else ''), 0.1)
        steps.add('shot={out}/back.ppm', 1.0, before=4.0)
        steps = steps.steps
        staged = out / 'retrozilla-page'
        staged.mkdir(exist_ok=True)
        (staged / 'WHEEL.HTM').write_text(PAGE)
        code, text, run = host(binary, out, 'retrozilla', program, f'"{program}" file:///E:/WHEEL.HTM', steps, 110,
                               live, win98_c.journal(live), drives=(f'E={staged}',),
                               prepare=prepare_retrozilla_profile)
        names = ('top', 'notch', 'line', 'lines', 'trackpad', 'back')
        if all((run / f'{name}.ppm').is_file() for name in names):
            shots = {name: picture(run, name, (0, 120, 1000, 740)) for name in names}
            report['retrozilla'] = shots
            check('RetroZilla: a notch scrolls the page', shots['notch'] != shots['top'])
            check('RetroZilla: a step of a third of a notch scrolls a line', shots['line'] not in (shots['top'], shots['notch']))
            check('RetroZilla: three such steps scroll what a notch does', shots['lines'] == shots['notch'])
            check('RetroZilla: a swipe with momentum scrolls on', shots['trackpad'] not in (shots['lines'], shots['top']))
            check('RetroZilla: the same way back shows the top', shots['back'] == shots['top'])
        else:
            check('RetroZilla: the page opened and the screenshots were taken', False)
        check('RetroZilla: no call the host does not carry', not failures(text))
    report['checks'] = checks
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'wheel host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
