#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""What the Control Panel sets is applied and is still there after a restart.

The Keyboard, Mouse, Sounds and Display pages set plain numbers of USER with
SystemParametersInfo: the typematic speed and delay, the double-click time
and box, the mouse thresholds, the beep, the drag box, the side menus drop
to, the lines of the wheel and the delay of a submenu. With
SPIF_UPDATEINIFILE USER stores each as a string under HKEY_CURRENT_USER
(Control Panel\\Keyboard, \\Mouse, \\Desktop, \\Sound) and reads it there at its
next start; the caret time and the colours are stored by the applets and
read by USER (Control Panel\\Desktop "CursorBlinkRate", Control Panel\\Colors).

tools/control_settings_fixture.c in three sessions of neo98-kms over one
private copy of drive C:, with the picture of the desktop in memory:

  set    every set action answers TRUE, the first one broadcasts
         WM_SETTINGCHANGE with its action, the values read back at once in
         the same process and in a second process (the caret time is one for
         all processes)
  get    a new session starts with every one of them
  keys   a held key repeats after the set delay at the set speed: twice, with
         a slow and a fast setting, the times of its WM_KEYDOWN measured in
         the guest
  menu   a popup menu is painted in COLOR_MENU, its selected item in
         COLOR_HIGHLIGHT, as SetSysColors has just set them
  pattern SPI_SETDESKPATTERN with SPIF_UPDATEINIFILE keeps the pattern in
         HKCU\\Control Panel\\Desktop, where Display Properties reads it
         back, and in [Desktop] of WIN.INI; a new session finds both

    build/analysis-venv/bin/python tools/test_control_settings_host.py [--no-build] [--keep] [--kms KMS]
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
WORK = ROOT / 'build/control-settings'

# What "set" sets, as "get" names it.
EXPECTED = {
    'keyboard speed result': 1, 'keyboard speed': 20, 'keyboard delay result': 1, 'keyboard delay': 2,
    'double click time': 777, 'caret blink time': 333,
    'mouse result': 1, 'mouse threshold 1': 4, 'mouse threshold 2': 12, 'mouse speed': 2,
    'beep result': 1, 'beep': 0,
    'double click width': 9, 'double click height': 7, 'drag width': 6, 'drag height': 5,
    'menu drop alignment': 1, 'wheel lines result': 1, 'wheel lines': 5,
    'menu show delay result': 1, 'menu show delay': 250, 'swapped': 1,
    # 90 pixels; 20 is below the 32 of an icon, which is the least.
    'icon spacing': 90, 'icon vertical spacing': 32,
    'animation result': 1, 'animation': 1, 'menu animation result': 1, 'menu animation': 1,
    'combo animation result': 1, 'combo animation': 0,
    'colour background': 10 | 20 << 8 | 30 << 16, 'colour window': 250 | 240 << 8 | 230 << 16,
    'colour button face': 100 | 110 << 8 | 120 << 16,
}
SETTERS = ('keyboard speed', 'keyboard delay', 'double click time', 'mouse', 'beep', 'double click width',
           'double click height', 'drag width', 'drag height', 'menu drop alignment', 'wheel lines',
           'menu show delay', 'mouse button swap', 'icon spacing', 'icon vertical spacing', 'animation',
           'menu animation', 'combo animation', 'caret blink time', 'colours')


def values(path):
    """The lines name=number of a result file; a name that repeats is a list."""
    found = {}
    if not path or not path.is_file():
        return found
    for line in path.read_text(errors='replace').splitlines():
        name, _, number = line.partition('=')
        try:
            number = int(number)
        except ValueError:
            continue
        if name == 'after':
            found.setdefault(name, []).append(number)
        else:
            found[name] = number
    return found


def typematic(speed, delay):
    """The delay and the period in milliseconds of a typematic setting."""
    rate = 31 - speed
    return (delay + 1) * 250, (8 + (rate & 7)) * (1 << ((rate >> 3) & 3)) * 4.17


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
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/control_settings_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32', '-ladvapi32',
                    '-o', str(drive / 'CTLSET.EXE')], check=True)
    logs = []

    def session(command, steps=()):
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
                   NEO98_HOST_INPUT=';'.join(steps))
        for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
            env.pop(variable, None)
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768',
                                 '--exe', 'C:\\CTLSET.EXE', '--command', 'CTLSET.EXE ' + command,
                                 '--time-limit', '60'], cwd=ROOT, env=env, capture_output=True, text=True,
                                timeout=120)
        logs.append(f'---- {command}: {result.returncode}\n{result.stdout}{result.stderr}')
        return result.returncode

    def held(speed, delay, seconds):
        """One key held for `seconds`: the delay to the first repeat and the periods after it."""
        key = win98_c.path('C:\\KEYS.TXT', drive)
        if key:
            key.unlink()
        session(f'keys {speed} {delay}', ['0:wait=ControlSettings', '0:idle=300', '0:keydown=41',
                                          f'{seconds}:keyup=41'])
        return values(win98_c.path('C:\\KEYS.TXT', drive))

    try:
        codes = [session('set'), session('get')]
        result = {name: values(win98_c.path(f'C:\\{name}.TXT', drive)) for name in ('SET', 'CHILD', 'GET')}
        slow, fast = held(12, 3, 2.6), held(31, 0, 1.5)
        # ALT+M opens the menu with its first item selected. The script waits
        # for a pixel of that item and for one of the item below it, and
        # says so when one does not come; Q ends the program.
        at = len(logs)
        session('menu', ['0:wait=ControlSettings', '0:idle=500', '0:alt=4d', '0:wait=60,52,c80a1e',
                         '0:wait=60,90,0ac81e', '0.2:key=51'])
        menu_log = logs[at]
        # Display Properties, Background: the pattern, set and read back in
        # a new session.
        session('putpattern')
        put_pattern = win98_c.path('C:\\PATTERN.TXT', drive).read_text(errors='replace')
        session('pattern')
        got_pattern = win98_c.path('C:\\PATTERN.TXT', drive).read_text(errors='replace')
    finally:
        (WORK / 'log.txt').write_text('\n'.join(logs))
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
            Path(str(drive) + '.clock').unlink(missing_ok=True)
    stops = [line for line in '\n'.join(logs).splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line]

    def differing(found):
        return {name: (found.get(name), wanted) for name, wanted in EXPECTED.items() if found.get(name) != wanted}

    refused = [name for name in SETTERS if result['SET'].get('set ' + name) != 1]
    pattern = 'registry pattern=170 85 170 85 170 85 170 85'
    checks = {
        f'the sessions ran without a stop: {codes} {stops[:1]}': codes == [0, 0] and not stops,
        'SPI_SETDESKPATTERN keeps the pattern in HKCU\\Control Panel\\Desktop and in WIN.INI, '
        f'also in a new session: {got_pattern.split()}': 'set pattern=1' in put_pattern and pattern in got_pattern
            and 'profile pattern=170 85 170 85 170 85 170 85' in got_pattern,
        f'every set action answers TRUE: {refused}': bool(result['SET']) and not refused,
        'SPI_SETKEYBOARDSPEED with SPIF_SENDCHANGE broadcasts WM_SETTINGCHANGE with its action: '
        f'{result["SET"].get("changes after keyboard speed")} {result["SET"].get("change was")}':
            result['SET'].get('changes after keyboard speed') == 1 and result['SET'].get('change was') == 11,
        f'the values read back at once: {differing(result["SET"])}': not differing(result['SET']),
        f'a second process has them, the caret time among them: {differing(result["CHILD"])}':
            not differing(result['CHILD']),
        f'a new session starts with them: {differing(result["GET"])}': not differing(result['GET']),
    }
    for name, (speed, delay), found in (('slow', (12, 3), slow), ('fast', (31, 0), fast)):
        wait, period = typematic(speed, delay)
        after = found.get('after', [])
        first, rest = (after[0] if after else None), after[1:]
        mean = sum(rest) / len(rest) if rest else 0
        # The machine may be late, never early: the first repeat comes after
        # the delay, and the repeats follow at the period on average.
        checks[f'{name}: the first repeat of a held key comes after the delay of {wait} ms: {first}'] = (
            first is not None and wait - 30 <= first <= wait + 250)
        checks[f'{name}: {len(rest)} more repeats, {mean:.1f} ms apart for a period of {period:.1f} ms'] = (
            len(rest) >= 5 and period * 0.85 <= mean <= period * 1.35)
        checks[f'{name}: every WM_KEYDOWN after the first is marked as a repeat: '
               f'{found.get("repeats")} of {found.get("key downs")}'] = (
            found.get('key downs', 0) > 1 and found.get('repeats') == found.get('key downs') - 1)
    late = [line for line in menu_log.splitlines() if 'timed out after' in line]
    checks[f'a popup menu is painted in the colours SetSysColors set for it: {late}'] = (
        not late and menu_log.count('NEO98 KMS input wait=') == 3)
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'control settings host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
