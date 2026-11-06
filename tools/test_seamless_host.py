#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The seamless view of the host, headless.

The host runs under SDL's dummy video driver; NEO98_HOST_SCREEN stands in for
the usable area of the Mac screen (1100x700), and the steps of
NEO98_HOST_INPUT stand in for the Mac pointer (pointer=, press=, release=)
and write the alpha of the overlay's picture (mask=). Three starts, each on a
fresh C: from setup's state with a registry file of its own:

1. tools/host_seamless_fixture.c, two popup windows, one with an elliptic
   window region, started with --seamless: the guest has a desktop of
   1100x700 and the window lies over the screen; the mask is the
   visible region of the two windows; a point outside is the Mac's (click
   through), a point inside reaches the guest in screen pixels; a foreground
   capture receives Win98's extra press and release for an outside click;
   a program mode of 640x480 takes the overlay away and gives it back;
   a size stored by the guest
   (CDS_UPDATEREGISTRY) ends the seamless view; View > Seamless brings it
   back (WM_DISPLAYCHANGE); full screen shows the program in front and leads
   back; the key equivalent Ctrl+Cmd+S ends and starts it. The registry never holds 1100,700. The list of Mac
   windows (windows=) names both guest windows with rectangle and title, and
   a window chosen on the Mac (choose=) comes to the front in the guest.
2. The same C: again: Seamless, then Restart. The restarted host comes up
   seamless without being asked.
3. Explorer after setup's first logon, with its matching registry and
   --seamless: the taskbar is opaque, the desktop
   with its icons is not, the Start menu opens by a press on the Start
   button and is opaque, a press beside it while it is open is the guest's
   and closes it, and the taskbar goes with "Taskbar in seamless mode".

    build/analysis-venv/bin/python tools/test_seamless_host.py [--no-build] [--no-explorer]

Results in build/seamless-host/results.json.
"""
from __future__ import annotations
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: the seamless overlay of the macOS host, its Mac pointer and masks; the Linux host shows seamless windows in X11')  # macOS only; skipped elsewhere
        _sys.exit(77)

import json
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
win98_c.use_pristine()  # tests start from setup's C:, never the user's

IMPORTS = {'KERNEL32': {'ExitProcess': 4, 'GetModuleHandleA': 4, 'Sleep': 4, 'WinExec': 8,
                       'OutputDebugStringA': 4},
           'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16,
                      'GetMessageA': 16, 'TranslateMessage': 4, 'DispatchMessageA': 4,
                      'BeginPaint': 8, 'EndPaint': 8, 'FillRect': 12, 'SetWindowTextA': 8,
                      'ChangeDisplaySettingsA': 8, 'GetSystemMetrics': 4, 'SetWindowRgn': 12,
                      'SetCapture': 4, 'ReleaseCapture': 0, 'SetFocus': 4, 'SetForegroundWindow': 4, 'GetKeyState': 4,
                      'CreatePopupMenu': 0, 'AppendMenuA': 16, 'TrackPopupMenu': 28, 'SendMessageA': 16,
                      'ShowWindow': 8, 'SystemParametersInfoA': 16, 'FindWindowA': 8,
                      'SetCursor': 4, 'LoadCursorA': 8, 'CreateCursor': 28},
           'GDI32': {'CreateSolidBrush': 4, 'CreateEllipticRgn': 16}}
SCREEN = (1100, 700)


def fixture(out):
    environment = neo98.tool_environment()
    dlltool = shutil.which('llvm-dlltool', path=environment['PATH'])
    assert dlltool, 'llvm-dlltool not found'
    libraries = []
    for dll, names in IMPORTS.items():
        definition, library = out / (dll + '.def'), out / (dll + '.lib')
        definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' +
                              ''.join(f'{name}@{bytes_}\n' for name, bytes_ in names.items()))
        subprocess.run([dlltool, '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)],
                       env=environment, check=True)
        libraries.append(str(library))
    obj, exe = out / 'fixture.obj', out / 'HSEAML.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-c', str(ROOT / 'tools/host_seamless_fixture.c'),
                    '-o', str(obj)], env=environment, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0',
                    '/nodefaultlib', '/machine:x86', '/out:' + str(exe), str(obj), *libraries],
                   env=environment, check=True)
    return exe


def image(path):
    """Width, height and a reader of a binary PPM (P6) or PGM (P5)."""
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    width, height = (int(v) for v in size.split())
    channels = 3 if magic == b'P6' else 1

    def at(x, y):
        i = (y * width + x) * channels
        return body[i:i + channels].hex()
    return width, height, at


def main():
    out = neo98.output_directory('seamless-host')
    for name in ('drive-c', 'drive-e', 'reports'):
        shutil.rmtree(out / name, ignore_errors=True)
    for path in list(out.glob('*.ppm')) + list(out.glob('*.pgm')) + list(out.glob('registry*')):
        path.unlink()
    exe = fixture(out)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)

    def run(program, drive, registry, steps, log_name, extra=(), menu=None, timeout=600):
        if not drive.exists():
            subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
        command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                   '--system-tree', '--exe', program, '--quiet', '--drive-c', str(drive),
                   '--registry', str(registry), '--crash-report-dir', str(out / 'reports'), *extra]
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_SCREEN='%dx%d' % SCREEN,
                   NEO98_HOST_DEADLINE=str(timeout - 30),
                   # A window that never paints shows after a second here (five in the host).
                   NEO98_SEAMLESS_GRACE='1000')
        env.pop('NEO98_HOST_ATTACHED', None)
        env.pop('NEO98_HOST_MENU', None)
        if menu:
            env['NEO98_HOST_MENU'] = menu
        log_path = out / log_name
        with log_path.open('w') as log:
            result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                    stdin=subprocess.DEVNULL, timeout=timeout)
        return result.returncode, log_path.read_text(errors='replace')

    def lines(log, pattern):
        return re.findall(pattern, log, re.M)

    wide = '%dx%d' % SCREEN
    drive, registry = out / 'drive-c', out / 'registry'

    # 1. The fixture: mask, pointer, capture, program mode, the guest's own size.
    # It starts seamless, so the windows stand where the fixture put them; a
    # change of the desktop size places them anew, as Win98 does.
    first = [f'0:wait=|screen {wide}', '0:idle=500', f'0:shot={out / "f1.ppm"}', f'0:mask={out / "f1.pgm"}',
             '0:pointer=600,500', '0:pointer=250,150',
             '0:press=600,500', '0:release=600,500',
             '0:press=200,150', '0:wait=|A down 100,70', '0:release=200,150',
             # The layer receives an outside click while the foreground queue
             # holds capture. Win98 posts both down and up to that queue,
             # so only the up title survives the guest turn.
             '0:menu=seamless-windows', '0:idle=200',
             '0:key=43', '0:wait=|captured', '0:pointer=600,500', '0:press=600,500', '0:wait=|A up 500,420',
             '0:release=600,500', '0:key=55', '0:wait=|released', '0:pointer=601,500',
             '0:menu=seamless-windows', '0:idle=200',
             '0:windows=1', '0:choose=B', '0:idle=500', '0:windows=1',
             # This is a system request, not a Delete key for the fixture.
             '0:menu=ctrl-alt-delete', '0:wait=#32770|Close Program', '0:idle=200',
             '0:key=1b', '0:idle=200',
             '0:key=50', '0:wait=|program 640x480', '0:idle=500', f'0:mask={out / "program.pgm"}',
             f'0:shot={out / "program.ppm"}',
             '0:key=44', f'0:wait=|desktop {wide}', '0:idle=500', f'0:mask={out / "f2.pgm"}',
             '0:menu=seamless-taskbar', '0:idle=200', '0:menu=seamless-taskbar', '0:idle=200',
             '0:key=52', '0:wait=|stored 800x600', '0:idle=500',
             '0:menu=seamless', f'0:wait=|screen {wide}', '0:idle=500',
             # Full screen is the program in front and leads back.
             '0:menu=fullscreen', '0:idle=500', '0:menu=fullscreen', '0:idle=500',
             '0:menu=shortcut=ctrl+cmd+s', '0:wait=|screen 800x600', '0:idle=500',
             '0:menu=shortcut=ctrl+cmd+s', f'0:wait=|screen {wide}', '0:idle=500', '0:menu=describe',
             '0:menu=quit']
    code, log = run(str(exe), drive, registry, first, 'host-1.log', extra=['--seamless'])
    waits = lines(log, r'^NEO98 HOST input (\S+)=(.*) (reached|timed out) after')
    overlays = lines(log, r'^NEO98 HOST seamless overlay=(\d) window=(\d+)x(\d+) screen=\S+ taskbar=\d \((.*)\)')
    canvases = lines(log, r'^NEO98 HOST canvas (\d+)x(\d+) window=(\d+)x(\d+)')
    pointers = lines(log, r'^NEO98 HOST seamless pointer=(\d+),(\d+) opaque=(\d) through=(\d)')
    check('fixture: every wait reached',
          len(waits) == sum(':wait=' in step or ':idle=' in step for step in first)
          and all(w[2] == 'reached' for w in waits))
    check('Nahtlos asks the guest for the desktop of the screen', f'NEO98 HOST seamless desktop {wide} asked' in log)
    check('the guest starts in the size of the screen and hears WM_DISPLAYCHANGE later',
          waits.count(('wait', f'|screen {wide}', 'reached')) == 3)
    check('the window lies over the screen', overlays[:1] == [('1', str(SCREEN[0]), str(SCREEN[1]), 'desktop')])
    shot, alpha = out / 'f1.ppm', out / 'f1.pgm'
    check('screenshot and mask written', shot.exists() and alpha.exists())
    if shot.exists() and alpha.exists():
        check('screenshot has the size of the screen', image(shot)[:2] == SCREEN)
        width, height, at = image(alpha)
        check('mask has the size of the screen', (width, height) == SCREEN)
        if (width, height) == SCREEN:
            # A is an ellipse in 100,80..400,280 with its centre at 250,180;
            # B is 250,180..450,330.
            check('mask: the middle of the elliptic window is opaque', at(180, 180) == 'ff')
            check('mask: the corner of its rectangle outside the region is not', at(102, 82) == '00')
            check('mask: the second window is opaque', at(440, 320) == 'ff')
            check('mask: its part beside the ellipse, inside the first rectangle, is opaque', at(395, 275) == 'ff')
            check('mask: the desktop is transparent', at(600, 500) == '00' and at(10, 10) == '00')
    check('pointer over the desktop: clicks go through', ('600', '500', '0', '1') in pointers)
    check('pointer over a window: clicks are the guest\'s', ('250', '150', '1', '0') in pointers)
    check('press on the desktop does not reach the guest', 'NEO98 HOST seamless press 600,500 goes through to the Mac' in log)
    check('press on a window reaches it in screen pixels', ('wait', '|A down 100,70', 'reached') in waits)
    check('capture: an outside click delivers the press and release at the same point',
          ('wait', '|A up 500,420', 'reached') in waits
          and lines(log, r'^OutputDebugString (A (?:down|up) 500,420)$') == ['A down 500,420', 'A up 500,420']
          and pointers.count(('600', '500', '1', '0')) == 1)
    check('Ctrl+Alt+Delete opens the system dialog and Escape closes it',
          ('wait', '#32770|Close Program', 'reached') in waits
          and 'modal dialog end result=0' in log and 'OutputDebugString CAD ' not in log)
    check('after the capture the desktop is the Mac\'s again', ('601', '500', '0', '1') in pointers)
    # One Mac window per guest window: the list the host keeps, front to back.
    lists = re.split(r'^NEO98 HOST seamless items ', log, flags=re.M)[1:]
    plans = [(part.split('\n', 1)[0], re.findall(
        r'^NEO98 HOST seamless item id=(\w+) frame=(\S+) rects=(\d+) listed=(\d) through=(\d) title=(.*)$',
        part.split('NEO98 HOST seamless choose')[0], re.M)) for part in lists]
    check('windows: two lists', len(plans) == 2 and all(head.startswith('2 mode=windows') for head, _ in plans))
    if len(plans) == 2:
        before, after = plans[0][1], plans[1][1]
        frames = {item[5]: item[1] for item in before}
        check('windows: each guest window with its rectangle and title, listed for Mission Control',
              frames.get('released') == '100,80,400,280' and frames.get('B') == '250,180,450,330'
              and all(item[3] == '1' and item[4] == '0' for item in before))
        check('windows: the elliptic window is many rectangles', any(item[5] == 'released' and int(item[2]) > 20 for item in before))
        check('windows: a window chosen on the Mac comes to the front in the guest',
              [item[5] for item in before] == ['released', 'B'] and [item[5] for item in after] == ['B', 'released'])
    check('program mode takes the overlay away', ('0', '640', '480', 'program mode') in overlays
          or any(o[0] == '0' and o[3] == 'program mode' for o in overlays))
    # 1100x700 leaves 672 below a title bar: 640x480 by 1.4.
    check('program mode: a framed window for 640x480, enlarged', ('640', '480', '896', '672') in canvases)
    check('program mode: no mask', re.search(r'seamless mask \S+program.pgm refused', log))
    check('program mode: the frame has the size of the mode, not the desktop\'s stretched onto it',
          (out / 'program.ppm').exists() and image(out / 'program.ppm')[:2] == (640, 480))
    check('back on the desktop the overlay returns', len([o for o in overlays if o[0] == '1']) >= 2
          and (out / 'f2.pgm').exists())
    check('taskbar item toggles', 'NEO98 HOST seamless taskbar=0' in log and 'NEO98 HOST seamless taskbar=1' in log)
    check('a size stored by the guest ends the seamless view',
          'NEO98 HOST seamless off (the guest chose a screen size)' in log and ('800', '600', '800', '600') in canvases)
    check('full screen shows the program in front and leads back to the seamless view',
          'NEO98 HOST seamless off (full screen chosen)' not in log
          and re.search(r'^NEO98 HOST full screen of a program hwnd=', log, re.M)
          and 'NEO98 HOST full screen of a program ends (menu)' in log)
    check('Ctrl+Cmd+S ends and starts it', 'NEO98 HOST seamless off (menu)' in log
          and log.count('NEO98 HOST seamless on') == 2
          and re.search(r'menu-item View > Seamless \[seamless\] ctrl\+cmd\+s \(enabled\)', log))
    stored = registry.read_bytes() if registry.exists() else b''
    check('the registry holds the guest\'s size and never the screen\'s',
          b'800,600' in stored and ('%d,%d' % SCREEN).encode() not in stored)
    check('no hang shown', 'NEO98 HOST hang begin' not in log)
    check('fixture run ended', code == 0)

    # 2. A restart keeps the view.
    second = [f'0:wait=|screen {wide}', '0:idle=300', f'0:mask={out / "restart.pgm"}', '0:windows=1']
    code, log = run(str(exe), drive, registry, second, 'host-2.log',
                    menu='2:seamless;1:seamless-windows;4:reboot;6:describe;1:quit')
    before, _, after = log.partition('NEO98 HOST reboot report=')
    check('restart: the host restarted', after)
    check('restart: seamless from the start, not asked again',
          re.search(r'^NEO98 HOST seamless overlay=1 window=%dx%d' % SCREEN, after, re.M)
          and 'NEO98 HOST seamless on' not in after and 'asked' not in after)
    check('restart: the guest starts in the size of the screen',
          re.search(r'^NEO98 HOST input wait=\|screen %s reached' % wide, after, re.M))
    check('restart: the menu shows it', re.search(r'Seamless \[seamless\] ctrl\+cmd\+s \(enabled\)', after))
    check('restart: one layer instead of a Mac window per guest window is kept as well',
          'NEO98 HOST seamless windows=0' in before and re.search(r'^NEO98 HOST seamless items 2 mode=layer', after, re.M)
          and re.search(r'One Mac window per guest window \[seamless-windows\]$', after, re.M))
    check('restart ended', code == 0)

    # 4. A Mac window per guest window: a press beside an open menu lands on
    # the Mac, and the guest loses the activation as by a press on a window of
    # another program.
    shutil.rmtree(out / 'drive-a', ignore_errors=True)
    fourth = [f'0:wait=|screen {wide}', '0:idle=300',
              '0:key=4d', '0:wait=|menu open', '0:idle=300', '0:windows=1', '0:pointer=600,500',
              '0:press=600,500', '0:release=600,500', '0:wait=|menu closed 0,2', '0:idle=200', '0:windows=1',
              '0:activation=1', '0:idle=500', '0:key=53', '0:wait=|status 2,2',
              '0:key=4c', '0:wait=|list open', '0:idle=300', '0:windows=1',
              '0:activation=0', '0:wait=|list closed', '0:idle=200', '0:windows=1',
              '0:activation=1', '0:idle=300', '0:choose=list closed', '0:idle=300', '0:key=53', '0:wait=|status 3,3',
              '0:activation=0', '0:idle=300', '0:press=200,150', '0:wait=|A down 100,70', '0:release=200,150',
              '0:idle=200', '0:key=53', '0:wait=|status 4,4',
              # A window that was shown and has not painted.
              '0:key=54', '0:wait=|B shown', f'0:mask={out / "fresh1.pgm"}', '0:idle=1300',
              f'0:mask={out / "fresh2.pgm"}', '0:wait=|B painted', '0:idle=300', f'0:mask={out / "fresh3.pgm"}',
              # A minimized window comes back by the Window menu.
              '0:key=4e', '0:wait=|B minimized', '0:idle=700', '0:menu=describe', '0:menu=guest-window=B',
              '0:wait=|B restored', '0:idle=700', '0:menu=describe', '0:menu=quit']
    code, log = run(str(exe), out / 'drive-a', out / 'registry-a', fourth, 'host-4.log', extra=['--seamless'])
    waits = lines(log, r'^NEO98 HOST input (\S+)=(.*) (reached|timed out) after')
    pointers = lines(log, r'^NEO98 HOST seamless pointer=(\d+),(\d+) opaque=(\d) through=(\d)')
    counts = [int(n) for n in lines(log, r'^NEO98 HOST seamless items (\d+) mode=windows')]
    check('activation: every wait reached', waits and all(w[2] == 'reached' for w in waits))
    check('activation: the open menu is a Mac window of its own', counts[:1] == [3])
    check('activation: beside the open menu the point is the Mac\'s', ('600', '500', '0', '1') in pointers)
    check('activation: a press beside the open menu goes to the Mac and takes the activation',
          'NEO98 HOST seamless press 600,500 goes through to the Mac' in log
          and 'NEO98 HOST seamless activation=0 (a press on the Mac)' in log)
    check('activation: the menu closes without a choice, its window is deactivated once',
          ('wait', '|menu closed 0,2', 'reached') in waits and counts[1:2] == [2])
    check('activation: back, the window is active again and has the keyboard', ('wait', '|status 2,2', 'reached') in waits)
    check('activation: the dropped list of a combo box is a Mac window and closes with the activation',
          ('wait', '|list closed', 'reached') in waits and counts[2:4] == [3, 2])
    check('activation: a press on a guest window brings its window back', ('wait', '|status 4,4', 'reached') in waits)
    fresh = [image(out / name)[2] if (out / name).exists() else None for name in ('fresh1.pgm', 'fresh2.pgm', 'fresh3.pgm')]
    check('first paint: three masks written', all(fresh))
    if all(fresh):
        check('first paint: a window that was shown and has not painted is not opaque',
              fresh[0](440, 320) == '00' and fresh[0](180, 180) == 'ff')
        check('first paint: after a second it is opaque although it has not painted', fresh[1](440, 320) == 'ff')
        check('first paint: painted, it stays opaque', fresh[2](440, 320) == 'ff')
    menus = log.split('NEO98 HOST menu-script describe ok')
    check('minimized: the Window menu lists the minimized window and brings it back',
          len(menus) >= 3 and re.search(r'menu-item Window > B \[guest-window\]', menus[0])
          and 'NEO98 HOST menu-script guest-window=B ok' in log and ('wait', '|B restored', 'reached') in waits
          and not re.search(r'menu-item Window > B \[guest-window\]', menus[1]))
    check('activation: no hang shown', 'NEO98 HOST hang begin' not in log)
    check('activation run ended', code == 0)
    shutil.rmtree(out / 'drive-a', ignore_errors=True)

    # 7. The pointer is the Mac's: the cursor the guest sets chooses
    # the cursor of the Mac, and no pointer stands in the pictures of the
    # guest's windows.
    shutil.rmtree(out / 'drive-p', ignore_errors=True)
    seventh = [f'0:wait=|screen {wide}', '0:idle=300', '0:press=200,150', '0:wait=|A down 100,70', '0:release=200,150',
               '0:idle=300', f'0:mask={out / "pointer.ppm"}', '0:windows=1']
    for n in range(1, 17):
        seventh += ['0:key=51', f'0:wait=|cursor {n},0', '0:idle=150']
    seventh += ['0:menu=quit']
    code, log = run(str(exe), out / 'drive-p', out / 'registry-p', seventh, 'host-7.log', extra=['--seamless'])
    waits = lines(log, r'^NEO98 HOST input (\S+)=(.*) (reached|timed out) after')
    names = lines(log, r'^NEO98 HOST seamless cursor=(.*)$')
    check('pointer: every wait reached', waits and all(w[2] == 'reached' for w in waits))
    wanted = ['arrow', 'ibeam', 'wait', 'cross', 'uparrow', 'size-nwse', 'size-nesw', 'size-we', 'size-ns', 'size-all',
              'no', 'hand', 'appstarting', 'help', 'custom 32x32 hot=3,5', 'hidden']
    after = names[-len(wanted):]
    # The first key sets the arrow, which the pointer already is.
    check('pointer: each cursor of USER chooses its Mac cursor, a program\'s own cursor its picture, none hides it',
          after[1:] == wanted[1:] and 'arrow' in names)
    if (out / 'pointer.ppm').exists():
        _, _, at = image(out / 'pointer.ppm')
        check('pointer: no drawn pointer in the picture of the window under it',
              all(at(x, y) == 'ff0000' for x in range(198, 214) for y in range(148, 170)))
    else:
        check('pointer: picture written', False)
    check('pointer: no Mac window for a drawn pointer', 'item id=fffffff2' not in log)
    check('pointer run ended', code == 0)
    shutil.rmtree(out / 'drive-p', ignore_errors=True)

    # 5. A session of 16-bit programs only takes the size of the screen in
    # the run as well.
    shutil.rmtree(out / 'drive-w', ignore_errors=True)
    fifth = ['0:wait=WFS_Frame', '0:idle=1000', '0:menu=seamless', '0:idle=2000', '0:windows=1',
             '0:menu=seamless', '0:idle=1500', '0:menu=quit']
    code, log = run('C:\\WINDOWS\\WINFILE.EXE', out / 'drive-w', out / 'registry-w', fifth, 'host-5.log')
    check('16 bit: the File Manager is up', re.search(r'^NEO98 HOST input wait=WFS_Frame reached', log, re.M))
    check('16 bit: the desktop takes the size of the screen in the run',
          re.search(r'^NEO98 HOST seamless overlay=1 window=%dx%d .*\(desktop\)' % SCREEN, log, re.M))
    check('16 bit: and gives it back', re.search(r'^NEO98 HOST seamless off \(menu\)', log, re.M)
          and len(re.findall(r'^NEO98 HOST canvas 800x600', log, re.M)) >= 2)
    check('16 bit run ended', code == 0)
    shutil.rmtree(out / 'drive-w', ignore_errors=True)

    # 6. Another process beside the fixture: a WM_WININICHANGE with its
    # section name reaches CALC as a string of its own memory, and
    # the pixels the Mac windows get for a dialog-like window over another.
    shutil.rmtree(out / 'drive-b', ignore_errors=True)
    sixth = [f'0:wait=|screen {wide}', '0:idle=300', '0:key=58', '0:wait=SciCalc|', '0:idle=800',
             '0:choose=calc started', '0:idle=500', '0:key=57', '0:wait=|broadcast done', '0:idle=500',
             '0:key=47', '0:wait=|C shown', '0:idle=500', f'0:mask={out / "items.ppm"}', '0:wait=SciCalc|',
             '0:menu=seamless', '0:idle=1500', '0:wait=SciCalc|', '0:menu=seamless', '0:idle=1500',
             # Close the fault dialog so CALC actually exits and its shared
             # brushes are released before testing the new window.
             # SendMessage may return only after the target's fault box closes.
             '0:key=4b', '0:wait=#32770|Calc', '0:idle=200', '0:button=Close',
             '0:wait=|calc failed', '0:idle=500', '0:choose=calc failed', '0:idle=200',
             '0:key=47', '0:wait=|C shown', '0:idle=800',
             f'0:mask={out / "items2.ppm"}', '0:menu=quit']
    code, log = run(str(exe), out / 'drive-b', out / 'registry-b', sixth, 'host-6.log', extra=['--seamless'])
    waits = lines(log, r'^NEO98 HOST input (\S+)=(.*) (reached|timed out) after')
    check('broadcast: every wait reached, CALC is still there', waits and all(w[2] == 'reached' for w in waits)
          and waits.count(('wait', 'SciCalc|', 'reached')) == 3)
    check('broadcast: no guest read outside its memory before CALC is made to fail',
          'violates page protection' not in log.split('NEO98 HOST input wait=|broadcast done')[0] and 'NEO98 HOST hang begin' not in log
          and 'violates page protection at 00000001' in log)
    check('broadcast: Close dismisses the failed CALC process before the new window',
          'FAULT BOX closed' in log and log.index('FAULT BOX closed') < log.rfind('wait=|C shown'))
    if (out / 'items2.ppm').exists():
        _, _, at = image(out / 'items2.ppm')
        check('after a failed program a new window still erases its surface with COLOR_BTNFACE',
              at(520, 400) == 'c0c0c0' and at(650, 360) == '000080')
    else:
        check('after a failed program: picture written', False)
    if (out / 'items.ppm').exists():
        _, _, at = image(out / 'items.ppm')
        check('pixels of the Mac windows: the dialog surface is COLOR_BTNFACE, its caption blue, the window behind red',
              at(520, 150) == 'c0c0c0' and at(650, 110) == '000080' and at(180, 180) == 'ff0000')
        check('pixels of the Mac windows: nothing where the desktop is', at(900, 600) == 'ff00ff')
    else:
        check('pixels of the Mac windows: picture written', False)
    check('broadcast run ended', code == 0)
    shutil.rmtree(out / 'drive-b', ignore_errors=True)

    # 3. Explorer: the shell's desktop is transparent, the taskbar is not.
    if '--no-explorer' not in sys.argv:
        e = out / 'drive-e'
        # Keep the files and journal of the completed first logon together,
        # as test_multi_app_host does. An empty journal runs Active Setup
        # again and can exhaust the taskbar wait before the UI test starts.
        import first_logon
        logged_on, journal = first_logon.session()
        subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(e)], check=True)
        # -f: a read-only file of the first copy is replaced, not refused.
        subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto', '-f']), '-R', str(logged_on) + '/.', str(e)], check=True)
        shutil.copyfile(journal, out / 'registry-e')
        third = ['0:wait=Shell_TrayWnd', '0:idle=2000', f'0:shot={out / "e1.ppm"}', f'0:mask={out / "e1.pgm"}',
                 f'0:mask={out / "e1-items.ppm"}',
                 # The tooltip of the Start button, and the Mac pointer leaving the taskbar.
                 '0:move=30,690', '0:idle=2000', '0:move=31,690', '0:idle=3000', '0:windows=1',
                 '0:leave=30,600', '0:idle=3000', '0:windows=1',
                 '0:pointer=1000,100', '0:pointer=30,690',
                 '0:press=30,690', '0:release=30,690', '0:idle=800', f'0:shot={out / "e2.ppm"}',
                 f'0:mask={out / "e2.pgm"}', '0:windows=1', '0:pointer=1000,101',
                 '0:press=1000,100', '0:release=1000,100', '0:idle=800', f'0:mask={out / "e3.pgm"}',
                 '0:menu=seamless-taskbar', '0:idle=300', f'0:mask={out / "e4.pgm"}',
                 '0:menu=seamless-icons', '0:idle=300', f'0:mask={out / "e5.pgm"}', '0:windows=1',
                 '0:menu=seamless-icons', '0:menu=seamless-taskbar', '0:activation=1', '0:idle=500',
                 # Without the taskbar the Start menu opens from the menu bar of the Mac.
                 '0:windows=1', '0:menu=seamless-taskbar', '0:idle=1500', '0:windows=1', '0:menu=start-menu', '0:idle=1500',
                 f'0:mask={out / "e6.pgm"}', '0:menu=describe', '0:menu=quit']
        code, log = run('C:\\WINDOWS\\EXPLORER.EXE', e, out / 'registry-e', third, 'host-3.log',
                        extra=['--seamless', '--seamless-taskbar'],
                        timeout=900)
        waits = lines(log, r'^NEO98 HOST input (\S+)=(.*) (reached|timed out) after')
        pointers = lines(log, r'^NEO98 HOST seamless pointer=(\d+),(\d+) opaque=(\d) through=(\d)')
        check('Explorer: every wait reached', waits and all(w[2] == 'reached' for w in waits))
        check('Explorer: seamless from the start', re.search(r'^NEO98 HOST seamless overlay=1 window=%dx%d' % SCREEN, log, re.M))
        masks = {}
        for name in ('e1', 'e2', 'e3', 'e4', 'e5'):
            path = out / (name + '.pgm')
            check(f'Explorer: {name}.pgm written', path.exists())
            masks[name] = image(path) if path.exists() else None
        if all(masks.values()):
            e1, e2, e3, e4, e5 = (masks[n][2] for n in ('e1', 'e2', 'e3', 'e4', 'e5'))
            check('Explorer: with "Desktop-Symbole zeigen" the icons are opaque and the desktop between them is not',
                  e5(38, 20) == 'ff' and e5(38, 100) == 'ff' and e5(1000, 100) == '00' and e5(900, 600) == '00'
                  and 'NEO98 HOST seamless icons=1' in log)
            check('Explorer: the taskbar is opaque', e1(30, 690) == 'ff' and e1(600, 690) == 'ff' and e1(1090, 690) == 'ff')
            check('Explorer: the desktop is transparent', e1(1000, 100) == '00' and e1(900, 600) == '00')
            check('Explorer: the icons of the desktop are transparent', e1(38, 20) == '00' and e1(38, 100) == '00')
            check('Explorer: the Start menu is opaque while open', e1(90, 500) == '00' and e2(90, 500) == 'ff')
            check('Explorer: a press beside the open menu closes it', e3(90, 500) == '00')
            check('Explorer: the taskbar goes with its menu item', e4(600, 690) == '00' and e3(600, 690) == 'ff')
        if (out / 'e1-items.ppm').exists():
            _, _, at = image(out / 'e1-items.ppm')
            check('Explorer: the Mac window of the taskbar gets its grey, also in the empty part',
                  at(600, 690) == 'c0c0c0' and at(1000, 100) == 'ff00ff')
        if (out / 'e6.pgm').exists():
            e6 = image(out / 'e6.pgm')[2]
            check('Explorer: without the taskbar the Start menu opens from the Window menu',
                  e6(90, 500) == 'ff' and e6(600, 690) == '00' and 'NEO98 HOST start menu asked' in log)
        else:
            check('Explorer: e6.pgm written', False)
        works = re.findall(r'^NEO98 HOST seamless work=(\S+)$', log, re.M)
        check('Explorer: with the taskbar the work area ends above it, without it is the whole screen',
              len(works) >= 2 and works[-2] == '0,0,%d,672' % SCREEN[0] and works[-1] == '0,0,%d,%d' % SCREEN)
        check('Explorer: the Dock menu has the Start menu', re.search(r'menu-item Dock > Start menu \[start-menu\]', log))
        check('Explorer: desktop is the Mac\'s, the Start button the guest\'s',
              ('1000', '100', '0', '1') in pointers and ('30', '690', '1', '0') in pointers)
        check('Explorer: with the menu open the desktop stays the Mac\'s', ('1000', '101', '0', '1') in pointers)
        check('Explorer: the press beside the menu takes the activation from the guest',
              'NEO98 HOST seamless activation=0 (a press on the Mac)' in log)
        listed = re.findall(r'^NEO98 HOST seamless item id=\w+ frame=(\S+) rects=\d+ listed=(\d) through=\d title=(.*)$', log, re.M)
        check('Explorer: the taskbar and the Start menu are no windows of their own in Mission Control',
              len(listed) >= 2 and any(frame.startswith('0,') and frame.endswith(',%d,%d' % SCREEN) for frame, _, _ in listed)
              and all(flag == '0' for frame, flag, title in listed if not title or frame.endswith(',%d,%d' % SCREEN)))
        def tips(part):
            found = []
            for frame, flag, title in re.findall(
                    r'^NEO98 HOST seamless item id=\w+ frame=(\S+) rects=\d+ listed=(\d) through=0 title=(.*)$', part, re.M):
                left, top, right, bottom = (int(v) for v in frame.split(','))
                if not title and right - left < 300 and bottom - top < 30:
                    found.append(frame)
            return found
        parts = re.split(r'^NEO98 HOST seamless items ', log, flags=re.M)
        # The tooltip does not show in every run without a screen; when it
        # showed, it must be gone after the pointer left.
        print('note Explorer: the tooltip of the Start button %s' % ('showed' if len(parts) >= 3 and tips(parts[1]) else 'did not show'))
        check('Explorer: no tooltip of the Start button stays when the Mac pointer has left the taskbar',
              len(parts) >= 3 and not tips(parts[2]) and 'NEO98 HOST seamless leave 30,600' in log)
        check('Explorer: press on the Start button reaches the guest',
              'NEO98 HOST seamless press 30,690 reaches the guest' in log)
        check('Explorer run ended', code == 0)

    report = {'checks': checks, 'screen': SCREEN}
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    failed = [name for name, okay in checks.items() if not okay]
    if failed:
        print(f'SEAMLESS HOST FAIL: {len(failed)} of {len(checks)} checks; see {out}')
        return 1
    print(f'SEAMLESS HOST PASS: {len(checks)} checks; {out / "results.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
