#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The locales Regional Settings offers, and choosing one of them.

The first page of Regional Settings lists what EnumSystemLocalesA hands out
with LCID_INSTALLED. KERNEL32 takes the locales out of the registry, not out
of LOCALE.NLS: the values of HKLM\\System\\CurrentControlSet\\Control\\Nls\\
Locale and then those of Nls\\AddLocales, in the order the registry keeps
them, each value whose name is eight hexadecimal digits; LCID_INSTALLED
only those with a name as their data, LCID_SUPPORTED the empty ones too. A
callback that answers FALSE ends the values of one key, not those of the
next. The port handed out English (United States) alone, so the list had
one entry.

GetLocaleInfoA answers the texts of a locale in that locale's own ANSI code
page, when Windows has the table of that code page (CP_*.NLS); without it
the locale has no data. The port read every locale in code page 1252 and
left out all others.

tools/regional_locale_fixture.c, in sessions of neo98-kms over one private
copy of drive C:

  list    EnumSystemLocalesA against the registry, both flags; FALSE from
          the callback with a value in AddLocales; ERROR_INVALID_FLAGS and
          ERROR_INVALID_PARAMETER; Monday in Russian and the currency of
          Greece in code pages 1251 and 1253 where Windows has them
  choose  a currency symbol set while English (United States) is the
          locale, then INTL.CPL:
          "German (Austrian)" chosen on the first page, OK, No to the
          restart; Windows stores the locale and keeps the symbol, and
          every other value follows the new locale: Monday first, the
          schilling,
          the separators, the long date
  after   the same answers in a new session

Measured in Windows 95 and Windows 98 with the same program.

    build/analysis-venv/bin/python tools/test_regional_locale_host.py [--no-build] [--keep] [--kms KMS]
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
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/regional-locale'
# What Windows answers for German (Austrian) with a currency symbol the user
# set before: the user's symbol wins, the rest is the locale's.
AUSTRIAN = {
    'user locale': '0c07', 'answer short date': 'dd.MM.yy', 'answer long date': 'dddd, dd. MMMM yyyy',
    'answer first day': '0', 'answer currency': 'US$', 'answer decimal': ',', 'answer thousand': '.',
    'answer time format': 'HH:mm:ss', 'answer language': 'German (Austrian)', 'answer date separator': '.',
    'formatted short date': '28.11.26', 'formatted long date': 'Samstag, 28. November 2026',
    'formatted number': '-1.234.567,89', 'formatted currency': '-US$ 1.234.567,89'.encode('cp1252').hex(),
    'stored Locale': '00000C07', 'stored sCurrency': 'US$',
}


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
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/regional_locale_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32', '-ladvapi32',
                    '-o', str(drive / 'REGLOC.EXE')], check=True)
    logs = []

    def session(command):
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:640x480')
        for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD', 'NEO98_HOST_INPUT'):
            env.pop(variable, None)
        written = win98_c.path('C:\\REGLOC.TXT', drive)
        if written:
            written.unlink()
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '640x480',
                                 '--exe', 'C:\\REGLOC.EXE', '--command', 'REGLOC.EXE ' + command,
                                 '--time-limit', '120'], cwd=ROOT, env=env, capture_output=True, text=True,
                                timeout=240)
        logs.append(f'---- {command}: {result.returncode}\n{result.stdout}{result.stderr}')
        written = win98_c.path('C:\\REGLOC.TXT', drive)
        if written:
            shutil.copy2(written, WORK / f'{command}.txt')
        lines = written.read_bytes().decode('cp1252').splitlines() if written else ()
        return dict(line.split('=', 1) for line in lines if '=' in line)

    listed = session('list')
    chosen = session('choose')
    after = session('after')
    tables = win98_c.path('C:\\WINDOWS\\SYSTEM\\CP_1251.NLS', drive) is not None
    installed = listed.get('installed', '').split()
    checks = {
        'LCID_INSTALLED hands out the named values of Nls\\Locale and AddLocales in their order':
            listed.get('installed') == listed.get('registry installed') and len(installed) > 1 and
            listed.get('installed.result') == '1',
        'LCID_SUPPORTED hands out every value of eight hexadecimal digits':
            listed.get('supported') == listed.get('registry supported') and
            len(listed.get('supported', '').split()) >= len(installed),
        'FALSE from the callback ends one key: the first locale, then the value of AddLocales':
            listed.get('stopped', '').split() == [installed[0] if installed else '?', '0000ABCD'],
        'flags 3: ERROR_INVALID_FLAGS': listed.get('flags 3') == '0' and listed.get('flags 3.error') == '1004',
        'no callback: ERROR_INVALID_PARAMETER':
            listed.get('no callback') == '0' and listed.get('no callback.error') == '87',
        ('Monday in Russian in code page 1251, the currency of Greece in 1253' if tables else
         'no data for Russian and Greek without their code pages'):
            (listed.get('russian monday') == 'понедельник'.encode('cp1251').hex() and
             listed.get('greek currency') == 'Δρχ'.encode('cp1253').hex()) if tables else
            (listed.get('russian monday.size') == '0' and listed.get('greek currency.size') == '0'),
        f'Regional Settings lists every installed locale: {chosen.get("listed")} of {len(installed)}':
            chosen.get('sheet') == '1' and chosen.get('listed') == str(len(installed)),
        'German (Austrian) chosen, OK, the restart declined, the sheet closed':
            chosen.get('chosen') == '1' and chosen.get('selected length') == chosen.get('name length') and
            chosen.get('closed') == '1' and ('restart your computer' in chosen.get('box', '') or
                                             'restart your computer' in logs[1]),
        # Regional Settings stores the locale alone; KERNEL32 answers every
        # other value out of the new locale, unless the user set it.
        'stored: the locale and the currency set before, no value of the new locale':
            not set(chosen.get('stored names', '').split()) & {'sLongDate', 'iFirstDayOfWeek', 'sShortDate', 'sDecimal',
                                                               'sThousand', 'sTimeFormat', 'sLanguage', 'sCountry'},
    }
    for name, expected in AUSTRIAN.items():
        checks[f'{name}: {expected!r}, {chosen.get(name)!r} at once, {after.get(name)!r} in a new session'] = \
            chosen.get(name) == expected and after.get(name) == expected
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    (WORK / 'log.txt').write_text('\n'.join(logs))
    if not all(checks.values()):
        print('    log:', WORK / 'log.txt')
    elif not args.keep:
        shutil.rmtree(drive, ignore_errors=True)
    print(f'regional locale host: {sum(map(bool, checks.values()))} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
