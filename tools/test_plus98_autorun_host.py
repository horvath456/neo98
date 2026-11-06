#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The setup wizard of Plus! 98 stays in front of its autorun window.

AUTORUN.EXE of the Plus! 98 CD sets the focus on the button under the pointer.
Its window is larger than the wizard that "Install Plus!" starts, and with
every move of the pointer over a button it came in front of the wizard; in the
seamless view the wizard, wholly covered, then had no Mac window at all. A
second click on "Install Plus!" starts setup.exe again, which finds the
running wizard, brings it to the front and ends.

Explorer is the shell, from a clone of the pristine drive C:, under SDL's
dummy video driver, once seamless (1280x800) and once on a desktop of 800x600:

- the autorun window, a click on "Install Plus!", the wizard is in front;
- the pointer moves over both buttons of the autorun window, on the part the
  wizard leaves visible: the wizard stays in front;
- a second click on "Install Plus!" there: the wizard is in front again
  afterwards, and stays there when the pointer moves on;
- the autorun window is dragged over the wizard by its title bar: the wizard
  is wholly covered, the Window menu lists it and its program keeps its
  icon in the Dock (seamless), and choosing it brings it back in front.

Skipped when ~/Downloads/PLUS98.ISO is not there (NEO98_PLUS98_ISO names
another file). The ISO is unpacked into the output folder, never mounted.

    build/analysis-venv/bin/python tools/test_plus98_autorun_host.py [--no-build] [--only seamless|desktop]
"""
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

ISO = Path(os.environ.get('NEO98_PLUS98_ISO', str(Path.home() / 'Downloads/PLUS98.ISO')))
AUTORUN, WIZARD = 'Plus! 98 CD-ROM', 'Microsoft Plus! 98 Setup'
SIZE = {'seamless': (1280, 800), 'desktop': (800, 600)}
# Where AUTORUN.EXE puts its window of 606 by 429 on the two desktops; the
# wizard (486 by 358) is centred and leaves 60 pixels of it visible on the left.
ORIGIN = {'seamless': (337, 110), 'desktop': (97, 43)}
DRAG = {'seamless': (60, 45), 'desktop': (0, 22)}


def numbered(steps):
    result, at = [], 0
    for step in steps:
        if step.startswith('pause='):
            at += int(step[6:])
            continue
        result.append(f'{at}:{step}')
        at += 1
    return result


def run(binary, out, name, cd):
    x, y = ORIGIN[name]
    dx, dy = DRAG[name]
    install, browse = (x + 13, y + 297), (x + 13, y + 375)
    # The Welcome window refreshes on a timer; three seconds of global idle
    # are not a readiness condition for Explorer. The autorun and wizard
    # waits below still require the actual target windows.
    steps = ['wait=Shell_TrayWnd', 'idle=200', f'open={cd}/autorun.exe', f'wait=|{AUTORUN}', 'pause=2',
             f'click={x + 260},{y + 297}', f'wait=|{WIZARD}', 'pause=5', 'windows=1',
             f'move={install[0] - 4},{install[1]}', f'move={install[0]},{install[1] + 1}', 'pause=1',
             f'move={browse[0]},{browse[1]}', f'move={browse[0] + 2},{browse[1] + 1}', 'pause=1', 'windows=1',
             f'click={install[0]},{install[1]}', 'pause=8', 'windows=1',
             f'move={install[0] + 2},{install[1] + 2}', f'move={browse[0]},{browse[1]}',
             f'move={install[0] + 1},{install[1]}', 'pause=2', 'windows=1',
             f'down={x + 160},{y + 10}', f'move={x + 160 + dx // 2},{y + 10 + dy // 2}',
             f'move={x + 160 + dx},{y + 10 + dy}', f'up={x + 160 + dx},{y + 10 + dy}', 'pause=2', 'windows=1',
             f'choose={WIZARD}', 'pause=2', 'windows=1', 'menu=quit']
    drive = out / f'drive-c-{name}'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
    settings = out / f'settings-{name}'
    shutil.rmtree(settings, ignore_errors=True)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
               '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--quiet', '--drive-c', str(drive), '--no-registry-file',
               '--cdrom', str(cd), '--crash-report-dir', str(out / f'reports-{name}')]
    if name == 'seamless':
        command.append('--seamless')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(numbered(steps)),
               NEO98_HOST_SCREEN='%dx%d' % SIZE[name], NEO98_HOST_DEADLINE='500', NEO98_DOCK_HELPERS='bundles',
               NEO98_SETTINGS_DIR=str(settings), NEO98_LOG_DIR=str(out / f'logs-{name}'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    log_path = out / f'{name}.log'
    with log_path.open('w') as log:
        try:
            code = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                  stdin=subprocess.DEVNULL, timeout=560).returncode
        except subprocess.TimeoutExpired:
            code = 'timeout'
    reports = sorted(p.name for p in (out / f'reports-{name}').iterdir()) if (out / f'reports-{name}').exists() else []
    for folder in (drive, settings, out / f'logs-{name}', out / f'reports-{name}'):
        shutil.rmtree(folder, ignore_errors=True)
    return code, log_path.read_text(errors='replace'), reports


def lists(text):
    """What each windows= step printed: the items front to back, the covered windows, the menu."""
    result = []
    for line in text.splitlines():
        if line.startswith('NEO98 HOST seamless items '):
            result.append({'items': [], 'covered': [], 'menu': []})
        elif not result:
            continue
        elif m := re.match(r'NEO98 HOST seamless item id=\w+ (?:minimized )?frame=\S+ .* title=(.*)$', line):
            result[-1]['items'].append(m.group(1))
        elif m := re.match(r'NEO98 HOST seamless covered id=\w+ title=(.*)$', line):
            result[-1]['covered'].append(m.group(1))
        elif m := re.match(r'NEO98 HOST menu window id=\w+ title=(.*)$', line):
            result[-1]['menu'].append(m.group(1))
    return result


def scenario(binary, out, name, cd):
    code, text, reports = run(binary, out, name, cd)
    seen = lists(text)
    seen += [{'items': [], 'covered': [], 'menu': []}] * (6 - len(seen))
    waits = re.findall(r'^NEO98 HOST input wait=(\S.*) (reached|timed out) after', text, re.M)

    def front(i):
        own = [t for t in seen[i]['items'] if t in (AUTORUN, WIZARD)]
        return own[:1] == [WIZARD]

    checks = {
        'host ended by its menu': code == 0,
        'every wait reached': bool(waits) and all(state == 'reached' for _, state in waits)
                              and 'timed out after' not in text,
        'no crash report': not reports,
        'the wizard is in front after "Install Plus!"': front(0),
        'it stays in front while the pointer moves over the buttons of the autorun window': front(1),
        'it is in front after a second click on "Install Plus!"': front(2),
        'and stays there while the pointer moves on': front(3),
        'the autorun window dragged over it covers it wholly': WIZARD in seen[4]['covered'] and
                                                                 WIZARD not in seen[4]['items'],
        'choosing the wizard brings it back in front': front(5) and WIZARD not in seen[5]['covered'],
    }
    if name == 'seamless':
        checks['the Window menu lists the covered wizard'] = WIZARD in seen[4]['menu']
        after = text.split('NEO98 HOST input down')[-1].split('NEO98 HOST seamless choose')[0]
        checks['its program keeps its icon in the Dock'] = \
            re.search(r'^NEO98 HOST dock program pid=\d+ name=setup ', text, re.M) is not None and \
            re.search(r'^NEO98 HOST dock program pid=\d+ setup ended', after, re.M) is None
    return checks


def main():
    if not ISO.exists():
        print(f'PLUS98 AUTORUN SKIP: {ISO} is not there')
        raise SystemExit(77)
    out = neo98.output_directory('plus98-autorun-host')
    cd = out / 'cd'
    if not (cd / 'autorun.exe').exists():
        cd.mkdir(exist_ok=True)
        subprocess.run(['bsdtar', '-xf', str(ISO), '-C', str(cd)], check=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    only = sys.argv[sys.argv.index('--only') + 1] if '--only' in sys.argv else None
    results, okay = {}, True
    for name in ('seamless', 'desktop'):
        if only and only != name:
            continue
        results[name] = scenario(binary, out, name, cd)
        for check, passed in results[name].items():
            print(f"{'ok  ' if passed else 'FAIL'} {name}: {check}")
            okay = okay and passed
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    if okay and not os.environ.get('NEO98_PLUS98_KEEP'):
        subprocess.run(['chmod', '-R', 'u+w', str(cd)], check=False)
        shutil.rmtree(cd, ignore_errors=True)
    if not okay:
        print(f'PLUS98 AUTORUN FAIL; see {out}')
        return 1
    print('PLUS98 AUTORUN PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
