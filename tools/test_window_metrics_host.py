#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The sizes of the parts of a window are those of Windows 98.

USER takes them at its start from HKCU\\Control Panel\\Desktop\\WindowMetrics,
where Setup of Windows 98 writes ScrollWidth -240 (twips, 16 pixels),
CaptionWidth -270 (18), SmCaptionWidth -195 (13), SmCaptionHeight -225 (15)
and MenuHeight -270 (18). The port read none of them and gave every program
scroll bars of 17, a menu bar of 19 and small captions of 16 by 16 pixels.
tools/window_metrics_fixture.c reads NONCLIENTMETRICS and GetSystemMetrics
over five starts on a copy of setup's drive C::

  setup     scroll bars 16 (SM_CXVSCROLL and iScrollWidth alike), captions
            18 (SM_CYCAPTION 19), small captions 13 by 15 (SM_CYSMCAPTION
            16), menu bar 18 (SM_CYMENU 19)
  registry  ScrollWidth -300 in the registry is a scroll bar of 20 at the
            next start
  set       a menu bar of 22 that SPI_SETNONCLIENTMETRICS stores with
            SPIF_UPDATEINIFILE is there at the next start

    build/analysis-venv/bin/python tools/test_window_metrics_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
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

FIELDS = ('ok', 'border', 'scroll width', 'scroll height', 'caption width', 'caption height',
          'small caption width', 'small caption height', 'menu width', 'menu height', 'SM_CXVSCROLL',
          'SM_CYHSCROLL', 'SM_CYCAPTION', 'SM_CYMENU', 'SM_CXSMSIZE', 'SM_CYSMCAPTION')


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('window-metrics')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), out / 'c.registry')
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/window_metrics_fixture.c'), '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32', '-ladvapi32',
                    '-o', str(drive / 'METRICS.EXE')], check=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='40',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    answers = {}
    try:
        for step in ('setup', 'registry', 'after-registry', 'set', 'after-set'):
            mode = step if step in ('registry', 'set') else 'read'
            result = win98_c.path('C:\\METRICS.TXT', drive)
            if result:
                result.unlink()
            with (out / f'{step}.txt').open('wb') as log:
                subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\METRICS.EXE',
                                '--command', f'C:\\METRICS.EXE {mode}', '--quiet', '--system-tree', str(drive),
                                '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                                '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                               stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=120)
            result = win98_c.path('C:\\METRICS.TXT', drive)
            numbers = [int(v) for v in result.read_text().split()] if result else []
            answers[step] = dict(zip(FIELDS, numbers))
    finally:
        shutil.rmtree(drive, ignore_errors=True)
    setup, registry, after_set = answers['setup'], answers['after-registry'], answers['after-set']
    want = {'ok': 1, 'border': 1, 'scroll width': 16, 'scroll height': 16, 'caption width': 18,
            'caption height': 18, 'small caption width': 13, 'small caption height': 15, 'menu width': 18,
            'menu height': 18, 'SM_CXVSCROLL': 16, 'SM_CYHSCROLL': 16, 'SM_CYCAPTION': 19, 'SM_CYMENU': 19,
            'SM_CXSMSIZE': 13, 'SM_CYSMCAPTION': 16}
    wrong = {name: (setup.get(name), value) for name, value in want.items() if setup.get(name) != value}
    checks = {
        f'setup: the sizes of Windows 98 (found, wanted: {wrong})': not wrong,
        'setup: SM_CXVSCROLL is iScrollWidth':
            setup.get('SM_CXVSCROLL') is not None and setup.get('SM_CXVSCROLL') == setup.get('scroll width'),
        f'registry: ScrollWidth -300 is a scroll bar of 20 at the next start: '
        f'{registry.get("scroll width")} {registry.get("SM_CXVSCROLL")}':
            registry.get('scroll width') == 20 and registry.get('SM_CXVSCROLL') == 20,
        f'set: the menu bar of 22 stays for the next start: {after_set.get("menu height")} '
        f'{after_set.get("SM_CYMENU")}, scroll bar {after_set.get("scroll width")}':
            after_set.get('menu height') == 22 and after_set.get('SM_CYMENU') == 23
            and after_set.get('scroll width') == 20,
    }
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    print(f'window metrics host: {sum(map(bool, checks.values()))} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
