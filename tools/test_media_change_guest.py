#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A CD-ROM and D: change in the running guest, not by a restart.

Two runs of the real host (build/native-app/neo98-host) under SDL's dummy video
driver, driven through its menu by NEO98_HOST_MENU like
tools/test_host_menu_guest.py:

1. DEVCHG.EXE (tools/device_change_fixture.c) runs with a folder as CD-ROM D:
   and holds a file of it open. Eject, an ISO image, then a folder as D:.
   DEVCHG.LOG must show DBT_DEVICEREMOVECOMPLETE and DBT_DEVICEARRIVAL with
   the DEV_BROADCAST_VOLUME of each drive (DBT_DEVTYP_VOLUME, the unit mask,
   DBTF_MEDIA for the CD-ROM), ERROR_NOT_READY from the empty drive and from
   the handle of the disc that left, the label of the new disc, and the CD-ROM
   moving on to E: beside the new D:. The host must not restart.
2. Explorer as the shell; an ISO with AUTORUN.INF (open=SETUP.EXE) goes into
   the drive. Win98's Explorer runs SETUP.EXE on the arrival, which writes
   C:\\AUTORUN.OK.

    build/analysis-venv/bin/python tools/test_media_change_guest.py [--no-build]

Results in build/media-change/results.json.
"""
from __future__ import annotations
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: mounts images with hdiutil, which exists on macOS only')  # macOS only; skipped elsewhere
        _sys.exit(77)

import json
import os
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

OUT = ROOT / 'build/media-change'
FONT = ROOT / 'build/generated/notepad-fixedsys.fnt'
IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'GetLastError': 0, 'SetLastError': 4, 'GetCommandLineA': 0,
                 'CreateFileA': 28, 'WriteFile': 20, 'ReadFile': 20, 'SetFilePointer': 16,
                 'CloseHandle': 4, 'GetVolumeInformationA': 32, 'GetFileAttributesA': 4,
                 'GetModuleHandleA': 4},
    'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16, 'GetMessageA': 16,
               'DispatchMessageA': 4, 'SetTimer': 16, 'PostQuitMessage': 4},
}


def build_fixture(env):
    libraries = []
    for dll, exports in IMPORTS.items():
        definition = OUT / f'{dll}.def'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' +
                              ''.join(f'{name}@{size}\n' for name, size in exports.items()))
        library = OUT / f'{dll}-import.lib'
        subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)],
                       env=env, check=True)
        libraries.append(str(library))
    obj = OUT / 'devchg.obj'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-Wall', '-Werror', '-c',
                    str(ROOT / 'tools/device_change_fixture.c'), '-o', str(obj)], env=env, check=True)
    exe = OUT / 'DEVCHG.EXE'
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0', '/nodefaultlib',
                    '/machine:x86', '/safeseh:no', f'/out:{exe}', str(obj), *libraries], env=env, check=True)
    return exe


def iso(name, files):
    source = OUT / f'{name}-source'
    shutil.rmtree(source, ignore_errors=True)
    source.mkdir(parents=True)
    for relative, data in files.items():
        (source / relative).write_bytes(data)
    image = OUT / f'{name}.iso'
    image.unlink(missing_ok=True)
    subprocess.run(['hdiutil', 'makehybrid', '-quiet', '-iso', '-joliet', '-default-volume-name', name.upper(),
                    '-o', str(image), str(source)], check=True)
    return image


def run_host(binary, arguments, steps, log_path, timeout=600, script=None):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_MENU=';'.join(steps))
    if script:
        # Steps that wait for the guest instead of the clock (NEO98_HOST_INPUT).
        env['NEO98_HOST_INPUT'] = ';'.join(script)
        env.pop('NEO98_HOST_MENU')
    env.pop('NEO98_HOST_ATTACHED', None)
    with log_path.open('w') as log:
        result = subprocess.run([str(binary), *arguments], cwd=ROOT, env=env, stdout=log,
                                stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=timeout)
    return result.returncode, log_path.read_text(errors='replace')


def main():
    if OUT.is_symlink():
        raise SystemExit(f'{OUT} is a link into another checkout')
    shutil.rmtree(OUT, ignore_errors=True)
    OUT.mkdir(parents=True)
    env = neo98.tool_environment()
    fixture = build_fixture(env)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)

    # 1. Events, the empty drive, the lost handle, D: and E:.
    cd = OUT / 'cd-folder'
    cd.mkdir()
    (cd / 'README.TXT').write_text('first disc\r\n')
    disc = iso('neo98disc', {'OTHER.TXT': b'second disc\r\n'})
    drive_d = OUT / 'drive-d'
    drive_d.mkdir()
    (drive_d / 'README.TXT').write_text('fixed D:\r\n')
    drive_c = OUT / 'drive-c'
    steps = ['8:cdrom-eject', f'3:cdrom-insert={disc}', f'3:drive-d={drive_d}', '4:quit']
    code, log = run_host(binary, [str(fixture), str(FONT), '--system-tree', '--quiet', '--drive-c', str(drive_c),
                                  '--no-registry-file', '--cdrom', str(cd),
                                  '--crash-report-dir', str(OUT / 'reports')],
                         steps, OUT / 'host-events.log')
    events = (drive_c / 'DEVCHG.LOG').read_text(errors='replace') if (drive_c / 'DEVCHG.LOG').exists() else ''
    lines = events.splitlines()
    check('fixture held the file of the first disc', lines[:1] == ['start held=1'])
    check('eject: DBT_DEVICEREMOVECOMPLETE for D: with DBTF_MEDIA',
          any(l.startswith('event=00008004 size=00000012 type=00000002 mask=00000008 flags=00000001') for l in lines))
    check('empty drive answers ERROR_NOT_READY',
          any('event=00008004' in l and 'volume-error=00000015' in l for l in lines))
    check('the handle of the disc that left fails with ERROR_NOT_READY',
          all('held-read=00000015' in l for l in lines[1:]) and len(lines) > 1)
    check('new disc: DBT_DEVICEARRIVAL for D: with its label',
          any(l.startswith('event=00008000 size=00000012 type=00000002 mask=00000008 flags=00000001')
              and 'label=NEO98DISC' in l and 'probe=absent' in l for l in lines))
    check('the CD-ROM moves on to E:',
          any(l.startswith('event=00008000') and 'mask=00000010' in l for l in lines))
    check('the folder arrives as D:',
          any(l.startswith('event=00008000') and 'mask=00000008 flags=00000000' in l and 'probe=present' in l
              for l in lines))
    check('no restart for the media', 'NEO98 HOST restart' not in log and log.count('NEO98 HOST running') == 1)
    check('host posted to the guest windows', re.search(r'MEDIA D: WM_DEVICECHANGE DBT_DEVICEARRIVAL to [1-9]', log))
    check('image detached at the end', 'hdiutil detach' in log)
    check('first run ends with exit code 0', code == 0)

    # 2. Autorun through Explorer.
    autorun = iso('neo98auto', {'AUTORUN.INF': b'[autorun]\r\nopen=SETUP.EXE\r\n',
                                'SETUP.EXE': fixture.read_bytes()})
    drive_c2 = OUT / 'drive-c-explorer'
    # Explorer needs 35 to 80 s for its first start, more on a loaded machine;
    # the script waits for the taskbar and a quiet guest, inserts the disc
    # and waits until the guest is quiet again.
    script = ['0:wait=Shell_TrayWnd', '0:idle=3000', '1:menu=describe', f'1:menu=cdrom-insert={autorun}',
              '2:idle=5000', '3:menu=quit']
    code2, log2 = run_host(binary, ['-', str(FONT), '--system-tree', '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--quiet',
                                    '--drive-c', str(drive_c2), '--no-registry-file',
                                    '--crash-report-dir', str(OUT / 'reports-explorer')],
                           [], OUT / 'host-explorer.log', script=script)
    marker = drive_c2 / 'AUTORUN.OK'
    check('Explorer runs AUTORUN.INF of the new disc', marker.exists())
    check('autorun started SETUP.EXE from the disc',
          marker.exists() and 'SETUP.EXE' in marker.read_text(errors='replace').upper())
    check('second run: no restart', 'NEO98 HOST restart' not in log2)

    report = {'checks': checks, 'events': lines, 'logs': [str(OUT / 'host-events.log'), str(OUT / 'host-explorer.log')]}
    (OUT / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    failed = [name for name, okay in checks.items() if not okay]
    if failed:
        print(f'MEDIA CHANGE FAIL: {len(failed)} of {len(checks)} checks; see {OUT}')
        return 1
    print(f'MEDIA CHANGE PASS: {len(checks)} checks')
    return 0


if __name__ == '__main__':
    sys.exit(main())
