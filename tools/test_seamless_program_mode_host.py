#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The seamless view comes back after a DirectDraw program in full screen.

tools/host_ddraw_mode_fixture.c is a program as a game is one: a black popup
window over the whole screen, DirectDraw in exclusive mode, a display mode of
640x480. Explorer is the shell, the host runs headless (SDL's dummy driver,
NEO98_HOST_SCREEN=1100x700) with --seamless, and the fixture starts three
times the way a double click starts it:

1. Exclusive mode and 640x480, RestoreDisplayMode, SetCooperativeLevel
   (NORMAL), the window made small, the end of the program. The overlay goes
   with the program mode and comes back; while the program is in exclusive
   mode at the size of the screen the window covers the screen without a
   frame instead of reaching beyond it; afterwards only the small window is
   opaque, and nothing of the program after its end.
2. The close button of the Mac window closes the program while it is in
   exclusive mode at 640x480 (SC_CLOSE); NEO98 goes on. In that mode the
   window has a title bar with the program's title, stands in the middle and
   shows the mode enlarged.
3. The program crashes there.
After each the desktop has the size of the screen again, the overlay is
back, and the mask is transparent outside the windows that are left.

    build/analysis-venv/bin/python tools/test_seamless_program_mode_host.py [--no-build]

Results in build/seamless-program-mode-host/results.json.
"""
from __future__ import annotations

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

IMPORTS = {'KERNEL32': {'ExitProcess': 4, 'GetModuleHandleA': 4},
           'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16, 'GetMessageA': 16,
                      'DispatchMessageA': 4, 'SetWindowTextA': 8, 'GetSystemMetrics': 4, 'MoveWindow': 24,
                      'SetTimer': 16, 'GetForegroundWindow': 0, 'SetForegroundWindow': 4},
           'GDI32': {'GetStockObject': 4}, 'DDRAW': {'DirectDrawCreate': 12}}
SCREEN = (1100, 700)


def fixture(out, source='host_ddraw_mode_fixture.c', name='HDDMODE.EXE', imports=None):
    environment = neo98.tool_environment()
    dlltool = shutil.which('llvm-dlltool', path=environment['PATH'])
    assert dlltool, 'llvm-dlltool not found'
    libraries = []
    for dll, names in (imports or IMPORTS).items():
        definition, library = out / (dll + '.def'), out / (dll + '.lib')
        definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' +
                              ''.join(f'{n}@{b}\n' for n, b in names.items()))
        subprocess.run([dlltool, '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)],
                       env=environment, check=True)
        libraries.append(str(library))
    obj, exe = out / (name + '.obj'), out / name
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-c', str(ROOT / 'tools' / source), '-o', str(obj)],
                   env=environment, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0',
                    '/nodefaultlib', '/machine:x86', '/out:' + str(exe), str(obj), *libraries],
                   env=environment, check=True)
    return exe


def alpha(path):
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    width, height = (int(v) for v in size.split())
    return width, height, lambda x, y: body[y * width + x]


def main():
    out = neo98.output_directory('seamless-program-mode-host')
    for old in list(out.glob('*.pgm')) + list(out.glob('*.log')):
        old.unlink()
    programs = out / 'programs'
    programs.mkdir(exist_ok=True)
    exe = fixture(programs)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    drive = out / 'drive-c'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
    masks = {name: out / f'{name}.pgm' for name in ('small', 'ended', 'exited', 'crashed')}
    steps = ['0:wait=Shell_TrayWnd', '0:idle=3000',
             # 1. The whole way and back.
             f'0:open={exe}', '0:wait=|ready', '1:key=45', '1:wait=|exclusive', '2:key=52', '2:wait=|restored',
             '3:windows=1', '3:key=4e', '3:wait=|normal', '4:key=53', '4:wait=|small', '5:windows=1',
             f'5:mask={masks["small"]}', '5:key=58', '7:windows=1', f'7:mask={masks["ended"]}',
             # 2. The end of the program in exclusive mode.
             f'8:open={exe}', '8:wait=|ready', '9:key=45', '9:wait=|exclusive', '10:closebox=1', '13:windows=1',
             f'13:mask={masks["exited"]}',
             # 3. A crash there.
             f'14:open={exe}', '14:wait=|ready', '15:key=45', '15:wait=|exclusive', '16:key=4b', '20:windows=1',
             f'20:mask={masks["crashed"]}', '21:menu=quit']
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
               '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--quiet', '--drive-c', str(drive), '--no-registry-file',
               '--drive-readonly', f'P={programs}', '--crash-report-dir', str(out / 'reports'), '--seamless']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_HOST_SCREEN='%dx%d' % SCREEN, NEO98_HOST_DEADLINE='540')
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        code = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                              stdin=subprocess.DEVNULL, timeout=600).returncode
    shutil.rmtree(drive, ignore_errors=True)
    shutil.rmtree(out / 'reports', ignore_errors=True)
    text = log_path.read_text(errors='replace')
    lines = [line for line in text.splitlines() if line.startswith('NEO98 HOST')]
    waits = re.findall(r'^NEO98 HOST input wait=(\S.*) (reached|timed out) after', text, re.M)
    overlays = re.findall(r'^NEO98 HOST seamless overlay=(\d) .*\((.*)\)', text, re.M)
    lists, current = [], None
    for line in lines:
        if line.startswith('NEO98 HOST seamless items '):
            current = {'mode': re.search(r'mode=(\w+)', line).group(1), 'items': []}
            lists.append(current)
        elif line.startswith('NEO98 HOST seamless item ') and current is not None:
            match = re.search(r'frame=(-?\d+),(-?\d+),(-?\d+),(-?\d+) .* title=(.*)$', line)
            current['items'].append((tuple(int(v) for v in match.groups()[:4]), match.group(5)))
    fixture_titles = {'ready', 'exclusive', 'restored', 'normal', 'small'}

    def clear(name):
        """The mask is transparent in the corners and holds nothing as large as the screen."""
        if not masks[name].exists():
            return False
        width, height, at = alpha(masks[name])
        corners = [(5, 5), (width - 6, 5), (5, height - 6), (width - 6, height - 6), (width - 6, height // 2)]
        return (width, height) == SCREEN and all(at(x, y) == 0 for x, y in corners)

    restored = next((i for i, line in enumerate(lines) if 'wait=|restored reached' in line), None)
    covers = [i for i, line in enumerate(lines) if 'window covers the screen' in line]
    checks = {
        'host ended': code == 0,
        'every wait reached': bool(waits) and all(w[1] == 'reached' for w in waits),
        'overlay off and on three times': [o[0] for o in overlays] == ['1', '0', '1', '0', '1', '0', '1'],
        'the program modes are named': all(o[1] == 'program mode' for o in overlays if o[0] == '0'),
        'exclusive mode at the size of the screen covers the screen without a frame':
            restored is not None and any(restored - 12 < i < restored + 12 for i in covers),
        # 1100x700 leaves 672 below a title bar: 640x480 by 1.4.
        'in the mode of 640x480 a framed window, enlarged and resizable':
            'NEO98 HOST window framed 896x672 for 640x480 resizable=1' in text,
        'it carries the title of the program': 'NEO98 HOST title exclusive' in text,
        'its close button closes the program, not NEO98':
            re.search(r'^NEO98 HOST program window closed: SC_CLOSE to [0-9a-f]+$', text, re.M) is not None
            and 'wait=|ready reached' in text.split('NEO98 HOST program window closed')[-1],
        'in exclusive mode at the size of the screen no overlay': len(lists) > 0 and lists[0]['mode'] == 'off',
        'the small window is the only part of the program on the screen':
            len(lists) > 1 and lists[1]['mode'] == 'windows'
            and [frame for frame, title in lists[1]['items'] if title == 'small'] == [(100, 100, 400, 300)],
        'mask with the small window': clear('small') and alpha(masks['small'])[2](250, 200) == 255,
        'nothing of the program after its end': len(lists) > 2 and lists[2]['mode'] == 'windows'
            and not any(title in fixture_titles for _, title in lists[2]['items']) and clear('ended'),
        'after the end in exclusive mode': len(lists) > 3 and lists[3]['mode'] == 'windows'
            and not any(title in fixture_titles for _, title in lists[3]['items']) and clear('exited'),
        'after a crash in exclusive mode': len(lists) > 4 and lists[4]['mode'] == 'windows'
            and not any(title in fixture_titles for _, title in lists[4]['items']) and clear('crashed'),
    }
    (out / 'results.json').write_text(json.dumps(checks, indent=1))
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'SEAMLESS PROGRAM MODE FAIL; see {log_path}')
        return 1
    print('SEAMLESS PROGRAM MODE PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
