#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The lists of KERNEL32's NLS functions: code pages, locales and their names.

tools/nls_lists_fixture.c, the same program in Windows 95 OSR2 and Windows
98 SE under QEMU, and here on a private copy of drive C:. What it measured:

  EnumSystemCodePagesA   the values of Nls\\CodePage in the registry's order
                         whose names have at most four characters and begin
                         with a digit; CP_INSTALLED those with a file name as
                         data. Windows 95 lists 25 and installs 1252, 437 and
                         850; Windows 98 lists and installs 15. The port
                         handed out a list of its own (437, 850, 866, 1252,
                         65000, 65001).
  IsValidCodePage        the ANSI and the OEM page, every page whose table
                         loads from the file its value names, and in Windows
                         98 also 65000 and 65001 (Windows 95: not). 866 has
                         a file in Windows 95 and no name in the registry:
                         not valid there.
  IsValidLocale          a locale of LOCALE.NLS is supported, and installed
                         when its code page's table loads; another one is
                         asked of the registry. Windows refuses locales once
                         a process has loaded 48 (Windows 95) or about 64
                         (Windows 98) of them, which made ten Spanish
                         locales of America look refused in Windows 98; the
                         first line asks two of them, and Hindi, before any
                         other: accepted.
  SLANGUAGE, SCOUNTRY    out of KERNEL32's string table and RCDATA, by the
                         language: Windows 95 says "English (British)",
                         Windows 98 "English (United Kingdom)"; both answer
                         the two names also for a locale whose code page is
                         missing (Russian in 95, Hebrew and Japanese in 98).
                         The length of SCOUNTRY counts one zero more than the
                         text has ("United States", 15). The port answered no
                         country at all and the names of Windows 98 on both.

The locales checked are those the fixture asks before Windows' limit.

    build/analysis-venv/bin/python tools/test_nls_lists_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture is built with it; docs/BUILDING.md
        print('SKIP: needs i686-w64-mingw32-gcc')
        _sys.exit(77)

import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402

NAMES = '|SLANGUAGE {}|SENGLANGUAGE {}|SNATIVELANGNAME {}|SABBREVLANGNAME {}|SCOUNTRY {}|SENGCOUNTRY {}|' \
        'SNATIVECTRYNAME {}|SABBREVCTRYNAME {}|ICOUNTRY {}'
NONE = NAMES.format(*['- 87'] * 9)
US = NAMES.format('24 English (United States)', '8 English', '8 English', '4 enu', '15 United States',
                  '14 United States', '14 United States', '4 USA', '2 1')
AUSTRIA = NAMES.format('18 German (Austrian)', '7 German', '8 Deutsch', '4 dea', '9 Austria', '8 Austria',
                       '11 \xd6sterreich', '4 AUT', '3 43')


def names_only(language, country):
    return NAMES.format(language, '- 87', '- 87', '- 87', country, '- 87', '- 87', '- 87', '- 87')


def britain(language):
    return NAMES.format(language, '8 English', '8 English', '4 eng', '16 United Kingdom', '15 United Kingdom',
                        '15 United Kingdom', '4 GBR', '3 44')


EXPECTED = {
    (4, 0): {
        'first': '240A:1 3C0A:1 439:0,0,0',
        'cp-installed': '1/0 1252 437 850',
        'cp-supported': '1/0 1250 1251 1252 1253 1254 1255 1256 1257 1361 437 737 775 850 852 857 855 862 864 866 '
                        '869 874 932 936 949 950',
        'cp-both': '0/1004',
        'cp-valid': '0:0 1:0 2:0 37:0 437:1 708:0 720:0 737:0 775:0 850:1 852:0 855:0 857:0 860:0 861:0 862:0 '
                    '863:0 864:0 865:0 866:0 869:0 874:0 932:0 936:0 949:0 950:0 1200:0 1250:0 1251:0 1252:1 '
                    '1253:0 1254:0 1255:0 1256:0 1257:0 1258:0 1361:0 10000:0 20866:1 28591:0 28592:1 28593:1 '
                    '28594:1 28595:0 28597:1 65000:0 65001:0',
        '00000409': 'inst 1|supp 1' + US,
        '00000809': 'inst 1|supp 1' + britain('18 English (British)'),
        '00000C07': 'inst 1|supp 1' + AUSTRIA,
        '00000419': 'inst 0|supp 1' + names_only('8 Russian', '8 Russia'),
        '00000425': 'inst 0|supp 1' + names_only('9 Estonian', '9 Estonia'),
        '00000000': 'inst 0|supp 0' + US,
        '00010409': 'inst 0|supp 0' + NONE,
    },
    (4, 10): {
        'first': '240A:1 3C0A:1 439:1,1,6',
        'cp-installed': '1/0 1250 1251 1252 1253 1254 1257 437 850 737 775 852 857 855 866 869',
        'cp-supported': '1/0 1250 1251 1252 1253 1254 1257 437 850 737 775 852 857 855 866 869',
        'cp-both': '0/1004',
        'cp-valid': '0:0 1:0 2:0 37:0 437:1 708:0 720:0 737:1 775:1 850:1 852:1 855:1 857:1 860:0 861:0 862:0 '
                    '863:0 864:0 865:0 866:1 869:1 874:0 932:0 936:0 949:0 950:0 1200:0 1250:1 1251:1 1252:1 '
                    '1253:1 1254:1 1255:0 1256:0 1257:1 1258:0 1361:0 10000:0 20866:1 28591:1 28592:1 28593:0 '
                    '28594:0 28595:1 28597:1 65000:1 65001:1',
        '00000409': 'inst 1|supp 1' + US,
        '00000809': 'inst 1|supp 1' + britain('25 English (United Kingdom)'),
        '00000C07': 'inst 1|supp 1' + AUSTRIA,
        '0000040D': 'inst 0|supp 1' + names_only('7 Hebrew', '8 Israel'),
        '00000411': 'inst 0|supp 1' + names_only('9 Japanese', '7 Japan'),
        '0000100A': 'inst 1|supp 1' + NAMES.format('20 Spanish (Guatemala)', '8 Spanish', '8 Espa\xf1ol', '4 esg',
                                                   '11 Guatemala', '10 Guatemala', '10 Guatemala', '4 GTM',
                                                   '4 502'),
        '00000000': 'inst 0|supp 0' + US,
        '00010409': 'inst 0|supp 0' + NONE,
    },
}


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('nls-lists')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    version = win98_c.kernel_version(drive)
    expected = EXPECTED.get(version, EXPECTED[(4, 10)])
    # Built as for the real Windows under QEMU: no SSE, subsystem 4.0.
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/nls_lists_fixture.c'), '-ffreestanding',
                    '-fno-builtin', '-nostartfiles', '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-march=pentium', '-mno-sse', '-mno-mmx', '-Wl,--major-subsystem-version,4',
                    '-lkernel32', '-luser32', '-ladvapi32', '-o', str(drive / 'NLSLIST.EXE')], check=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='120',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    registry = win98_c.journal(pristine)
    if registry.is_file():
        shutil.copy2(registry, out / 'c.registry')
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\NLSLIST.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=240)
    written = win98_c.path('C:\\NLSLIST.TXT', drive)
    lines = written.read_bytes().decode('latin-1').replace('\r', '').splitlines() if written else []
    shutil.rmtree(drive, ignore_errors=True)
    got = {}
    for line in lines:
        if line.startswith('locale '):
            got[line[7:15]] = line[16:]
        elif '=' in line:
            got[line.split('=', 1)[0]] = line.split('=', 1)[1]
    checks = {f'the fixture went through all its lines (KERNEL32 {version[0]}.{version[1]:02d})': 'done' in lines}
    for name, want in expected.items():
        checks[f'{name}: {got.get(name)!r} (Windows: {want!r})'] = got.get(name) == want
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    log:', out / 'log.txt')
    print(f'nls lists host: {sum(map(bool, checks.values()))} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
