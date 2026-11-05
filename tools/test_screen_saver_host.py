#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Screen saver and display power in the host.

A copy of the C: of setup names "Flying Windows" in SYSTEM.INI [boot]
SCRNSAVE.EXE (FLYING~1.SCR), as DESK.CPL writes it. The guest
tools/screen_saver_fixture.c sets the screen saver to three seconds and
power off to eight seconds (SPI_SETSCREENSAVETIMEOUT, SPI_SETSCREENSAVEACTIVE,
SPI_SETPOWEROFFTIMEOUT, SPI_SETPOWEROFFACTIVE) and waits without input. The
host runs under SDL's dummy video driver with NEO98_HOST_INPUT: screenshots at
six and twelve seconds, a mouse movement at thirteen, a screenshot at fifteen.
Checks:

- USER posts SC_SCREENSAVE and DefWindowProc starts FLYING~1.SCR /s,
- the saver's window covers the screen at six seconds (not the teal desktop),
- the display is black at twelve seconds (power off),
- after the movement the picture is back.

    build/analysis-venv/bin/python tools/test_screen_saver_host.py [--no-build]

Results in build/screen-saver-host/results.json.
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


def pixels(path):
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    return body


def main():
    out = neo98.output_directory('screen-saver-host')
    env = neo98.tool_environment()
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)

    # The guest program.
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-c', str(ROOT / 'tools/screen_saver_fixture.c'), '-o',
                    str(out / 'fixture.obj')], env=env, check=True)
    libs = []
    for dll, entries in {'KERNEL32': {'ExitProcess': 4, 'GetTickCount': 0, 'Sleep': 4},
                         'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16,
                                    'ShowWindow': 8, 'SystemParametersInfoA': 16, 'PeekMessageA': 20,
                                    'DispatchMessageA': 4, 'TranslateMessage': 4}}.items():
        definition, library = out / (dll + '.def'), out / (dll + '.lib')
        definition.write_text('LIBRARY %s.DLL\nEXPORTS\n' % dll +
                              ''.join('%s@%d\n' % (n, b) for n, b in entries.items()))
        subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)],
                       env=env, check=True)
        libs.append(str(library))
    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', '-c', '-R', str((ROOT / 'build/win98-c').resolve()), str(drive_c)], check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0', '/nodefaultlib',
                    '/machine:x86', '/out:' + str(drive_c / 'SAVERTST.EXE'), str(out / 'fixture.obj'), *libs],
                   env=env, check=True)
    ini = drive_c / 'WINDOWS/SYSTEM.INI'
    text = ini.read_bytes().decode('cp1252')
    text = text.replace('[boot]\r\n', '[boot]\r\nSCRNSAVE.EXE=C:\\WINDOWS\\SYSTEM\\FLYING~1.SCR\r\n', 1)
    ini.write_bytes(text.encode('cp1252'))
    shots = [out / 'saver.ppm', out / 'off.ppm', out / 'back.ppm']
    for shot in shots:
        shot.unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    steps = [f'6:shot={shots[0]}', f'12:shot={shots[1]}', '13:move=400,300', '13.3:move=420,310',
             f'15:shot={shots[2]}']
    run_env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_MENU='17:quit')
    run_env.pop('NEO98_HOST_ATTACHED', None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'C:\\SAVERTST.EXE', '--quiet',
               '--drive-c', str(drive_c), '--no-registry-file',
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=run_env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=300)
    log_text = log_path.read_text(errors='replace')
    check('screen saver started', re.search(r'CreateProcess .*FLYING~1\.SCR /s', log_text, re.I))
    check('saver window covers the screen',
          re.search(r'CreateWindow class=WindowsScreenSaverClass style=\w+ parent=0 x=0 y=0 w=800 h=600', log_text))
    check('screenshots written', all(s.exists() for s in shots))
    if all(s.exists() for s in shots):
        saver, off, back = (pixels(s) for s in shots)
        teal = bytes([0, 0x80, 0x80])
        def share(body, rgb):
            return sum(1 for i in range(0, len(body) - 2, 3 * 97) if body[i:i + 3] == rgb) / (len(body) / (3 * 97))
        check('saver covers the desktop', share(saver, teal) < 0.05)
        check('display off is black', share(off, b'\0\0\0') > 0.999)
        check('input turns the display on', share(back, b'\0\0\0') < 0.999)
    check('host ended', result.returncode == 0)
    report = {'log': str(log_path), 'exit_code': result.returncode, 'steps': steps, 'checks': checks}
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    failed = [name for name, okay in checks.items() if not okay]
    if failed:
        print(f'SCREEN SAVER FAIL: {len(failed)} of {len(checks)} checks; see {log_path}')
        return 1
    print(f'SCREEN SAVER PASS: {len(checks)} checks; {out / "results.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
