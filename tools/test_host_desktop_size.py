#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The host window follows the size of the desktop, headless.

The host runs under SDL's dummy video driver with the fixture
tools/host_desktop_size_fixture.c, a fresh C: from setup's state and a
registry file of its own. NEO98_HOST_SCREEN stands in for the usable area of
the Mac screen. Two starts:

1. The desktop is 800x600; the fixture stores 1024x768 with
   CDS_UPDATEREGISTRY. The canvas becomes 1024x768; on a 1000x700 screen the
   window is fitted to 933x700 with 4:3 kept. The screenshot is 1024x768 and
   shows the red block beyond the old 800x600 edge 1:1. A mouse move in window
   points reaches the guest in canvas pixels. A program mode of 640x480
   (CDS_FULLSCREEN) keeps the canvas, the guest goes idle in it, and
   a click at the canvas centre reaches the program at 320,240. Back on the desktop, the fixture stores 640x480
   with CDS_UPDATEREGISTRY: canvas and window become 640x480.
2. The same C: and registry again (a restart): the host starts with a
   640x480 canvas; Skalierung 2x doubles the window on a 1440x1000 screen,
   Full screen on and off brings the window back to the canvas size.

    build/analysis-venv/bin/python tools/test_host_desktop_size.py [--no-build]

Results in build/host-desktop-size/results.json.
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
           'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16,
                      'GetMessageA': 16, 'TranslateMessage': 4, 'DispatchMessageA': 4,
                      'BeginPaint': 8, 'EndPaint': 8, 'FillRect': 12, 'SetWindowTextA': 8,
                      'ChangeDisplaySettingsA': 8, 'GetSystemMetrics': 4, 'MoveWindow': 24,
                      'InvalidateRect': 12},
           'GDI32': {'CreateSolidBrush': 4}}


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
    obj, exe = out / 'fixture.obj', out / 'HDSIZE.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-c', str(ROOT / 'tools/host_desktop_size_fixture.c'),
                    '-o', str(obj)], env=environment, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0',
                    '/nodefaultlib', '/machine:x86', '/out:' + str(exe), str(obj), *libraries],
                   env=environment, check=True)
    return exe


def image(path):
    """Width, height and a pixel reader of a binary PPM (P6)."""
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    width, height = (int(v) for v in size.split())

    def at(x, y):
        i = (y * width + x) * 3
        return body[i:i + 3].hex()
    return width, height, at


def main():
    out = neo98.output_directory('host-desktop-size')
    for name in ('drive-c', 'reports'):
        shutil.rmtree(out / name, ignore_errors=True)
    for name in ('registry', 'a.ppm', 'b.ppm', 'c.ppm'):
        (out / name).unlink(missing_ok=True)
    drive_c = out / 'drive-c'
    subprocess.run(['cp', '-c', '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    exe = fixture(out)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', str(exe), '--quiet', '--drive-c', str(drive_c),
               '--registry', str(out / 'registry'), '--crash-report-dir', str(out / 'reports')]
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)

    def run(steps, screen, log_name):
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_SCREEN=screen)
        env.pop('NEO98_HOST_ATTACHED', None)
        env.pop('NEO98_HOST_MENU', None)
        log_path = out / log_name
        with log_path.open('w') as log:
            result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                    stdin=subprocess.DEVNULL, timeout=600)
        return result.returncode, log_path.read_text(errors='replace')

    # 1. 1024x768 on a 1000x700 screen, a 640x480 program mode, then a
    # 640x480 desktop.
    first = ['0:wait=|s1', '0:idle=500', '0:wait=900,700,ff0000', f'0:shot={out / "a.ppm"}',
             '0:motion=466,350', '0:wait=|s1 move 511,384',
             '0:click=900,700', '0:wait=|s2 640x480', '0:idle=500',
             '0:click=512,384', '0:wait=|s3 at 320,240', '0:idle=500', '0:wait=320,240,0000ff',
             f'0:shot={out / "b.ppm"}', '0:motion=100,50', '0:wait=|s3 move 100,50', '0:menu=quit']
    code, log = run(first, '1000x700', 'host-1.log')
    canvases = re.findall(r'^NEO98 HOST canvas (\d+)x(\d+) window=(\d+)x(\d+)', log, re.M)
    waits = re.findall(r'^NEO98 HOST input (\S+)=(.*) (reached|timed out) after', log, re.M)
    check('first start: every wait reached', waits and all(w[2] == 'reached' for w in waits))
    check('first start: canvas 800x600 at the start', canvases[:1] == [('800', '600', '800', '600')])
    check('1024x768 desktop: canvas 1024x768, window fitted to 933x700', ('1024', '768', '933', '700') in canvases)
    check('640x480 desktop: canvas and window 640x480', canvases[-1:] == [('640', '480', '640', '480')])
    check('program mode 640x480 keeps the canvas', not any(c[:2] == ('640', '480') for c in canvases[:-1]))
    check('mouse move in window points reaches the guest in canvas pixels', ('wait', '|s1 move 511,384', 'reached') in waits)
    check('click at the canvas centre reaches the program mode at 320,240', ('wait', '|s3 at 320,240', 'reached') in waits)
    check('mouse move on the 640x480 desktop', ('wait', '|s3 move 100,50', 'reached') in waits)
    for name, size in (('a.ppm', (1024, 768)), ('b.ppm', (640, 480))):
        path = out / name
        check(f'{name} written', path.exists())
        if path.exists():
            width, height, at = image(path)
            check(f'{name} is {size[0]}x{size[1]}', (width, height) == size)
            if name == 'a.ppm' and (width, height) == size:
                # The red block 850..1000 x 650..750 at its own pixels, blue beside it.
                check('a.ppm shows the block 1:1', at(850, 650) == 'ff0000' and at(999, 749) == 'ff0000'
                      and at(849, 650) == '0000ff' and at(1000, 750) == '0000ff')
    # a thread waiting in GetMessage during a program mode is not
    # runnable because of the shell's composition, which does not run then.
    check('program mode: the guest goes idle, no hang shown', 'NEO98 HOST hang begin' not in log)
    check('first start ended', code == 0)

    # 2. A restart into the stored 640x480.
    shot = out / 'c.ppm'
    second = ['0:wait=|restart 640x480', '0:idle=500', f'0:shot={shot}', '0:menu=scale-2', '1:menu=fullscreen',
              '1:menu=fullscreen', '1:menu=scale-1', '1:menu=quit']
    code, log = run(second, '1440x1000', 'host-2.log')
    canvases = re.findall(r'^NEO98 HOST canvas (\d+)x(\d+) window=(\d+)x(\d+)', log, re.M)
    views = re.findall(r'^NEO98 HOST view scale=(\d) fullscreen=(\d) window=(\d+)x(\d+)', log, re.M)
    check('restart: canvas 640x480 from the start', canvases == [('640', '480', '640', '480')])
    check('restart: the fixture found 640x480', re.search(r'^NEO98 HOST input wait=\|restart 640x480 reached', log, re.M))
    check('restart: screenshot 640x480', shot.exists() and image(shot)[:2] == (640, 480))
    check('scale 2 doubles the 640x480 window', ('2', '0', '1280', '960') in views)
    check('full screen on and off, back to the canvas size', any(v[1] == '1' for v in views)
          and views[-1] == ('1', '0', '640', '480'))
    check('restart ended', code == 0)
    report = {'checks': checks, 'first': first, 'second': second}
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    failed = [name for name, okay in checks.items() if not okay]
    if failed:
        print(f'HOST DESKTOP SIZE FAIL: {len(failed)} of {len(checks)} checks; see {out}')
        return 1
    print(f'HOST DESKTOP SIZE PASS: {len(checks)} checks; {out / "results.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
