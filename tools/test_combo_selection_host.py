#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The edit field of a combo box shows no selection without the focus.

USER makes the edit field of a combo box with ES_NOHIDESEL, so the edit
keeps drawing its selection when it loses the focus; the combo box takes it
away itself: on EN_KILLFOCUS its kill-focus helper sets the selection to 0,0,
closes the list and sends CBN_KILLFOCUS. Neo98 only sent CBN_KILLFOCUS, and
the text the focus had selected stayed highlighted.

tools/combo_selection_fixture.c gives a drop-down combo box the focus (its
text is selected) and a second later a button.

    build/analysis-venv/bin/python tools/test_combo_selection_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)

import json
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

# The edit field of the combo box, inside its border and left of its button.
FIELD = (13, 13, 190, 30)


def highlighted(path):
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    width = int(size.split()[0])
    left, top, right, bottom = FIELD
    return sum(1 for y in range(top, bottom) for x in range(left, right)
               if body[(y * width + x) * 3:(y * width + x) * 3 + 3] == b'\x00\x00\x80')


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('combo-selection')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    (drive / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/combo_selection_fixture.c'), '-O1', '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32', '-lgdi32',
                    '-o', str(drive / 'COMBOSEL.EXE')], env=neo98.tool_environment(), check=True)
    steps = ['0:wait=ComboSel', '0.3:move=600,450', f'0.6:shot={out}/focused.ppm', f'3:shot={out}/left.ppm',
             '4:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='30',
               NEO98_HOST_INPUT=';'.join(steps), NEO98_SETTINGS_DIR=str(out / 'settings'),
               NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\COMBOSEL.EXE', '--quiet',
                        '--desktop', '640x480', '--system-tree', str(drive), '--drive-c', str(drive),
                        '--registry', str(out / 'c.registry'), '--crash-report-dir', str(out / 'reports')],
                       cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=120)
    written = win98_c.path('C:\\COMBOSEL.TXT', drive)
    report = written.read_text().split() if written else []
    shutil.rmtree(drive, ignore_errors=True)
    complete = len(report) == 6 and (out / 'focused.ppm').is_file() and (out / 'left.ppm').is_file()
    style = int(report[2], 16) if complete else 0
    selection = (int(report[4]), int(report[5])) if complete else None
    before = highlighted(out / 'focused.ppm') if complete else 0
    after = highlighted(out / 'left.ppm') if complete else -1
    checks = {
        'the program wrote what it read and the host took both pictures': complete,
        f'the combo\'s edit field has ES_NOHIDESEL, as USER makes it: {style:08x}': bool(style & 0x100),
        f'with the focus its text is selected and highlighted: {before} pixels': before > 200,
        f'without the focus the selection is 0,0: {selection}': selection == (0, 0),
        f'and nothing of the field is highlighted: {after} pixels': after == 0,
    }
    (out / 'results.json').write_text(json.dumps({'report': report, 'checks': checks}, indent=1))
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    pictures and log in', out)
    print(f'combo selection host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
