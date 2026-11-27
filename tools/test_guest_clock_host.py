#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A date and time set in Windows is still set after a restart.

A PC keeps what SetLocalTime and SetSystemTime set in its battery clock,
which runs on while the machine is off. The port started every session at
the host's time. It now keeps the distance between the guest clock and the
host's clock in a file beside drive C:, `<drive>.clock`
(tools/clock_file.h), and never touches the host's clock.

tools/guest_clock_fixture.c in sessions of neo98-kms over one private copy
of drive C:

  a session that sets nothing shows the host's time and leaves no file
  SetLocalTime to 3 February 2001 04:05:06: the file is there
  the next session, two seconds later, shows that day and a clock that ran on
  SetSystemTime to 9 August 2007 10:11:12 UTC: the next session goes on from
  what that one showed
  a time zone set with SetTimeZoneInformation is the zone of the next
  session, and the local time did not move with it
  --host-clock starts at the host's time again and removes the file, and so
  does removing the file by hand

    build/analysis-venv/bin/python tools/test_guest_clock_host.py [--no-build] [--keep] [--kms KMS]
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
import datetime
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/guest-clock'


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
    clock = Path(str(drive) + '.clock')
    pristine = win98_c.use_pristine()
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), win98_c.journal(drive))
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/guest_clock_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32',
                    '-o', str(drive / 'GCLOCK.EXE')], check=True)
    logs = []

    def session(command, *options):
        """One run; what it wrote, the times as datetime."""
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:640x480')
        for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD', 'NEO98_HOST_INPUT'):
            env.pop(variable, None)
        written = win98_c.path('C:\\GCLOCK.TXT', drive)
        if written:
            written.unlink()
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '640x480',
                                 '--exe', 'C:\\GCLOCK.EXE', '--command', 'GCLOCK.EXE ' + command,
                                 '--time-limit', '60', *options], cwd=ROOT, env=env, capture_output=True,
                                text=True, timeout=120)
        logs.append(f'---- {command} {options}: {result.returncode}\n{result.stdout}{result.stderr}')
        written = win98_c.path('C:\\GCLOCK.TXT', drive)
        found = {}
        for line in (written.read_text().splitlines() if written else ()):
            name, _, value = line.partition('=')
            found[name] = (datetime.datetime.strptime(value, '%Y%m%d%H%M%S') if name in ('local', 'system')
                           else int(value))
        return found

    def near(found, wanted, within=90):
        """Whether a time of the guest lies at or shortly after `wanted`."""
        return found is not None and 0 <= (found - wanted).total_seconds() <= within

    try:
        now = datetime.datetime.now().replace(microsecond=0)
        plain = session('get')
        untouched = not clock.exists()
        local = session('set')
        stored = clock.read_text().split()[0] if clock.exists() else None
        time.sleep(2.2)
        later = session('get')
        utc = session('system')
        utc_later = session('get')
        zone = session('zone')
        zone_later = session('get')
        host = session('get', '--host-clock')
        host_now = datetime.datetime.now().replace(microsecond=0)
        removed = not clock.exists()
        session('set')
        clock.unlink(missing_ok=True)
        by_hand = session('get')
    finally:
        (WORK / 'log.txt').write_text('\n'.join(logs))
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
            clock.unlink(missing_ok=True)
    day = datetime.datetime(2001, 2, 3, 4, 5, 6)
    summer = datetime.datetime(2007, 8, 9, 10, 11, 12)
    checks = {
        f'a session that sets nothing shows the host\'s time: {plain.get("local")} for {now}':
            near(plain.get('local'), now - datetime.timedelta(seconds=2)),
        'and leaves no file beside the drive': untouched,
        f'SetLocalTime answers TRUE and the time is set: {local.get("set")} {local.get("local")}':
            local.get('set') == 1 and near(local.get('local'), day, 5),
        f'the distance to the host\'s clock is in {clock.name}: {stored}':
            stored is not None and stored.lstrip('-').isdigit() and int(stored) < -800_000_000_000,
        f'the next session has the set day, and the clock ran on meanwhile: {later.get("local")}':
            near(later.get('local'), day + datetime.timedelta(seconds=2), 60),
        # The hour Windows shows after SetSystemTime depends on whether the
        # new date lies on the other side of a daylight change; the day does
        # not, and the next session goes on from what this one showed.
        f'SetSystemTime is kept the same way: {utc.get("system")} then {utc_later.get("system")}':
            utc.get('set') == 1 and utc.get('system') is not None and utc['system'].date() == summer.date()
            and near(utc_later.get('system'), utc['system'], 60),
        f'a time zone set is the zone of the next session: bias {zone.get("bias")} then {zone_later.get("bias")}':
            zone.get('set') == 1 and zone.get('bias') == 300 and zone_later.get('bias') == 300,
        f'the local time did not move with the zone: {zone_later.get("local")} after {utc_later.get("local")}':
            zone_later.get('local') is not None and utc_later.get('local') is not None
            and 0 <= (zone_later['local'] - utc_later['local']).total_seconds() <= 60,
        f'--host-clock starts at the host\'s time: {host.get("local")} for {host_now}':
            near(host.get('local'), host_now - datetime.timedelta(seconds=10), 20),
        'and removes the file': removed,
        f'so does removing the file by hand: {by_hand.get("local")}':
            near(by_hand.get('local'), host_now - datetime.timedelta(seconds=10), 60),
    }
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'guest clock host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
