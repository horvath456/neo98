#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The children of a message box: its buttons, the icon and the text.

USER builds a message box as a dialog template: the buttons, a STATIC with
SS_ICON (id 10) for the icon and a STATIC (id 0FFFFh, SS_NOPREFIX,
SS_EDITCONTROL) for the text, every rectangle in dialog units. Programs and
hooks find the text by GetDlgItem, GetDlgItemText, EnumChildWindows and
GetWindowText, and a WH_CBT hook sees every control made. Neo98 made the
buttons only and painted icon and text into the box itself.

tools/message_box_children_fixture.c opens five boxes (no icon; the four
icons; a long text of several lines; MB_RIGHT; a text shorter than its icon)
and writes, from its CBT hook and from a timer while each box runs, what it
finds. EXPECTED is the same program measured in Windows 98 SE in a virtual
machine; Windows 95 OSR2 answers every line the same. The test compares every
line but two things of the hook: the class, which Windows' dialog manager
hands over as the atom of the class and Neo98 by its name, and
WS_CLIPSIBLINGS, which Neo98 adds to a popup before the hook sees it and
Windows after.

The single button of an MB_OK box is BS_DEFPUSHBUTTON until the box is
activated again: Windows renumbers it to IDCANCEL but keeps IDOK as the
default id, so an activation that passes the default on takes the style
away (measured both ways, depending on whether another window was in front
for a moment). EXPECTED has the box that kept its activation.

The thread timer that ends each box runs inside the box's modal loop: the
loop dispatches its messages, so a timer's procedure is called there, also
for a timer without a window (Neo98's loop sent each message to its window,
and such a tick was lost).

    build/analysis-venv/bin/python tools/test_message_box_children_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture is built with it; docs/BUILDING.md
        print('SKIP: needs i686-w64-mingw32-gcc')
        _sys.exit(77)

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402

EXPECTED = r'''
m0.cbt0=#32770,id=0,style=80c803c5,272,198,104,100,name="Plain"
m0.cbt1=#49194,id=1,style=50030001,11,41,75,23,name="OK"
m0.cbt2=#49198,id=65535,style=50022080,11,11,77,15,name="Saved."
m0.box=#32770,client=98x75,window=272,198,104,100,work=0,0,640,452
m0.child0=Button,id=2,style=50030001,11,41,75,23,text="OK",gettext=2,length=2
m0.child1=Static,id=65535,style=50022080,11,11,77,15,text="Saved.",gettext=6,length=6
m0.dlgitem.ffff=1
m0.dlgitemtext.ffff="Saved."
m0.dlgitem.10=0
m0.dlgitem.20=0
m0.result=1
m1.cbt0=#32770,id=0,style=80c803c5,203,189,242,119,name="Question"
m1.cbt1=#49194,id=6,style=50030001,39,60,75,23,name="&Yes"
m1.cbt2=#49194,id=7,style=50010000,120,60,75,23,name="&No"
m1.cbt3=#49198,id=10,style=50020003,11,11,0,0,name="\ff\02\7f"
m1.cbt4=#49198,id=65535,style=50022080,59,20,168,15,name="Do you want "
m1.box=#32770,client=236x94,window=203,189,242,119,work=0,0,640,452
m1.child0=Button,id=6,style=50030001,39,60,75,23,text="&Yes",gettext=4,length=4
m1.child1=Button,id=7,style=50010000,120,60,75,23,text="&No",gettext=3,length=3
m1.child2=Static,id=10,style=50020003,11,11,32,32,text="\ff\02\7f",gettext=3,length=3
m1.child3=Static,id=65535,style=50022080,59,20,168,15,text="Do you want to save the changes?",gettext=32,length=32
m1.dlgitem.ffff=1
m1.dlgitemtext.ffff="Do you want to save the changes?"
m1.dlgitem.10=1
m1.dlgitem.20=0
m1.result=7
m2.cbt0=#32770,id=0,style=80c803c5,128,179,392,139,name="Exclamation"
m2.cbt1=#49194,id=1,style=50030001,116,80,75,23,name="OK"
m2.cbt2=#49194,id=2,style=50010000,197,80,75,23,name="Cancel"
m2.cbt3=#49198,id=10,style=50020003,11,11,0,0,name="\ff\03\7f"
m2.cbt4=#49198,id=65535,style=50022080,59,11,318,54,name="The disk is "
m2.box=#32770,client=386x114,window=128,179,392,139,work=0,0,640,452
m2.child0=Button,id=1,style=50030001,116,80,75,23,text="OK",gettext=2,length=2
m2.child1=Button,id=2,style=50010000,197,80,75,23,text="Cancel",gettext=6,length=6
m2.child2=Static,id=10,style=50020003,11,11,32,32,text="\ff\03\7f",gettext=3,length=3
m2.child3=Static,id=65535,style=50022080,59,11,318,54,text="The disk is full. Free some space on the disk and try again, or save the document on another disk. This text is long enough to need several lines in its box.\0d\0aAnd a line of its own.",gettext=181,length=181
m2.dlgitem.ffff=1
m2.dlgitemtext.ffff="The disk is full. Free some space on the disk and try again, or save the document on another disk. This text is long enough to need several lines in its box.\0d\0aAnd a line of its own."
m2.dlgitem.10=1
m2.dlgitem.20=0
m2.result=2
m3.cbt0=#32770,id=0,style=80c803c5,204,189,239,119,name="Hand"
m3.cbt1=#49194,id=4,style=50030001,38,60,75,23,name="&Retry"
m3.cbt2=#49194,id=2,style=50010000,119,60,75,23,name="Cancel"
m3.cbt3=#49198,id=10,style=50020003,11,11,0,0,name="\ff\01\7f"
m3.cbt4=#49198,id=65535,style=50022080,59,20,165,15,name="Right aligne"
m3.box=#32770,client=233x94,window=204,189,239,119,work=0,0,640,452
m3.child0=Button,id=4,style=50030001,38,60,75,23,text="&Retry",gettext=6,length=6
m3.child1=Button,id=2,style=50010000,119,60,75,23,text="Cancel",gettext=6,length=6
m3.child2=Static,id=10,style=50020003,11,11,32,32,text="\ff\01\7f",gettext=3,length=3
m3.child3=Static,id=65535,style=50022080,59,20,165,15,text="Right aligned & with an ampersand",gettext=33,length=33
m3.dlgitem.ffff=1
m3.dlgitemtext.ffff="Right aligned & with an ampersand"
m3.dlgitem.10=1
m3.dlgitem.20=0
m3.result=2
m4.cbt0=#32770,id=0,style=80c803c5,191,179,266,139,name="Asterisk"
m4.cbt1=#49194,id=3,style=50030001,11,80,75,23,name="&Abort"
m4.cbt2=#49194,id=4,style=50010000,92,80,75,23,name="&Retry"
m4.cbt3=#49194,id=5,style=50010000,173,80,75,23,name="&Ignore"
m4.cbt4=#49198,id=10,style=50020003,11,11,0,0,name="\ff\04\7f"
m4.cbt5=#49198,id=65535,style=50022080,59,11,192,54,name="Line one\0d\0aLi"
m4.box=#32770,client=260x114,window=191,179,266,139,work=0,0,640,452
m4.child0=Button,id=3,style=50030001,11,80,75,23,text="&Abort",gettext=6,length=6
m4.child1=Button,id=4,style=50010000,92,80,75,23,text="&Retry",gettext=6,length=6
m4.child2=Button,id=5,style=50010000,173,80,75,23,text="&Ignore",gettext=7,length=7
m4.child3=Static,id=10,style=50020003,11,11,32,32,text="\ff\04\7f",gettext=3,length=3
m4.child4=Static,id=65535,style=50022080,59,11,192,54,text="Line one\0d\0aLine two\0d\0aLine three\0d\0aLine four",gettext=41,length=41
m4.dlgitem.ffff=1
m4.dlgitemtext.ffff="Line one\0d\0aLine two\0d\0aLine three\0d\0aLine four"
m4.dlgitem.10=1
m4.dlgitem.20=0
m4.result=5
done=1
'''
CLASSES = {'Button': 'BUTTON', 'Static': 'STATIC'}


def lines(text):
    return dict(line.split('=', 1) for line in text.strip().splitlines() if '=' in line)


def hook_line(key, value, classes):
    """A line of the CBT hook without what Windows and Neo98 hand over
    differently: the class (an atom in Windows, the name in Neo98; the
    name the child of the same place answers) and WS_CLIPSIBLINGS."""
    klass, rest = value.split(',', 1)
    style = re.search(r'style=([0-9a-f]{8})', rest)
    if style:
        rest = rest.replace(style.group(0), f'style={int(style.group(1), 16) & ~0x04000000:08x}')
    return classes.get(key, klass) + ',' + rest


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('message-box-children')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', '-a', '--reflink=auto', f'{pristine}/.', str(drive)], check=True)
    if drive.is_symlink():
        raise SystemExit(f'{drive} is a link')
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/message_box_children_fixture.c'),
                    '-ffreestanding', '-fno-builtin', '-nostartfiles', '-nostdlib', '-Wl,-e,_mainCRTStartup',
                    '-mwindows', '-luser32', '-lkernel32', '-lgdi32', '-o', str(drive / 'MBCHILD.EXE')], check=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='90',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\MBCHILD.EXE', '--quiet',
                        '--desktop', '640x480', '--system-tree', str(drive), '--drive-c', str(drive),
                        '--registry', str(out / 'c.registry'), '--crash-report-dir', str(out / 'reports')],
                       cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=240)
    written = win98_c.path('C:\\MBCHILD.TXT', drive)
    got = lines(written.read_text(errors='replace')) if written else {}
    shutil.rmtree(drive, ignore_errors=True)
    expected = lines(EXPECTED)
    # The class each hook line stands for: the hook sees the dialog and
    # then the children in the order EnumChildWindows finds them.
    classes = {}
    for key in expected:
        match = re.match(r'(m\d)\.cbt(\d+)$', key)
        if match:
            box, index = match.group(1), int(match.group(2))
            child = expected.get(f'{box}.child{index - 1}', '#32770') if index else '#32770'
            name = child.split(',')[0]
            classes[key] = CLASSES.get(name, name)
    checks = {'the fixture ran to its end': got.get('done') == '1'}
    for key, value in expected.items():
        if key == 'done':
            continue
        mine = got.get(key)
        if '.cbt' in key:
            value = hook_line(key, value, classes)
            mine = hook_line(key, mine, {}) if mine else mine
        same = mine == value
        label = f'{key}: as in Windows 98'
        checks[label if same else f'{label}\n      got      {mine}\n      expected {value}'] = same
    extra = sorted(set(got) - set(expected))
    checks[f'no window more than in Windows: {extra}'] = not extra
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    log:', out / 'log.txt')
    print(f'message box children host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
