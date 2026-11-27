#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A time or date picture set in Regional Settings changes what follows from it.

SetLocaleInfoA stores a value of the user's locale under
HKCU\\Control Panel\\International. For the time format and the short date
KERNEL32 also stores what follows from the picture: "iTime", "iTLZero" and
"iTimePrefix", and "iDate" and "sDate". The port stored the picture alone,
so after choosing a 24-hour style LOCALE_ITIME still said 12 hours, and
Date/Time of the Control Panel, which asks for it, went on showing AM and
PM. It also took LOCALE_ITIMEMARKPOSN for the format of negative numbers.

tools/locale_settings_fixture.c in sessions of neo98-kms over one private
copy of drive C:

  HH:mm:ss and dd.MM.yyyy: 24 hours, a leading zero, no marker first, the day
  first and "." between the fields, at once and in a new session; the time
  and the date are formatted that way
  LOCALE_IDATE itself cannot be set (ERROR_INVALID_FLAGS)
  tt h:mm:ss and M/d/yy: 12 hours, no zero, the marker first, the month
  first and "/"

The lists of the locale are those of the installed version's LOCALE.NLS:
the short dates of English (United States) on Windows 95 have no
yyyy-MM-dd, which Windows 98 added.

EnumTimeFormatsA and EnumDateFormatsA of the user's locale hand out the
picture the user has set first and leave it out of the list of the locale
that follows. Regional Settings fills its "Time style" and date style boxes
from them and selects the entry that begins with the picture in use
(CB_FINDSTRING): with the locale's order alone, "H:mm:ss" selected
"h:mm:ss tt", the first entry, every time the page was opened again. A
picture that is not one of the locale's is one entry more; the list of
another locale has only its own.

A separator set alone rewrites its picture: LOCALE_STIME puts the new
separator between the fields H, h, m and s of the time format and leaves
what leads to the marker t or to a quote, LOCALE_SDATE replaces the first
separator of the short date and every later one that equals it. Regional
Settings sets only the separator when only that box was changed and counts
on this. A separator has at most three characters, no digit and none of the
letters of its picture (ERROR_INVALID_PARAMETER). LOCALE_STIMEFORMAT stores
"sTime" as well, what stands between its first two fields, and nothing for
a picture without such a pair.

KERNEL32 compares every value it would store, the one given and those that
follow from it, with what the locale itself has for that type, and removes
the stored value when they are equal: after "0" and then "6" for the first
day of the week of English (United States) no iFirstDayOfWeek is stored
and the locale answers 6; the same for the list separator, for the default
time format and what follows from it, and for the default short date.
LOCALE_ITIME rewrites the time format, every h to H for 1 and back for 0.
The first day of the week takes one digit from 0 to 6.

A new locale (Locale of HKCU\\Control Panel\\International and
InvalidateNLSCache, as Regional Settings does it) answers its own values:
German (Germany) begins the week on Monday (0).

    build/analysis-venv/bin/python tools/test_locale_settings_host.py [--no-build] [--keep] [--kms KMS]
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
WORK = ROOT / 'build/locale-settings'
# The lists of English (United States) and the time formats of German
# (Germany) in LOCALE.NLS, in its order.
TIMES = ['h:mm:ss tt', 'hh:mm:ss tt', 'H:mm:ss', 'HH:mm:ss']
SHORT = ['M/d/yy', 'M/d/yyyy', 'MM/dd/yy', 'MM/dd/yyyy', 'yy/MM/dd', 'yyyy-MM-dd', 'dd-MMM-yy']
# The short dates of English (United States) in LOCALE.NLS of Windows 95
# (KERNEL32 4.00): the same without yyyy-MM-dd.
SHORT_4_00 = ['M/d/yy', 'M/d/yyyy', 'MM/dd/yy', 'MM/dd/yyyy', 'yy/MM/dd', 'dd-MMM-yy']


def kernel_version(drive):
    """(major, minor) of the file version of the drive's KERNEL32.DLL, from
    its VS_FIXEDFILEINFO."""
    path = win98_c.path('C:\\WINDOWS\\SYSTEM\\KERNEL32.DLL', drive)
    data = path.read_bytes() if path else b''
    at = data.find(struct.pack('<I', 0xFEEF04BD))
    if at < 0:
        return (4, 10)
    high = struct.unpack_from('<I', data, at + 8)[0]
    return (high >> 16, high & 0xffff)
LONG = ['dddd, MMMM dd, yyyy', 'MMMM dd, yyyy', 'dddd, dd MMMM, yyyy', 'dd MMMM, yyyy']
GERMAN = ['HH:mm:ss', 'H:mm:ss', 'H.mm', "H.mm' Uhr '"]


def listed(own, locale):
    """What an enumeration writes: the user's picture, then the locale's without it."""
    return ''.join(picture + '|' for picture in [own] + [picture for picture in locale if picture != own])


EUROPEAN = {'time format': 'HH:mm:ss', '24 hours': '1', 'hour zero': '1', 'marker first': '0',
            'short date': 'dd.MM.yyyy', 'date order': '1', 'date separator': '.', 'time': '15:04:05',
            'date': '03.02.2001', 'time formats': listed('HH:mm:ss', TIMES),
            'short dates': listed('dd.MM.yyyy', SHORT), 'long dates': listed(LONG[0], LONG),
            'other locale': listed(GERMAN[0], GERMAN)}
AMERICAN = {'time format': 'tt h:mm:ss', '24 hours': '0', 'hour zero': '0', 'marker first': '1',
            'short date': 'M/d/yy', 'date order': '0', 'date separator': '/', 'time': 'PM 3:04:05',
            'date': '2/3/01', 'time formats': listed('tt h:mm:ss', TIMES),
            'short dates': listed('M/d/yy', SHORT), 'long dates': listed(LONG[0], LONG),
            'other locale': listed(GERMAN[0], GERMAN)}


SEPARATORS = {
    'separator of h:mm:ss tt': ':',
    'time separator .': '1 0', 'time format with .': 'h.mm.ss tt', 'time with .': '3.04.05 PM',
    'date separator -': '1 0', 'short date with -': 'M-d-yy', 'date with -': '2-3-01',
    'date separator . of two': '1 0', 'short date of two separators': 'dd.MM-yyyy',
    "separator of tt h:mm' Uhr 'ss": ':',
    'time separator empty': '1 0', 'time format without separator': "tt hmm' Uhr 'ss",
    'time separator h': '0 87', 'time separator 1': '0 87', 'date separator of four': '0 87',
    'date separator M': '0 87',
    'separator of HHmm': '',
}
DEFAULTS = {
    'first day 0': '1 0', 'stored first day 0': '0', 'first day after 0': '0',
    'first day 6': '1 0', 'stored first day 6': 'absent', 'first day after 6': '6',
    'first day 7': '0 87',
    'list ;': '1 0', 'stored list ;': ';', 'list ,': '1 0', 'stored list ,': 'absent',
    'time format HH:mm:ss': '1 0', 'stored HH:mm:ss': 'HH:mm:ss', 'stored iTime of HH:mm:ss': '1',
    'time format h:mm:ss tt': '1 0', 'stored h:mm:ss tt': 'absent', 'stored iTime of h:mm:ss tt': 'absent',
    'stored iTLZero of h:mm:ss tt': 'absent', 'stored sTime of h:mm:ss tt': 'absent',
    'iTime 1': '1 0', 'time format after iTime 1': 'H:mm:ss tt', 'stored iTime 1': '1',
    'time after iTime 1': '15:04:05 PM',
    'iTime 0': '1 0', 'time format after iTime 0': 'h:mm:ss tt', 'stored time format after iTime 0': 'absent',
    'short date M/d/yy': '1 0', 'stored M/d/yy': 'absent', 'stored iDate of M/d/yy': 'absent',
    'stored sDate of M/d/yy': 'absent',
}
GERMAN = {'user locale': '407', 'first day': '0', 'short date': 'dd.MM.yy', 'time format': 'HH:mm:ss',
          'decimal': ','}


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
    # The locale's own list of short dates is that of the installed version.
    if kernel_version(drive) < (4, 10):
        for expected, own in ((EUROPEAN, 'dd.MM.yyyy'), (AMERICAN, 'M/d/yy')):
            expected['short dates'] = listed(own, SHORT_4_00)
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/locale_settings_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32', '-ladvapi32',
                    '-o', str(drive / 'LOCSET.EXE')], check=True)
    logs = []

    def session(command):
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:640x480')
        for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD', 'NEO98_HOST_INPUT'):
            env.pop(variable, None)
        written = win98_c.path('C:\\LOCSET.TXT', drive)
        if written:
            written.unlink()
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '640x480',
                                 '--exe', 'C:\\LOCSET.EXE', '--command', 'LOCSET.EXE ' + command,
                                 '--time-limit', '60'], cwd=ROOT, env=env, capture_output=True, text=True,
                                timeout=120)
        logs.append(f'---- {command}: {result.returncode}\n{result.stdout}{result.stderr}')
        written = win98_c.path('C:\\LOCSET.TXT', drive)
        return dict(line.split('=', 1) for line in (written.read_text().splitlines() if written else ()) if '=' in line)

    try:
        runs = {name: session(name.split()[0]) for name in ('set', 'get after set', 'back', 'get after back',
                                                            'separators', 'defaults', 'german')}
    finally:
        (WORK / 'log.txt').write_text('\n'.join(logs))
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
            Path(str(drive) + '.clock').unlink(missing_ok=True)

    def differing(found, wanted):
        return {name: (found.get(name), value) for name, value in wanted.items() if found.get(name) != value}

    first = runs['set']
    checks = {
        f'both pictures are taken: {first.get("set time")} {first.get("set date")}':
            first.get('set time') == '1' and first.get('set date') == '1',
        f'LOCALE_IDATE itself is refused with ERROR_INVALID_FLAGS: {first.get("set order")} '
        f'{first.get("set order error")}': first.get('set order') == '0' and first.get('set order error') == '1004',
        f'HH:mm:ss and dd.MM.yyyy, at once: {differing(first, EUROPEAN)}': not differing(first, EUROPEAN),
        f'HH:mm:ss and dd.MM.yyyy in a new session: {differing(runs["get after set"], EUROPEAN)}':
            not differing(runs['get after set'], EUROPEAN),
        f'tt h:mm:ss and M/d/yy, at once: {differing(runs["back"], AMERICAN)}': not differing(runs['back'], AMERICAN),
        f'tt h:mm:ss and M/d/yy in a new session: {differing(runs["get after back"], AMERICAN)}':
            not differing(runs['get after back'], AMERICAN),
        f'a separator rewrites its picture, a picture names its separator: '
        f'{differing(runs["separators"], SEPARATORS)}': not differing(runs['separators'], SEPARATORS),
        f'a value equal to the locale\'s own is removed, LOCALE_ITIME rewrites the time format: '
        f'{differing(runs["defaults"], DEFAULTS)}': not differing(runs['defaults'], DEFAULTS),
        f'a new locale answers its own values: {differing(runs["german"], GERMAN)}':
            not differing(runs['german'], GERMAN),
    }
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'locale settings host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
