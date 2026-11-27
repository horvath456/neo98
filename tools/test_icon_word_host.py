#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The low word of an icon handle names the icon.

Under Windows 9x an icon handle is a 16-bit handle of USER, and programs
written for it keep and pass icons as words. The Explorer of Windows 95 sets
the icon of a folder window with WM_SETICON and the low word of the handle
ImageList_GetIcon gave it. The handles of the port carry a tag above bit 15:
the word named nothing, WM_GETICON answered it as it came, and DrawIconEx and
GetIconInfo refused it, so nobody could draw the icon of such a window, and
Explorer's DestroyIcon of the previous icon, also by its word, freed nothing.

tools/icon_word_fixture.c in neo98-kms over a private copy of drive C: makes
an icon and uses its low word: DrawIconEx draws the same picture, GetIconInfo
answers, a class is registered with it, WM_SETICON and SetClassLong take it
and WM_GETICON, the next WM_SETICON and GetClassLong answer the whole handle,
CopyIcon copies it, DestroyIcon destroys the copy by its word and neither the
copy's handle nor its word names an icon afterwards, while the first icon is
still drawn. A word with another generation names no icon.

    build/analysis-venv/bin/python tools/test_icon_word_host.py [--no-build] [--keep] [--kms KMS]
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
WORK = ROOT / 'build/icon-word'
STEPS = ('icon made', 'the handle draws', 'the word draws the same', 'the word has its bitmaps', 'class registered',
         'class icon is the handle', 'window icon is the handle', 'previous icon is the handle',
         'small class icon is the handle', 'the word is copied', 'the word is destroyed',
         'the destroyed handle is gone', 'the destroyed word is gone', 'the icon is still there',
         'no icon by another word')
EXPECTED = {name: '1' for name in STEPS}


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
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/icon_word_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lgdi32', '-lkernel32',
                    '-o', str(drive / 'ICONWORD.EXE')], check=True)
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:800x600')
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD', 'NEO98_HOST_INPUT'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '800x600',
                                 '--exe', 'C:\\ICONWORD.EXE', '--time-limit', '60'], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=120)
        (WORK / 'log.txt').write_text(f'{result.returncode}\n{result.stdout}{result.stderr}')
        written = win98_c.path('C:\\ICONWORD.TXT', drive)
        found = dict(line.split('=', 1) for line in (written.read_text().splitlines() if written else ())
                     if '=' in line)
    finally:
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
            Path(str(drive) + '.clock').unlink(missing_ok=True)
    checks = {f'{name}: {found.get(name)} (expected {wanted})': found.get(name) == wanted
              for name, wanted in EXPECTED.items()}
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'icon word host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
