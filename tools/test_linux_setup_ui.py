#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the packaged setup's real folder and image choosers with X11 input."""
import argparse
import ctypes as C
import json
import os
import platform
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from linux_x11_test import X11, I, P, U
from native_app import ROOT
import win98_c


class KeyboardState(C.Structure):
    _fields_ = [('group', C.c_ubyte), ('locked_group', C.c_ubyte),
                ('base_group', C.c_ushort), ('latched_group', C.c_ushort),
                *[(name, C.c_ubyte) for name in ('mods', 'base_mods', 'latched_mods',
                  'locked_mods', 'compat_state', 'grab_mods', 'compat_grab_mods',
                  'lookup_mods', 'compat_lookup_mods')], ('ptr_buttons', C.c_ushort)]


def wait(check, seconds=12):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        result = check()
        if result:
            return result
        time.sleep(.08)
    raise AssertionError('Setup UI did not respond')


def type_path(x, path):
    names = {'/': 'slash', '-': 'minus', '.': 'period', '_': 'underscore', ' ': 'space'}
    x.x.XKeycodeToKeysym.argtypes = [P, C.c_ubyte, I]
    x.x.XKeycodeToKeysym.restype = U
    x.x.XkbGetState.argtypes = [P, C.c_uint, C.POINTER(KeyboardState)]
    state = KeyboardState()
    assert x.x.XkbGetState(x.display, 0x100, C.byref(state)) == 0
    for letter in str(path):
        name = names.get(letter, letter)
        symbol = x.x.XStringToKeysym(name.encode())
        code = x.x.XKeysymToKeycode(x.display, symbol)
        shifted = x.x.XKeycodeToKeysym(x.display, code, 0) != symbol
        if shifted:
            assert x.x.XKeycodeToKeysym(x.display, code, 1) == symbol, name
        if letter.isalpha() and state.locked_mods & 2:
            shifted = not shifted
        x.chord(*(['Shift_L', name] if shifted else [name]))
        time.sleep(.025)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path,
                        help='AppRun of an existing package (default: build the current one)')
    args = parser.parse_args()
    if args.runner is None and not (sys.platform.startswith('linux') and platform.machine() == 'x86_64'):
        print('SKIP: the Linux package is built only on Linux x86_64')
        return 77
    root = win98_c.use_pristine()
    out = ROOT / 'build/native-linux/setup-ui'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'results.json').unlink(missing_ok=True)
    x = X11(); x.require_input()
    # Build only once the test can run: the build is long, and other
    # tests may be building the same programs at the same time.
    if args.runner is None:
        import linux_app
        linux_app.package()
        args.runner = ROOT / 'build/Neo98.AppDir/AppRun'
    reports = {}

    with tempfile.TemporaryDirectory(dir=out) as temporary:
        config = Path(temporary) / 'config'
        env = dict(os.environ, NEO98_SETTINGS_DIR=str(config), NEO98_LOG_TO_FILE='0',
                   NEO98_APP_MODE='1', SDL_AUDIODRIVER='dummy', NEO98_CLIPBOARD='private',
                   GDK_SCALE='1', GDK_DPI_SCALE='1', NEO98_HOST_INPUT='3:menu=quit')
        for name in ('NEO98_WIN98_ROOT', 'NEO98_LINUX_SETUP_SOURCE', 'NEO98_LINUX_SETUP_TARGET',
                     'NEO98_SETTINGS_FILE', 'NEO98_SETUP_SCRIPT', 'SDL_VIDEODRIVER'):
            env.pop(name, None)
        with (out / 'session.log').open('w') as log:
            process = subprocess.Popen([str(args.runner.resolve()), '--setup-linux', '--exe',
                r'C:\WINDOWS\NOTEPAD.EXE', '--ram-drive', '--no-registry-file', '--time-limit', '15'],
                cwd='/tmp', env=env, stdout=log, stderr=log)
            try:
                def window(title):
                    assert process.poll() is None, (out / 'session.log').read_text()[-3000:]
                    return next((w for w in x.windows(process.pid) if w['title'] == title), None)

                setup = wait(lambda: window('Set up Neo98'))
                time.sleep(.4)
                setup = window('Set up Neo98')
                x.capture(setup['id'], out / 'setup.png')

                def choose(image=False):
                    current = window('Set up Neo98')
                    x.click(current['x'] + (490 if image else 180), current['y'] + 115)
                    title = 'Choose an ISO, ZIP or 7z file' if image else 'Choose a Windows 98 folder'
                    dialog = wait(lambda: window(title))
                    time.sleep(.25)
                    dialog = window(title)
                    x.capture(dialog['id'], out / ('image.png' if image else 'folder.png'))
                    return title, dialog

                def chooser_button(dialog, right=False):
                    extents = x.property(dialog['id'], '_GTK_FRAME_EXTENTS')
                    if len(extents) == 4:
                        left, border_right, top = extents[:3]
                        dx = dialog['width'] - border_right - 54 if right else left + 54
                        dy = top + 23
                    else:
                        # GTK uses a bottom action area with server-side WM decorations.
                        dx = dialog['width'] - (54 if right else 144)
                        dy = dialog['height'] - 23
                    x.click(dialog['x'] + dx, dialog['y'] + dy)

                # This click hung in the previous build: the modal setup window
                # kept the GTK input grab while its non-modal child was visible.
                title, dialog = choose()
                chooser_button(dialog)
                wait(lambda: not window(title))
                time.sleep(.2)
                assert window('Set up Neo98')
                assert not (config / 'machine-path').exists()
                reports['folder_cancel_button'] = True

                # Escape must close the chooser, leaving the setup form alive.
                title, dialog = choose()
                x.chord('Escape')
                wait(lambda: not window(title))
                time.sleep(.2)
                assert window('Set up Neo98')
                reports['folder_escape'] = True

                title, dialog = choose(image=True)
                chooser_button(dialog)
                wait(lambda: not window(title))
                time.sleep(.2)
                assert window('Set up Neo98')
                reports['image_cancel_button'] = True

                title, dialog = choose()
                x.chord('Control_L', 'l')
                time.sleep(.15)
                x.chord('Control_L', 'a')
                type_path(x, str(root) + '/')
                time.sleep(.2)
                x.capture(dialog['id'], out / 'folder-path.png')
                x.chord('Return')
                time.sleep(.5)
                if window(title):
                    x.capture(dialog['id'], out / 'folder-path-entered.png')
                    chooser_button(dialog, right=True)
                wait(lambda: not window(title))
                x.capture(setup['id'], out / 'selected.png')
                x.click(setup['x'] + setup['width'] - 75, setup['y'] + setup['height'] - 37)
                wait(lambda: (config / 'machine-path').exists())
                assert (config / 'machine-path').read_text().strip() == str(root.resolve())
                process.wait(timeout=25)
                assert process.returncode == 0, (out / 'session.log').read_text()[-3000:]
                reports['folder_selected_and_started'] = True
            finally:
                if process.poll() is None:
                    process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
    assert not win98_c.pristine_problems(root)
    text = (out / 'session.log').read_text()
    assert 'CRITICAL' not in text, text[-3000:]
    (out / 'results.json').write_text(json.dumps(reports, indent=2) + '\n')
    print('LINUX SETUP UI PASS: folder/image cancel, Escape, real folder selection and startup')


if __name__ == '__main__':
    raise SystemExit(main())
