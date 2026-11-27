#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A sound assigned to an event in Sounds plays for that event.

The Sounds page of the Control Panel writes the file of an event as the
default value of HKCU\\AppEvents\\Schemes\\Apps\\.Default\\EVENT\\.Current.
MessageBeep and USER itself play those events: USER hands the alias to the
play routine of MMSYSTEM with flags of its own. The port sent them through
PlaySound of WINMM, which refuses one of those flags, so no event of USER
ever sounded, and MessageBeep was the beep of the speaker whatever the
scheme said. The events of the menu and of minimize, maximize and restore
were not raised at all.

tools/event_sound_fixture.c on neo98-kms over one private copy of drive C:,
each session with the samples the device plays recorded (NEO98_AUDIO_TAP):

  assigned in one session, played in the next: MessageBeep(MB_ICONASTERISK)
  plays the file of SystemAsterisk as long as that file lasts
  an event with an empty entry is the 40 ms beep of the speaker
  with the beep off (SPI_SETBEEP) MessageBeep is silent
  PlaySound with SND_ALIAS plays the event and answers FALSE for one without
  an entry
  a window with a menu bar: opening a menu is MenuPopup, choosing an item
  MenuCommand, and the system commands SC_MAXIMIZE, SC_RESTORE and
  SC_MINIMIZE are Maximize, RestoreDown, Minimize and, out of the minimized
  state, RestoreUp

    build/analysis-venv/bin/python tools/test_event_sounds_host.py [--no-build] [--keep] [--kms KMS]
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
import array
import os
import re
import shutil
import subprocess
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/event-sounds'
RATE = 44100
MEDIA = 'C:\\WINDOWS\\MEDIA\\'
ASSIGNED = {'SystemAsterisk': 'CHIMES.WAV', 'SystemQuestion': '', 'MenuPopup': 'DING.WAV', 'MenuCommand': 'DING.WAV',
            'Maximize': 'DING.WAV', 'Minimize': 'DING.WAV', 'RestoreUp': 'DING.WAV', 'RestoreDown': 'DING.WAV'}


def sounding(tap, floor=64):
    """Seconds between the first and the last loud frame of a recording, 0 for silence."""
    if not tap.exists():
        return 0.0
    samples = array.array('h')
    samples.frombytes(tap.read_bytes()[: tap.stat().st_size // 4 * 4])
    left = samples[0::2]
    loud = [i for i in range(0, len(left), 16) if abs(left[i]) > floor]
    return (loud[-1] - loud[0]) / RATE if loud else 0.0


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
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/event_sound_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32', '-ladvapi32',
                    '-lwinmm', '-o', str(drive / 'EVSND.EXE')], check=True)
    with wave.open(str(win98_c.path(MEDIA + 'CHIMES.WAV', drive))) as chimes:
        chimes_seconds = chimes.getnframes() / chimes.getframerate()
    logs, count = [], [0]

    def session(command, steps=()):
        """One run; the seconds of sound it played and its log."""
        count[0] += 1
        tap = WORK / f'tap{count[0]}.raw'
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:640x480',
                   NEO98_AUDIO_TAP=str(tap), NEO98_HOST_INPUT=';'.join(steps))
        for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
            env.pop(variable, None)
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '640x480',
                                 '--exe', 'C:\\EVSND.EXE', '--command', 'EVSND.EXE ' + command,
                                 '--time-limit', '60'], cwd=ROOT, env=env, capture_output=True, text=True,
                                timeout=120)
        log = result.stdout + result.stderr
        logs.append(f'---- {command}: {result.returncode}\n{log}')
        return sounding(tap), log

    def answer():
        written = win98_c.path('C:\\EVSND.TXT', drive)
        text = written.read_text() if written else ''
        if written:
            written.unlink()
        return text.strip()

    try:
        for event, file in ASSIGNED.items():
            session(f'assign {event} {MEDIA + file if file else ""}'.rstrip())
        asterisk, asterisk_log = session('beep 64')
        question, question_log = session('beep 32')
        quiet, _ = session('quiet 64')
        alias, _ = session('alias SystemAsterisk')
        alias_answer = answer()
        missing, _ = session('alias NoSuchEventOfThisTest')
        missing_answer = answer()
        # ALT+F and the letter of an item of the File menu: Nothing,
        # Maximize, Restore, Minimize (after which the window restores
        # itself) and Exit.
        steps, at = ['0:wait=EventSounds', '0:idle=500'], 0.0
        for item in 'NXRIE':
            steps += [f'{at:.1f}:alt=46', f'{at + 0.7:.1f}:key={ord(item):02x}']
            at += 2.6 if item == 'I' else 1.4
        _, window_log = session('window', steps)
    finally:
        (WORK / 'log.txt').write_text('\n'.join(logs))
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
            Path(str(drive) + '.clock').unlink(missing_ok=True)
    stops = [line for line in '\n'.join(logs).splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line]
    events = re.findall(r'SOUND event name=(\S+) flags=[0-9a-f]+ result=(\d+)', window_log)
    names = [name for name, _ in events]
    wanted = ['MenuPopup', 'MenuCommand', 'MenuPopup', 'MenuCommand', 'Maximize', 'MenuPopup', 'MenuCommand',
              'RestoreDown', 'MenuPopup', 'MenuCommand', 'Minimize', 'RestoreUp', 'MenuPopup', 'MenuCommand']
    checks = {
        f'the sessions ran without a stop: {stops[:1]}': not stops,
        f'MessageBeep(MB_ICONASTERISK) plays the {chimes_seconds:.2f} s of the file assigned to SystemAsterisk: '
        f'{asterisk:.2f} s': 'SOUND event name=SystemAsterisk' in asterisk_log
            and chimes_seconds * 0.85 <= asterisk <= chimes_seconds * 1.15,
        f'an event with an empty entry is the 40 ms beep of the speaker: {question * 1000:.0f} ms':
            'SOUND event name=SystemQuestion' in question_log and 0.03 <= question <= 0.06,
        f'with the beep off MessageBeep is silent: {quiet * 1000:.0f} ms': quiet == 0,
        f'PlaySound with SND_ALIAS plays the event: {alias_answer} {alias:.2f} s':
            alias_answer == 'played=1' and chimes_seconds * 0.85 <= alias <= chimes_seconds * 1.15,
        f'and answers FALSE for an event without an entry: {missing_answer} {missing * 1000:.0f} ms':
            missing_answer == 'played=0' and missing == 0,
        f'the menu and the window states raise their events in the order of Windows: {names}': names == wanted,
        f'the first of them plays: {events[:1]}': events[:1] == [('MenuPopup', '0')],
    }
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Recordings and log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'event sounds host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
