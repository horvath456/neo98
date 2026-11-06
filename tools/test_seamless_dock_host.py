#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Seamless: minimized windows and a Dock icon per program.

The host runs headless (SDL's dummy driver) with --seamless and
tools/host_seamless_fixture.c, which starts CALC.EXE as a second program.
NEO98_DOCK_HELPERS=silent starts the helper of every program without a Dock
icon, so the channel between host and helper is the real one; the bundles go
to a settings folder of the test.

Minimized windows:
- B minimized by the guest is no window on the screen any more and is listed
  as a minimized Mac window (the one that goes into the Dock) and in the menu
  "Windows"; the click in the Dock (dock-restore=) restores it in the guest;
- Window > Minimize (Cmd+M) minimizes the guest's foreground window
  (Calculator) by SC_MINIMIZE, and the Dock click brings it back.

A Dock icon per program:
- a bundle <settings>/Programme/Calc.app with the helper, an Info.plist
  (name, identifier at.andreas-horvath.neo98.app.<hash>) and an.icns made
  from the icon of the window, and one for the fixture, which has no icon;
- "activate" from the helper brings the program's window to the foreground,
  "quit" closes the program (WM_CLOSE), and when the program has ended the
  helper is told to end and its process is gone.

    build/analysis-venv/bin/python tools/test_seamless_dock_host.py [--no-build]

Results in build/seamless-dock-host/results.json.
"""
from __future__ import annotations
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: the Dock icons of seamless programs exist on macOS only')  # macOS only; skipped elsewhere
        _sys.exit(77)

import json
import os
import plistlib
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's
import test_seamless_host  # noqa: E402


LSREGISTER = ('/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/'
              'Support/lsregister')


def main():
    out = neo98.output_directory('seamless-dock-host')
    for name in ('drive-c', 'settings', 'reports'):
        shutil.rmtree(out / name, ignore_errors=True)
    exe = test_seamless_host.fixture(out)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    drive, settings = out / 'drive-c', out / 'settings'
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
    steps = ['0:wait=|screen 1100x700', '0:idle=500', '0:windows=1',
             # The guest minimizes B; the Dock click restores it.
             '0:key=4e', '0:wait=|B minimized', '0:idle=500', '0:windows=1', '0:menu=describe',
             '0:dock-restore=B', '0:wait=|B restored', '0:idle=500', '0:windows=1',
             # A second program; Cmd+M minimizes its window.
             '0:key=58', '0:wait=|calc started', '0:wait=|Calculator', '1:menu=minimize', '3:windows=1',
             '3:dock-restore=Calculator', '6:windows=1',
             # Its helper says "activate" at 12 s and "quit" at 16 s.
             '20:windows=1', '21:menu=quit']
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
               '--exe', str(exe), '--quiet', '--drive-c', str(drive), '--no-registry-file',
               '--crash-report-dir', str(out / 'reports'), '--seamless']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_HOST_SCREEN='1100x700', NEO98_HOST_DEADLINE='240', NEO98_SEAMLESS_GRACE='1000',
               NEO98_DOCK_HELPERS='silent', NEO98_SETTINGS_DIR=str(settings), NEO98_LOG_DIR=str(out / 'logs'),
               NEO98_DOCK_HELPER_SCRIPT='12:activate@Calc;16:quit@Calc')
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        code = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                              stdin=subprocess.DEVNULL, timeout=300).returncode
    shutil.rmtree(drive, ignore_errors=True)
    text = log_path.read_text(errors='replace')
    lines = [line for line in text.splitlines() if line.startswith('NEO98 HOST')]
    waits = re.findall(r'^NEO98 HOST input wait=(\S.*) (reached|timed out) after', text, re.M)
    lists, current = [], None
    for line in lines:
        if line.startswith('NEO98 HOST seamless items '):
            current = {}
            lists.append(current)
        elif line.startswith('NEO98 HOST seamless item ') and current is not None:
            current[line.rsplit('title=', 1)[1]] = ' minimized ' in line
    # pid, name, icon width, icon height, helper, program, bundle
    programs = {m.group(6).rsplit('\\', 1)[-1]: m for m in re.finditer(
        r'^NEO98 HOST dock program pid=(\d+) name=(.+) icon=(\d+)x(\d+) helper=(\d+) program=(.+) bundle=(.*)$', text, re.M)}
    calc = programs.get('CALC.EXE')
    checks = {
        'host ended': code == 0,
        'every wait reached': bool(waits) and all(w[1] == 'reached' for w in waits),
        'six window lists': len(lists) == 6,
    }
    if len(lists) == 6:
        checks['B is a window on the screen at first'] = lists[0].get('B') is False
        checks['minimized by the guest, B is a minimized Mac window'] = lists[1].get('B') is True
        checks['the Window menu lists it and has Minimize with Cmd+M'] = bool(
            re.search(r'menu-item Window > B \[guest-window\]', text)
            and re.search(r'menu-item Window > Minimize \[minimize\] cmd\+m', text))
        checks['the click in the Dock restores B'] = ('NEO98 HOST seamless restore hwnd=' in text
                                                      and lists[2].get('B') is False)
        checks['Cmd+M minimizes the foreground window of the guest'] = (
            re.search(r'^NEO98 HOST minimize hwnd=[1-9a-f]', text, re.M) is not None
            and lists[3].get('Calculator') is True)
        checks['the click in the Dock restores Calculator'] = lists[4].get('Calculator') is False
        checks['Quit in the Dock closed Calculator'] = 'Calculator' not in lists[5]
    checks['a helper for CALC.EXE and one for the fixture'] = calc is not None and 'HSEAML.EXE' in programs
    if calc:
        pid, bundle, helper = calc.group(1), Path(calc.group(7)), int(calc.group(5))
        info = plistlib.loads((bundle / 'Contents/Info.plist').read_bytes()) if (bundle / 'Contents/Info.plist').exists() else {}
        icon = bundle / 'Contents/Resources/icon.icns'
        sips = subprocess.run(['sips', '-g', 'pixelWidth', str(icon)], capture_output=True, text=True)
        checks['the bundle is <settings>/Programme/Calc.app'] = bundle == settings / 'Programme/Calc.app'
        checks['Info.plist: name, identifier, executable'] = (
            info.get('CFBundleName') == 'Calc' and info.get('CFBundleExecutable') == 'neo98-dock-helper'
            and str(info.get('CFBundleIdentifier', '')).startswith('at.andreas-horvath.neo98.app.')
            and str(info.get('NEO98Program', '')).endswith('CALC.EXE') and 'LSUIElement' not in info)
        checks['the helper is in the bundle'] = os.access(bundle / 'Contents/MacOS/neo98-dock-helper', os.X_OK)
        checks['the icon of the window is 32x32'] = (calc.group(3), calc.group(4)) == ('32', '32')
        checks['icon.icns holds it enlarged (512 wide)'] = (icon.exists() and icon.read_bytes()[:4] == b'icns'
                                                            and 'pixelWidth: 512' in sips.stdout)
        checks['the helper started and said hello'] = helper > 0 and f'dock helper pid={pid} says hello' in text
        activate = next((i for i, line in enumerate(lines) if line == f'NEO98 HOST dock activate pid={pid} windows=1'), None)
        quit_ = next((i for i, line in enumerate(lines) if line == f'NEO98 HOST dock quit pid={pid} windows=1'), None)
        ended = next((i for i, line in enumerate(lines) if line.startswith(f'NEO98 HOST dock program pid={pid} Calc ended')), None)
        checks['activate, quit, ended in this order'] = None not in (activate, quit_, ended) and activate < quit_ < ended
        alive = subprocess.run(['kill', '-0', str(helper)], capture_output=True).returncode == 0 if helper > 0 else True
        checks['the helper process is gone'] = not alive
    fixture_program = programs.get('HSEAML.EXE')
    if fixture_program:
        helper = int(fixture_program.group(5))
        alive = subprocess.run(['kill', '-0', str(helper)], capture_output=True).returncode == 0 if helper > 0 else True
        checks['a program without an icon still has its bundle'] = (
            fixture_program.group(3) == '0' and (Path(fixture_program.group(7)) / 'Contents/Info.plist').exists())
        checks['the helpers end with the host'] = not alive
    # One report per choice, and the fuse. The helper of the fixture
    # gets what a click on its Dock icon brings, the activation and the reopen
    # event, in both orders: each pair is one line for the host. Then
    # activations in a row, as a loop between the helpers and the host would
    # make them: after five reports within eight seconds the helper says
    # "storm" and reports nothing more.
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
    env['NEO98_HOST_INPUT'] = ';'.join(['0:wait=|screen 1100x700', '0:idle=500', '14:windows=1', '15:menu=quit'])
    env['NEO98_DOCK_HELPER_SCRIPT'] = ('2:activate;2.3:reopen;4:reopen;4.2:activate;5.2:activate;6.4:activate;'
                                       '7.6:activate;8.8:activate;10:activate;11.2:reopen')
    log_path = out / 'host-choices.log'
    with log_path.open('w') as log:
        code = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                              stdin=subprocess.DEVNULL, timeout=300).returncode
    shutil.rmtree(drive, ignore_errors=True)
    text = log_path.read_text(errors='replace')
    said = re.findall(r'^NEO98 HOST dock (activate|reopen|helper) pid=\d+ (?:windows=\d+|says (\w+))$', text, re.M)
    checks['choices: run ended'] = code == 0
    checks['choices: a pair is one line, five reports, then the fuse'] = said == [
        ('helper', 'hello'), ('activate', ''), ('reopen', ''), ('activate', ''), ('activate', ''), ('activate', ''),
        ('helper', 'storm')]
    # The taskbar of the guest, shown in the seamless view: the button of a
    # minimized window brings it back.
    if '--no-explorer' not in sys.argv:
        subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
        steps = ['0:wait=Shell_TrayWnd', '0:idle=3000', f'0:open={drive / "WINDOWS/CALC.EXE"}', '0:wait=|Calculator',
                 '2:windows=1', '3:menu=minimize', '5:windows=1', f'5:shot={out / "taskbar.ppm"}',
                 # The button "Calculator" on the taskbar (after "Welcome to Windows 98").
                 '6:pointer=390,687', '6:press=390,687', '6.2:release=390,687', '8:windows=1', '9:menu=quit']
        command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
                   '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--quiet', '--drive-c', str(drive), '--no-registry-file',
                   '--crash-report-dir', str(out / 'reports'), '--seamless', '--seamless-taskbar']
        env['NEO98_HOST_INPUT'] = ';'.join(steps)
        env['NEO98_DOCK_HELPERS'] = 'bundles'
        env.pop('NEO98_DOCK_HELPER_SCRIPT', None)
        env.pop('NEO98_SEAMLESS_GRACE', None)
        log_path = out / 'host-taskbar.log'
        with log_path.open('w') as log:
            code = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                  stdin=subprocess.DEVNULL, timeout=600).returncode
        shutil.rmtree(drive, ignore_errors=True)
        text = log_path.read_text(errors='replace')
        lists, current = [], None
        for line in text.splitlines():
            if line.startswith('NEO98 HOST seamless items '):
                current = {}
                lists.append(current)
            elif line.startswith('NEO98 HOST seamless item ') and current is not None:
                current[line.rsplit('title=', 1)[1]] = ' minimized ' in line
        checks['taskbar: Explorer run ended'] = code == 0
        checks['taskbar: Calculator on the screen, then minimized by Cmd+M'] = (
            len(lists) == 3 and lists[0].get('Calculator') is False and lists[1].get('Calculator') is True)
        checks['taskbar: a press on its button brings it back'] = (
            len(lists) == 3 and lists[2].get('Calculator') is False
            and 'NEO98 HOST seamless press 390,687 reaches the guest' in text)
        checks['taskbar: bundles without a helper process under "bundles"'] = bool(
            re.search(r'^NEO98 HOST dock program pid=\d+ name=Calc icon=32x32 helper=0 ', text, re.M))
    # macOS registers an application bundle it finds with LaunchServices by
    # itself, some time after it was written; the bundles of a test do not
    # stay there, nor on the disk, where it would find them again.
    bundles = sorted((settings / 'Programme').glob('*.app'))
    for bundle in bundles:
        shutil.rmtree(bundle, ignore_errors=True)
    # The registration may arrive after the bundle is gone; -u works on a
    # path that no longer exists.
    time.sleep(5)
    for bundle in bundles:
        subprocess.run([LSREGISTER, '-u', str(bundle)], capture_output=True)
    dump = subprocess.run([LSREGISTER, '-dump'], capture_output=True, text=True, errors='replace').stdout
    checks['no bundle of the test stays with LaunchServices'] = str(settings / 'Programme') not in dump
    (out / 'results.json').write_text(json.dumps(checks, indent=1))
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'SEAMLESS DOCK FAIL; see {log_path}')
        return 1
    print('SEAMLESS DOCK PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
