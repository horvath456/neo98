#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The caption of the active MDI child is active while its frame is.

USER keeps a frame-on bit per window that DefWindowProc sets and clears with
WM_NCACTIVATE; the caption of a child window is drawn active by it. The MDI
client hands the frame's WM_NCACTIVATE on to its active child, and activating
a child sends it WM_NCACTIVATE TRUE while the frame is the active window.
Three sessions on neo98-kms, with the picture of a 1024x768 desktop in memory:

  winfile  File Manager (16-bit WINFILE.EXE): the caption of its window of
           drive C: is active, inactive while File > Run has the activation,
           and active again once the dialog is closed
  progman  Program Manager (16-bit PROGMAN.EXE) makes a group from the dialog
           of File > New, which disables its frame: the new group is the
           active child all the same and shows the active caption once the
           dialog is closed
  fixture  tools/mdi_caption_fixture.c, a 32-bit MDI frame with two
           children: the second, active child has the active caption and
           the first the inactive one; a message box over the frame turns
           both inactive, and closing it brings the active caption back

Before, every MDI child kept the inactive caption. Each step that needs a
menu or a dialog waits for it, and each picture waits until the machine is
quiet, so a loaded machine only makes the sessions longer.

    build/analysis-venv/bin/python tools/test_mdi_caption_host.py [--no-build] [--keep] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import neo98  # noqa: E402
import win98_c  # noqa: E402
from test_media_center_host import read_png  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/mdi-caption'
# A point of the caption of File Manager's window of drive C: on 1024x768,
# left of its buttons, and one of Program Manager's first group.
WINFILE_CAPTION = (600, 102)
PROGMAN_CAPTION = (450, 110)
IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'GetModuleHandleA': 4, 'CreateFileA': 28, 'WriteFile': 20, 'CloseHandle': 4,
                 'lstrlenA': 4},
    'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefFrameProcA': 20, 'DefMDIChildProcA': 16,
               'SendMessageA': 16, 'ShowWindow': 8, 'UpdateWindow': 4, 'GetWindowRect': 8, 'GetMessageA': 16,
               'TranslateMessage': 4, 'DispatchMessageA': 4, 'TranslateMDISysAccel': 8, 'MessageBoxA': 16,
               'PostQuitMessage': 4, 'LoadCursorA': 8, 'GetSysColorBrush': 4, 'wsprintfA': None},
}


def build_fixture(out):
    env = neo98.tool_environment()
    dlltool = shutil.which('llvm-dlltool', path=env['PATH'])
    if not dlltool:
        raise SystemExit('llvm-dlltool is missing')
    libraries = []
    for dll, names in IMPORTS.items():
        definition, library = out / f'{dll}.def', out / f'{dll}.lib'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' + ''.join(
            f'{name}\n' if size is None else f'{name}@{size}\n' for name, size in names.items()))
        subprocess.run([dlltool, '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)], env=env, check=True)
        libraries.append(str(library))
    obj, exe = out / 'fixture.obj', out / 'MDICAP.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-stack-protector',
                    '-c', str(ROOT / 'tools/mdi_caption_fixture.c'), '-o', str(obj)], env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/nodefaultlib', '/machine:x86',
                    '/timestamp:0', '/out:' + str(exe), str(obj), *libraries], env=env, check=True)
    return exe


def pixel(picture, x, y):
    width, _, data = picture
    return tuple(data[(y * width + x) * 3:(y * width + x) * 3 + 3])


def active(colour):
    """The caption colours of Windows 98: navy to light blue, or two greys."""
    red, green, blue = colour
    return blue > red + 60


def inactive(colour):
    red, green, blue = colour
    return red == green == blue and red >= 0x80


def session(kms, drive, exe, steps, limit):
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(WORK)))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    result = subprocess.run([str(kms), '--drive-c', str(drive), '--desktop', '1024x768', '--exe', exe,
                             '--time-limit', str(limit)], cwd=ROOT, env=env, capture_output=True, text=True,
                            timeout=limit + 60)
    return result.stdout + result.stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--keep', action='store_true', help='keep the copy of the drive')
    parser.add_argument('--kms', type=Path, default=BINARY)
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host()
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    fixture = build_fixture(WORK)
    drive = WORK / 'c'
    pristine = win98_c.use_pristine()
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), win98_c.journal(drive))
    shutil.copyfile(fixture, drive / 'MDICAP.EXE')
    try:
        # File > Run is the item below the separator under Properties.
        log = session(args.kms, drive, 'C:\\WINDOWS\\WINFILE.EXE',
                      ['0:wait=WFS_Frame', '0:idle=500', '0:shot={out}/winfile-active.png', '0.5:click=19,31',
                       '0.5:wait=#32768', '1:click=40,163', '1:wait=#32770|Run', '1:idle=300',
                       '1:shot={out}/winfile-dialog.png', '1.5:key=1b', '1.5:idle=500',
                       '1.5:shot={out}/winfile-back.png', '2:quit=1'], 120)
        # File > New without a group asks for the properties of a new one.
        log += session(args.kms, drive, 'C:\\WINDOWS\\PROGMAN.EXE',
                       ['0:wait=Progman', '0:idle=500', '0.5:alt=46', '0.5:wait=#32768', '1:type=n',
                        '1:wait=#32770|Program Group Properties', '1:idle=300', '1.5:type=First', '2:key=0d',
                        '2:idle=500', '2:shot={out}/progman-group.png', '2.5:quit=1'], 120)
        log += session(args.kms, drive, 'C:\\MDICAP.EXE',
                       ['0:wait=Neo98MdiCaptionFrame', '0:idle=500', '0:shot={out}/fixture-active.png', '0.5:type=m',
                        '0.5:wait=#32770', '0.5:idle=500', '0.5:shot={out}/fixture-box.png', '1:key=0d', '1:idle=500',
                        '1:shot={out}/fixture-back.png', '1.5:quit=1'], 120)
        written = win98_c.path('C:\\MDICAP.TXT', drive)
        rectangles = [list(map(int, line.split())) for line in written.read_text().split('\n') if line.strip()] \
            if written else []
    finally:
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
    (WORK / 'log.txt').write_text(log)
    stops = [line for line in log.splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line]
    names = ('winfile-active', 'winfile-dialog', 'winfile-back', 'fixture-active', 'fixture-box', 'fixture-back',
             'progman-group')
    pictures = {name: read_png(WORK / f'{name}.png') if (WORK / f'{name}.png').is_file() else None for name in names}
    winfile = {name: pixel(pictures[name], *WINFILE_CAPTION) for name in names[:3] if pictures[name]}
    # A point of each child's caption, left of its three buttons.
    points = [(r[2] - 80, r[1] + 12) for r in rectangles] if len(rectangles) == 2 else []
    fixture = {name: [pixel(pictures[name], *point) for point in points] for name in names[3:6]
               if pictures[name] and points}
    group = pixel(pictures['progman-group'], *PROGMAN_CAPTION) if pictures['progman-group'] else None
    timed_out = [line for line in log.splitlines() if 'timed out after' in line]
    checks = {
        'every session runs without a stop': not stops and all(pictures.values()) and len(points) == 2,
        f'every menu, dialog and quiet moment came: {timed_out}': not timed_out,
        f'winfile: the active child shows the active caption {winfile.get("winfile-active")}':
            'winfile-active' in winfile and active(winfile['winfile-active']),
        f'winfile: inactive while File > Run is open {winfile.get("winfile-dialog")}':
            'winfile-dialog' in winfile and inactive(winfile['winfile-dialog']),
        f'winfile: active again after the dialog {winfile.get("winfile-back")}':
            'winfile-back' in winfile and active(winfile['winfile-back']),
        f'progman: the group made from File > New shows the active caption {group}':
            group is not None and active(group),
        f'fixture: first child inactive, second active {fixture.get("fixture-active")}':
            'fixture-active' in fixture and inactive(fixture['fixture-active'][0]) and
            active(fixture['fixture-active'][1]),
        f'fixture: both inactive under the message box {fixture.get("fixture-box")}':
            'fixture-box' in fixture and all(inactive(colour) for colour in fixture['fixture-box']),
        f'fixture: the second child active again after it {fixture.get("fixture-back")}':
            'fixture-back' in fixture and inactive(fixture['fixture-back'][0]) and
            active(fixture['fixture-back'][1]),
    }
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    if stops:
        print('    stop:', stops[0])
    print(f'Pictures and log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'mdi caption: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
