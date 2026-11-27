#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""SPI_SETNONCLIENTMETRICS gives the windows that are open their new frame.

After USER has taken the new sizes it gives every window of the system its
frame anew, the children after their parent, through SetWindowPos with
SWP_FRAMECHANGED: a window with a sizing border grows by the change of the
border on every side, and a window gets taller by the change of the caption
if it has one and by that of the menu bar if it has one, so its client area
keeps its size. Then the whole screen is painted again. The port only kept
the sizes: a window that was open stayed as it was laid out, with the old
caption and the old client area, and only a window made later had the new
frame.

tools/frame_metrics_fixture.c in sessions of neo98-kms over one private copy
of drive C::

  frames  with the sizing border 3 units wider, the caption 10 and the menu
          bar 6 pixels higher: the main window (sizing border, caption, menu
          bar) is 3 pixels wider on every side and 3 + 10 + 6 at the bottom,
          its child (sizing border, caption) 3 and 3 + 10, the popup with a
          fixed frame only 10 at the bottom; no client area changes its
          size; each window got WM_WINDOWPOSCHANGING, WM_NCCALCSIZE and
          WM_WINDOWPOSCHANGED with SWP_FRAMECHANGED once, was asked for its
          small icon again and got no WM_SIZE; without SPIF_SENDCHANGE
          there is no WM_SETTINGCHANGE. With the old sizes set again every
          rectangle is the old one.
          The box of the start of a drag (SM_CXDRAG, SM_CYDRAG) is 2 by 2
          pixels without DragWidth and DragHeight in the registry.
  minset  SPI_SETMINIMIZEDMETRICS with SPIF_UPDATEINIFILE
  minget  a new session starts with these minimized metrics: USER reads
          MinWidth, MinHorzGap, MinVertGap and MinArrange of
          HKCU\\Control Panel\\Desktop\\WindowMetrics at its start

    build/analysis-venv/bin/python tools/test_frame_metrics_host.py [--no-build] [--keep] [--kms KMS]
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
WORK = ROOT / 'build/frame-metrics'
MESSAGES = {'WM_WINDOWPOSCHANGING': 1, 'WM_NCCALCSIZE': 1, 'WM_WINDOWPOSCHANGED': 1, 'SWP_FRAMECHANGED': 1,
            'WM_SIZE': 0, 'WM_SETICON 2': 1, 'WM_SETTINGCHANGE': 0, 'client wider': 0, 'client higher': 0,
            'back': 1}
# left, top, right, bottom: how far each side moved.
SIDES = {'main': (-3, -3, 3, 3 + 10 + 6), 'child': (-3, -3, 3, 3 + 10), 'popup': (0, 0, 0, 10)}
FRAMES = {'drag box': 202, 'set': 1, 'caption': 10, 'menu bar': 6, 'sizing border': 3, 'sizing border down': 3}
for _window, _sides in SIDES.items():
    FRAMES.update({f'{_window} {name}': value for name, value in zip(('left', 'top', 'right', 'bottom'), _sides)})
    FRAMES.update({f'{_window} {name}': value for name, value in MESSAGES.items()})
# ARW_TOPRIGHT | ARW_DOWN is 7.
MINIMIZED = {'get': 1, 'width': 200, 'horizontal gap': 7, 'vertical gap': 9, 'arrange': 7, 'minimized window': 200,
             'cell wider': 7, 'cell higher': 9}


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
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/frame_metrics_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lgdi32', '-lkernel32',
                    '-o', str(drive / 'FRAMES.EXE')], check=True)
    logs = []

    def session(command):
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:800x600')
        for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD', 'NEO98_HOST_INPUT'):
            env.pop(variable, None)
        written = win98_c.path('C:\\FRAMES.TXT', drive)
        if written:
            written.unlink()
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '800x600',
                                 '--exe', 'C:\\FRAMES.EXE', '--command', ('FRAMES.EXE ' + command).strip(),
                                 '--time-limit', '60'], cwd=ROOT, env=env, capture_output=True, text=True,
                                timeout=120)
        logs.append(f'---- {command}: {result.returncode}\n{result.stdout}{result.stderr}')
        written = win98_c.path('C:\\FRAMES.TXT', drive)
        found = {}
        for text in (written.read_text().splitlines() if written else ()):
            name, _, number = text.partition('=')
            try:
                found[name] = int(number)
            except ValueError:
                pass
        return found

    try:
        runs = {name: session(name) for name in ('', 'minset', 'minget')}
    finally:
        (WORK / 'log.txt').write_text('\n'.join(logs))
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
            Path(str(drive) + '.clock').unlink(missing_ok=True)

    def differing(found, wanted):
        return {name: (found.get(name), value) for name, value in wanted.items() if found.get(name) != value}

    checks = {}
    frames = runs['']
    for group in ('drag box', 'set', 'caption', 'menu bar', 'sizing border', 'sizing border down'):
        checks[f'{group}: {frames.get(group)} (expected {FRAMES[group]})'] = frames.get(group) == FRAMES[group]
    for window in SIDES:
        wrong = differing(frames, {name: value for name, value in FRAMES.items() if name.startswith(window + ' ')})
        checks[f'the {window} window has its new frame, its client area and the messages: {wrong}'] = not wrong
    checks[f'minimized metrics, set: {differing(runs["minset"], dict(MINIMIZED, set=1))}'] = \
        not differing(runs['minset'], dict(MINIMIZED, set=1))
    checks[f'minimized metrics in a new session: {differing(runs["minget"], MINIMIZED)}'] = \
        not differing(runs['minget'], MINIMIZED)
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'frame metrics host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
