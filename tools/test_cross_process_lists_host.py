#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""List box, combo box and edit messages with a pointer, sent to another process.

USER of Windows 95 and 98 carries the pointer of such a message to a control
of another process: USER32 hands the message to the 16-bit USER, whose thunk
knows the control's messages by its class and copies the string, the text,
the rectangle or the array into its own memory and back. CB_SELECTSTRING,
LB_GETTEXT, EM_GETLINE and their fellows therefore work across processes; an
owner-drawn list or combo box without LBS_HASSTRINGS or CBS_HASSTRINGS takes
the value itself. Neo98 copied only the strings of WM_SETTEXT and WM_GETTEXT
and the block of WM_COPYDATA, and every other pointer reached the other
process as a number of the sender's memory.

tools/cross_process_lists_fixture.c starts itself as a server with a combo
box, a list box, an edit, two owner-drawn boxes and a list of extended
selection, sends them the messages and writes the answers and the buffers;
EXPECTED is the same program measured in Windows 98 SE in a virtual machine,
and Windows 95 OSR2 answers every line the same. Then it starts
tools/cross_process_lists_win16_fixture.asm, a 16-bit program that sends
CB_ADDSTRING, CB_FINDSTRINGEXACT, CB_GETLBTEXT, LB_ADDSTRING, LB_GETTEXT and
EM_GETLINE to the same server with the 16-bit message numbers.

Measured on the way and done too: WM_GETTEXT to another process gets
the whole buffer back, zeros after the text; LB_ADDFILE, which ended
Neo98's list box ("unsupported listbox message"), looks the file up as USER
does; the entries of LB_DIR and LB_ADDFILE are in lower case. In Windows
EM_GETLINE to an edit of another process makes its sender fault; the
fixture sends it only when asked to ("getline", as here).

    build/analysis-venv/bin/python tools/test_cross_process_lists_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc', 'nasm'] if not _shutil.which(_t)]
    if _missing:  # the fixtures are built with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)

import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win16_fixture  # noqa: E402
import win98_c  # noqa: E402

EXPECTED = r'''
server=1
c.add0=0
c.add1=1
c.add2=2
c.insert=1
c.count=4
c.find=3
c.findexact=2
c.select=1
c.cursel=1
c.textlen=4
c.text=4,"Beta\00###"
c.gettext=8,"Inserted\00\00\00\00\00\00\00\00####"
c.seteditsel=1
c.geteditsel=40001,1,4
c.droppedrect=1,0,0,150,120
c.dir=6
c.dircount=7
c.dirtext=5,"a.txt\00"
l.add0=0
l.add1=1
l.add2=2
l.insert=0
l.count=4
l.find=3
l.findexact=2
l.select=1
l.cursel=1
l.textlen=5
l.text=5,"Three\00##"
l.itemrect=1,0,16,148,32
l.tabstops=0
l.dir=6
l.dircount=7
l.addfile=-1
l.dirtext=5,"a.txt\00"
l.addfile2=7
l.addedtext=5,"c.txt\00"
e.settext=1
e.replacesel=1
e.text=20,"XY line\0d\0aSecond line\00"
e.getsel=20002,2,2
e.tabstops=1
o.add0=0
o.add1=1
o.data=43981
o.text=4,12345678
o.find=1
o.select=1
o.cursel=1
ol.add0=0
ol.add1=1
ol.text=4,5566
ol.find=1
x.range=0
x.selcount=2
x.selitems=2,1,2,-1,-1
m.minmax=0,640,480,123,45
'''
# The formatting rectangle of the edit is the edit's own matter: Windows'
# is two points wider, and EM_SETRECT insets the rectangle and cuts it to
# whole lines and answers 1 there. Here only that the rectangles crossed
# both ways is compared (not the fill of the sender's buffer, 77h).
RECTANGLES = ('e.getrect', 'e.getrect2')


def lines(text):
    return dict(line.split('=', 1) for line in text.strip().splitlines() if '=' in line)


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('cross-process-lists')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', '-a', '--reflink=auto', f'{pristine}/.', str(drive)], check=True)
    if drive.is_symlink():
        raise SystemExit(f'{drive} is a link')
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/cross_process_lists_fixture.c'),
                    '-ffreestanding', '-fno-builtin', '-nostartfiles', '-nostdlib', '-Wl,-e,_mainCRTStartup',
                    '-mwindows', '-luser32', '-lkernel32', '-lgdi32', '-o', str(drive / 'XPROC.EXE')], check=True)
    win16_fixture.assemble(ROOT / 'tools/cross_process_lists_win16_fixture.asm', out / 'XP16.EXE', 'XP16',
                           neo98.tool_environment())
    shutil.copyfile(out / 'XP16.EXE', drive / 'XP16.EXE')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='120',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\XPROC.EXE',
                        '--command', 'C:\\XPROC.EXE getline', '--quiet',
                        '--desktop', '640x480', '--system-tree', str(drive), '--drive-c', str(drive),
                        '--registry', str(out / 'c.registry'), '--crash-report-dir', str(out / 'reports')],
                       cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=300)
    written = win98_c.path('C:\\XPROC.TXT', drive)
    got = lines(written.read_text(errors='replace')) if written else {}
    sixteen = win98_c.path('C:\\XP16OUT.TXT', drive)
    data = sixteen.read_bytes() if sixteen else b''
    shutil.rmtree(drive, ignore_errors=True)
    expected = lines(EXPECTED)
    checks = {'the fixture ran to its end': got.get('done') == '1'}
    for key, value in expected.items():
        mine = got.get(key)
        same = mine == value
        label = f'{key}: as in Windows'
        checks[label if same else f'{label}\n      got      {mine}\n      expected {value}'] = same
    for key in RECTANGLES:
        fields = (got.get(key) or '').split(',')
        crossed = len(fields) == 5 and fields[0] == '1' and all(f.lstrip('-').isdigit() and int(f) != 0x77777777
                                                                 for f in fields)
        checks[f'{key}: the rectangle came back ({got.get(key)})'] = crossed
    # EM_GETLINE: in Windows 95 and 98 its sender faults when the edit is
    # another process's; here the line comes back as from an edit of the
    # same process: no end after it, and no more than the first word allows.
    checks[f'EM_GETLINE gives line 1 ({got.get("e.getline")})'] = got.get('e.getline') == '11,"Second line##"'
    checks[f'EM_GETLINE with room for 4 gives 4 ({got.get("e.getlineshort")})'] = \
        got.get('e.getlineshort') == '4,"Firs##"'
    # The 16-bit sender: words and buffers of 16 bytes (the .asm says which).
    if len(data) == 2 * 4 + 16 + 2 * 2 + 16 + 2 + 16:
        found, combo_add, combo_find, combo_get = struct.unpack_from('<4H', data, 0)
        combo_text = data[8:24]
        list_add, list_get = struct.unpack_from('<2H', data, 24)
        list_text = data[28:44]
        edit_get, = struct.unpack_from('<H', data, 44)
        edit_text = data[46:62]
        # The combo box holds Alpha, Inserted, Beta, Gamma and the three files
        # of the directory, the list box Zero, One, Two, Three, the three
        # files and C.TXT once more.
        checks.update({
            f'16 bits: the server and its controls are found ({found})': found == 1,
            f'16 bits: CB_ADDSTRING "W16" answers 7 ({combo_add})': combo_add == 7,
            f'16 bits: CB_FINDSTRINGEXACT "w16" finds 7 ({combo_find})': combo_find == 7,
            f'16 bits: CB_GETLBTEXT gives "W16" and its end ({combo_get}, {combo_text[:6]})':
                combo_get == 3 and combo_text[:6] == b'W16\0##',
            f'16 bits: LB_ADDSTRING "L16" answers 8 ({list_add})': list_add == 8,
            f'16 bits: LB_GETTEXT gives "L16" and its end ({list_get}, {list_text[:6]})':
                list_get == 3 and list_text[:6] == b'L16\0##',
            f'16 bits: EM_GETLINE gives the second line without an end ({edit_get}, {edit_text})':
                edit_get == 11 and edit_text[:13] == b'Second line##',
        })
    else:
        checks[f'16 bits: the program wrote its results ({len(data)} bytes)'] = False
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    log:', out / 'log.txt')
    print(f'cross process lists host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
