#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Text typed into the boxes of Regional Settings is taken.

INTL.CPL of the Control Panel reads each of its editable combo boxes as "the
selected item, or else the text" (CB_GETCURSEL, then CB_GETLBTEXT or
GetDlgItemText) and sets the value with SetLocaleInfoA. A page marks a box
as changed on CBN_SELCHANGE and on CBN_EDITCHANGE. The Date page sets its
two styles when it is left (PSN_KILLACTIVE), which OK sends to the page in
front; PSN_APPLY only clears its flags.

The applet runs in sessions of neo98-kms over one private copy of drive C:,
each page opened with `RUNDLL32 SHELL32.DLL,Control_RunDLL INTL.CPL,,PAGE` and
filled by the input script: the keys go into the box that has the focus, Tab
moves on in the order of the page's template, Enter is OK.

  Date: short date style dd.MM.yy, long date style d. MMMM yyyy
  Time: AM symbol vorm, PM symbol nachm, time separator "." (which
  rewrites the style: h.mm.ss tt)
  Number: decimal symbol ",", digit grouping symbol ".", list separator ";"
  Currency: currency symbol EUR, decimal symbol ",", digit grouping symbol "."

A session after them (a new start of the host) reads every value back with
GetLocaleInfoA and formats a date, a time, a number and an amount with them
(tools/locale_settings_fixture.c, LOCSET.EXE typed). Then the time style
HH:mm:ss is typed alone and read back: a changed time style is set first,
and the page then fills its separator box anew from it and sets that, so
the separator follows the style (":"). With the style and the separator
changed together the style wins, on Windows 98 as here.

    build/analysis-venv/bin/python tools/test_regional_typed_host.py [--no-build] [--keep] [--kms KMS]
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
WORK = ROOT / 'build/regional-typed'
SHEET = '#32770|Regional Settings Properties'


# Per page: its number for Control_RunDLL and what is typed, a Tab between
# boxes ('\t'), Enter at the end.
PAGES = {
    'date': (4, ['dd.MM.yy', '\t', '\t', 'd. MMMM yyyy']),
    'time': (3, ['vorm', '\t', 'nachm', '\t', '.']),
    'number': (1, [',', '\t', '\t', '.', '\t', '\t', '\t', '\t', '\t', '\t', ';']),
    'currency': (2, ['EUR', '\t', '\t', '\t', ',', '\t', '\t', '.']),
}
STYLE = (3, ['\t', '\t', '\t', 'HH:mm:ss'])
EXPECTED = {
    'short date': 'dd.MM.yy', 'long date': 'd. MMMM yyyy', 'date separator': '.',
    'time format': 'h.mm.ss tt', 'time separator': '.', 'am': 'vorm', 'pm': 'nachm',
    'decimal': ',', 'thousand': '.', 'list': ';',
    'currency': 'EUR', 'money decimal': ',', 'money thousand': '.',
    'short': '03.02.01', 'long': '3. February 2001', 'time': '3.04.05 nachm',
    'number': '1.234.567,50', 'amount': 'EUR1.234.567,50',
}
AFTER_STYLE = {'time format': 'HH:mm:ss', 'time separator': ':', 'time': '15:04:05', 'am': 'vorm'}


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
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/locale_settings_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32', '-ladvapi32',
                    '-o', str(drive / 'LOCSET.EXE')], check=True)
    logs = []

    def session(exe, command, steps=()):
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:800x600',
                   NEO98_HOST_INPUT=';'.join(steps))
        for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
            env.pop(variable, None)
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '800x600',
                                 '--exe', exe, '--command', command, '--time-limit', '40'], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=120)
        logs.append(f'---- {command}: {result.returncode}\n{result.stdout}{result.stderr}')
        return result.returncode

    closed = {}

    def fill(name, page, typing):
        steps, at = [f'0:wait={SHEET}', '0:idle=1500'], 1.0
        for piece in typing:
            # The script separates its steps with ";": that key is VK_OEM_1 of
            # the US layout.
            key = {'\t': '09', ';': 'ba'}.get(piece)
            steps.append(f'{at:.1f}:key={key}' if key else f'{at:.1f}:type={piece}')
            at += 0.5
        steps.append(f'{at + 0.5:.1f}:key=0d')
        closed[name] = session('C:\\WINDOWS\\RUNDLL32.EXE',
                               f'RUNDLL32.EXE SHELL32.DLL,Control_RunDLL INTL.CPL,,{page}', steps)

    def read_back():
        written = win98_c.path('C:\\LOCSET.TXT', drive)
        if written:
            written.unlink()
        session('C:\\LOCSET.EXE', 'LOCSET.EXE typed')
        written = win98_c.path('C:\\LOCSET.TXT', drive)
        return dict(line.split('=', 1) for line in (written.read_text().splitlines() if written else ())
                    if '=' in line)

    try:
        for name, (page, typing) in PAGES.items():
            fill(name, page, typing)
        found = read_back()
        fill('time style', *STYLE)
        styled = read_back()
    finally:
        (WORK / 'log.txt').write_text('\n'.join(logs))
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
            Path(str(drive) + '.clock').unlink(missing_ok=True)
    checks = {f'the {name} page closed with OK: {code}': code == 0 for name, code in closed.items()}
    checks.update({f'{name}: {found.get(name)!r} (expected {wanted!r})': found.get(name) == wanted
                   for name, wanted in EXPECTED.items()})
    checks.update({f'after the time style alone, {name}: {styled.get(name)!r} (expected {wanted!r})':
                   styled.get(name) == wanted for name, wanted in AFTER_STYLE.items()})
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'regional typed host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
