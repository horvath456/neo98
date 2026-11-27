#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A stand of the host's main thread: the sound holds, and the log says where.

The whole machine runs on the main thread of the host, between the events of
the Mac and the present of the picture. When the Mac keeps that thread, no
Windows thread runs: a looping sound would play the same piece over and over
(Sound::Mixer::Beat holds the mixer instead), and the log has to say where
the thread stood.

tools/machine_stand_fixture.c plays TADA.WAV in a loop. The input script
makes the main thread stand for 400 ms (stand=400), as the Mac sometimes
does. Then:
- the log has a slow turn of 400 ms or more that puts the time outside the
  guest, the GPU, the events and the present, and counts a hold of the sound;
- the stall watch (tools/host_stall_stack.h) wrote the stack of the main
  thread while it stood, down to the sleep that kept it (on the Mac).

    build/analysis-venv/bin/python tools/test_machine_stand_host.py [--no-build]

Results in build/machine-stand-host/results.json.
"""
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
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

MAC = sys.platform == 'darwin'


def main():
    out = neo98.output_directory('machine-stand-host')
    for name in ('drive-c', 'settings', 'reports'):
        shutil.rmtree(out / name, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if MAC else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()),
                    str(out / 'drive-c')], check=True)
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/machine_stand_fixture.c'), '-O1', '-ffreestanding',
                    '-fno-builtin', '-nostdlib', '-nostartfiles', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-luser32', '-lwinmm', '-lkernel32', '-o', str(out / 'drive-c/STAND.EXE')],
                   check=True, stdin=subprocess.DEVNULL)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    steps = ['0:wait=|playing', '0:idle=500', '1:stand=400', '2:idle=500', '3:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'), NEO98_HOST_DEADLINE='120')
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_SCREEN'):
        env.pop(key, None)
    command = [str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--system-tree', '--exe', 'C:\\STAND.EXE',
               '--quiet', '--drive-c', str(out / 'drive-c'), '--no-registry-file',
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        code = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                              stdin=subprocess.DEVNULL, timeout=300).returncode
    shutil.rmtree(out / 'drive-c', ignore_errors=True)
    text = log_path.read_text(errors='replace')
    turns = [dict(re.findall(r'(\w+)=(-?\d+)', line))
             for line in re.findall(r'^NEO98 HOST slow turn (.*)$', text, re.M)]
    stand = [t for t in turns if int(t.get('ms', 0)) >= 400 and int(t.get('other_ms', 0)) >= 380]
    frames = re.findall(r'^NEO98 HOST stall frame \d+ (.*)$', text, re.M)
    checks = {
        'the fixture plays its sound': 'input wait=|playing reached' in text,
        'a slow turn of 400 ms or more with the time outside guest, GPU, events and present': bool(stand),
        'the stand held the sound': bool(stand) and int(stand[0].get('sound_holds', 0)) >= 1,
        'the stall watch wrote the stack of the main thread down to its sleep':
            (len(frames) >= 3 and any('sleep' in f or 'semwait' in f for f in frames)) if MAC else None,
        'the host ended': code == 0,
    }
    (out / 'results.json').write_text(json.dumps({'turns': turns, 'frames': frames[:20], 'checks': checks},
                                                 indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'skip' if okay is None else 'ok  ' if okay else 'FAIL'} {name}")
    if not all(okay is not False for okay in checks.values()):
        print(f'MACHINE STAND FAIL; see {out}')
        return 1
    print('MACHINE STAND PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
