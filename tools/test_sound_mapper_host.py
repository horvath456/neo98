#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The preferred devices of the wave mapper, and the volume of the multimedia page.

The wave mapper reads "Playback" and "Record" of
HKCU\\Software\\Microsoft\\Multimedia\\Sound Mapper when it is loaded at the
start of Windows and, when a value is not there, writes the name of the first
device that has a mixer and capabilities; a value that is there is left
alone. The Audio page of the Control Panel's Multimedia opens the mixer of
the device "Playback" names and shows and moves the volume of its wave line
under the speakers; without the name it showed nothing and its slider stood
at Low. A driver keeps the levels of its mixer over a restart.

tools/sound_mapper_fixture.c in sessions of neo98-kms over one private copy
of drive C:, the microphone the test device of the port
(NEO98_TEST_MICROPHONE):

  without a microphone a new session writes "Playback" and no "Record", and
  there is no input device
  with one it writes "Record" as well
  a "Playback" of another machine's device is left alone
  a wave volume set through the mixer is there in the next session
  Windows 95 (KERNEL32 4.00): the multimedia page, its slider moved to Low
  with the Home key and closed with OK, sets the wave volume to 0, and the
  next session has it; the page of Windows 98 has no slider (its volume is a
  button that starts the Volume Control)

    build/analysis-venv/bin/python tools/test_sound_mapper_host.py [--no-build] [--keep] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
if __name__ == '__main__':
    import shutil as _shutil
    if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture is built with it; docs/BUILDING.md
        print('SKIP: needs i686-w64-mingw32-gcc')
        _sys.exit(77)
import argparse
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/sound-mapper'
FOREIGN = 'SB16 Wave Out [220]'


def kernel_version(drive):
    """(major, minor) of the file version of the drive's KERNEL32.DLL."""
    path = win98_c.path('C:\\WINDOWS\\SYSTEM\\KERNEL32.DLL', drive)
    data = path.read_bytes() if path else b''
    at = data.find(struct.pack('<I', 0xFEEF04BD))
    if at < 0:
        return (4, 10)
    high = struct.unpack_from('<I', data, at + 8)[0]
    return (high >> 16, high & 0xffff)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--keep', action='store_true', help='keep the copy of the drive')
    parser.add_argument('--kms', type=Path, default=BINARY)
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host(vulkan=False)
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    drive = WORK / 'c'
    pristine = win98_c.use_pristine()
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), win98_c.journal(drive))
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/sound_mapper_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32', '-ladvapi32',
                    '-lwinmm', '-o', str(drive / 'MAPPER.EXE')], check=True)
    logs = []

    def session(command, microphone, steps=()):
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:800x600',
                   NEO98_HOST_INPUT=';'.join(steps), NEO98_TEST_MICROPHONE=microphone)
        for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
            env.pop(variable, None)
        written = win98_c.path('C:\\MAPPER.TXT', drive)
        if written:
            written.unlink()
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '800x600',
                                 '--exe', 'C:\\MAPPER.EXE', '--command', 'MAPPER.EXE ' + command,
                                 '--time-limit', '40'], cwd=ROOT, env=env, capture_output=True, text=True,
                                timeout=120)
        logs.append(f'---- {command} ({microphone}): {result.returncode}\n{result.stdout}{result.stderr}')
        written = win98_c.path('C:\\MAPPER.TXT', drive)
        return dict(line.split('=', 1) for line in (written.read_text().splitlines() if written else ()) if '=' in line)

    try:
        session('forget', 'none')
        quiet = session('read', 'none')
        session('forget', 'sine')
        heard = session('read', 'sine')
        session('foreign', 'sine')
        foreign = session('read', 'sine')
        session('lower', 'sine')
        lowered = session('read', 'sine')
        slider = kernel_version(drive) < (4, 10)
        if slider:
            session('forget', 'sine')
            moved = session('applet', 'sine', ['0:wait=#32770', '0:idle=2000', '1:key=24', '2:key=0d'])
            after = session('read', 'sine')
    finally:
        (WORK / 'log.txt').write_text('\n'.join(logs))
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
            Path(str(drive) + '.clock').unlink(missing_ok=True)
    output = quiet.get('output')
    checks = {
        f'without a microphone the start writes "Playback", the first output device: {quiet.get("playback")!r} '
        f'({output!r})': bool(output) and quiet.get('playback') == output,
        f'and no "Record", there being no input device: {quiet.get("record")!r} {quiet.get("inputs")}':
            quiet.get('record') == 'absent' and quiet.get('inputs') == '0',
        f'with a microphone it writes "Record", the first input device: {heard.get("record")!r} '
        f'({heard.get("input")!r})': heard.get('inputs') == '1' and bool(heard.get('input'))
            and heard.get('record') == heard.get('input'),
        f'a "Playback" of another machine is left alone: {foreign.get("playback")!r}':
            foreign.get('playback') == FOREIGN,
        f'a wave volume set through the mixer is there in the next session: {lowered.get("wave")}':
            lowered.get('wave') == '16384' and lowered.get('master') == '65535',
    }
    if slider:
        checks.update({
            f'the page shows the wave volume and the slider moved to Low sets it: {moved.get("wave")}':
                moved.get('wave') == '0' and moved.get('master') == '65535',
            f'the next session has it: {after.get("wave")} {after.get("master")}':
                after.get('wave') == '0' and after.get('master') == '65535',
        })
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'sound mapper host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
