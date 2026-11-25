#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The clocks of Windows count from the start of Neo98, not of the host.

On Windows, GetTickCount, timeGetTime, the time of a message and
QueryPerformanceCounter count from the start of Windows, and programs compare
them with each other. The port counted all of them from the boot of the host
(its monotonic clock), so a program showed hundreds of hours of uptime.

tools/clock_start_fixture.c reads the clocks of the 32-bit side,
tools/clock_start_win16_fixture.asm those of the 16-bit side (USER.13
GetTickCount, USER.15 GetCurrentTime, MMSYSTEM.607 timeGetTime), each in a
session of its own of neo98-host:

  32  GetTickCount is below the time the session took; timeGetTime,
      QueryPerformanceCounter over its frequency and the time of a message
      agree with it; it advances by the 500 ms of a Sleep
  16  the three clocks of the 16-bit side are below the time the session
      took and agree with each other

    build/analysis-venv/bin/python tools/test_clock_start_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc', 'nasm'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixtures with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)

import os
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win16_fixture  # noqa: E402
import win98_c  # noqa: E402


def session(binary, out, drive, program, result, size):
    """Runs one program; its results and the milliseconds the session took."""
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='40',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    began = time.monotonic()
    with (out / 'log.txt').open('ab') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', program, '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=120)
    took = int((time.monotonic() - began) * 1000)
    written = win98_c.path(result, drive)
    data = written.read_bytes() if written else b''
    return (struct.unpack(f'<{size // 4}I', data[:size]) if len(data) >= size else None), took


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('clock-start')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/clock_start_fixture.c'), '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-lwinmm', '-luser32', '-lkernel32',
                    '-o', str(drive / 'CLOCK32.EXE')], check=True)
    win16_fixture.assemble(ROOT / 'tools/clock_start_win16_fixture.asm', out / 'CLOCK16.EXE', 'CLOCK16',
                           neo98.tool_environment(), ('KERNEL', 'USER', 'MMSYSTEM'))
    shutil.copyfile(out / 'CLOCK16.EXE', drive / 'CLOCK16.EXE')
    try:
        flat, took32 = session(binary, out, drive, 'C:\\CLOCK32.EXE', 'C:\\CLOCK32.TXT', 28)
        words, took16 = session(binary, out, drive, 'C:\\CLOCK16.EXE', 'C:\\CLOCK16.TXT', 12)
    finally:
        shutil.rmtree(drive, ignore_errors=True)

    def near(a, b, within=100):
        return abs(int(a) - int(b)) <= within

    checks = {'both programs wrote their clocks': flat is not None and words is not None}
    if flat:
        tick, multimedia, counter, message, message_time, later, frequency = flat
        checks.update({
            f'32: GetTickCount {tick} ms counts from the start, within the {took32} ms of the session':
                0 < tick < took32,
            f'32: timeGetTime {multimedia} agrees with it': near(multimedia, tick),
            f'32: QueryPerformanceCounter over its frequency ({frequency} Hz) agrees with it: {counter}':
                frequency > 0 and near(counter, tick),
            f'32: the time of a message agrees with it: {message}, GetMessageTime {message_time}':
                near(message, tick) and message_time == message,
            f'32: it advances by the 500 ms of a Sleep: {later - tick}': 450 <= later - tick < 2000,
        })
    if words:
        tick, current, multimedia = words
        checks.update({
            f'16: GetTickCount {tick} ms counts from the start, within the {took16} ms of the session':
                0 < tick < took16,
            f'16: GetCurrentTime {current} and timeGetTime {multimedia} agree with it':
                near(current, tick) and near(multimedia, tick),
        })
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    log:', out / 'log.txt')
    print(f'clock start host: {sum(map(bool, checks.values()))} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
