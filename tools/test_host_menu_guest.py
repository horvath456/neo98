#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The macOS menu bar of the host window, driven without a person.

tools/neo98_host.cpp takes NEO98_HOST_MENU="seconds:item[=argument];..." and
triggers each named item of tools/host_menu.mm the way a click does
(NSMenu performActionForItemAtIndex:, the open panel answered with the given
path), or posts a key press that AppKit matches against the key equivalents
of the menu. The host runs under SDL's dummy video driver, so no window appears
and the user's screen stays as it is; the menu then lives in an NSApplication
without a Dock icon. One run covers every item:

- Crash report from the item and from Cmd+Shift+B, each one report.
- Eject CD-ROM is disabled while no CD-ROM is in the drive.
- Scale 2x and 1x resize the window, Full screen switches in and out.
- A folder as CD-ROM: the running guest gets it as D: (no restart, regression;
  the guest side is tools/test_media_change_guest.py).
- An ISO image: hdiutil attaches it read-only, the mount point becomes the
  disc; Eject empties the drive and detaches the image.
- Drive D: with a folder: D: is that folder, a CD-ROM then takes E:.
- Network > Internet connected pulls the network cable; the menu shows it,
  a restart keeps it (--offline), the item plugs it in again.
- Restart through Cmd+Shift+R writes the reboot report, Quit ends the run.

    build/analysis-venv/bin/python tools/test_host_menu_guest.py [--no-build] [--no-images]

--no-images skips the three hdiutil checks when mounting images is forbidden.

Results in build/host-menu/results.json.
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


def main():
    images = '--no-images' not in sys.argv
    out = neo98.output_directory('host-menu')
    for name in ('drive-c', 'reports', 'cd-folder', 'iso-source', 'drive-d'):
        shutil.rmtree(out / name, ignore_errors=True)
    cd = out / 'cd-folder'
    (cd / 'SETUP').mkdir(parents=True)
    (cd / 'README.TXT').write_text('NEO98 menu test\r\n')
    source = out / 'iso-source' / 'NEO98MENU'
    source.mkdir(parents=True)
    (source / 'AUTORUN.INF').write_text('[autorun]\r\nopen=SETUP.EXE\r\n')
    iso = out / 'neo98menu.iso'
    iso.unlink(missing_ok=True)
    if images:
        subprocess.run(['hdiutil', 'makehybrid', '-quiet', '-iso', '-joliet', '-default-volume-name', 'NEO98MENU',
                        '-o', str(iso), str(source)], check=True)
    drive_d = out / 'drive-d'
    drive_d.mkdir()
    (drive_d / 'DATEN.TXT').write_text('D:\r\n')
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    steps = [
        # First start: no CD-ROM, D: empty.
        '4:describe', '1:cdrom-eject', '1:report', '1:shortcut=cmd+shift+b',
        '1:scale-2', '1:fullscreen', '1:fullscreen', '1:scale-1',
        # The network cable out; the menu shows it, a restart keeps it.
        '1:network-online', '1:describe',
        f'1:cdrom-insert={cd}',
        # The folder is the disc of D:.
        '2:describe', f'1:cdrom-insert={iso}',
        # The image is the disc.
        '2:cdrom-eject',
        # The drive is empty.
        f'2:drive-d={drive_d}',
        # The new D:; the empty CD-ROM drive moved to E:.
        f'2:cdrom-insert={cd}',
        # D: and the CD-ROM on E:.
        '2:describe', '1:shortcut=cmd+shift+r',
        # After the reboot: still without the cable, then plugged in again.
        '4:describe', '1:network-online', '1:quit',
    ]
    if not images:
        steps.remove(f'1:cdrom-insert={iso}')
    # A screen that holds the 1600x1200 window of scale 2 (the window is
    # fitted to the usable area of the Mac screen).
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', NEO98_HOST_MENU=';'.join(steps), NEO98_HOST_SCREEN='2560x1600')
    env.pop('NEO98_HOST_ATTACHED', None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'C:\\WINDOWS\\NOTEPAD.EXE', '--quiet',
               '--drive-c', str(out / 'drive-c'), '--no-registry-file',
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=600)
    log = log_path.read_text(errors='replace')
    lines = log.splitlines()
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)

    script = re.findall(r'^NEO98 HOST menu-script (.*) (ok|refused)$', log, re.M)
    check('every step ran', len(script) == len(steps))
    check('eject refused without a CD-ROM', ('cdrom-eject', 'refused') in script)
    check('other steps accepted', all(state == 'ok' for step, state in script if step != 'cdrom-eject')
          and script.count(('cdrom-eject', 'ok')) == 1)
    items = [line for line in lines if line.startswith('NEO98 HOST menu-item ')]
    for title in ('Machine > Restart [reboot] shift+cmd+r', 'Machine > Write crash report [report] shift+cmd+b',
                  'Machine > Mount ISO or folder as CD-ROM … [cdrom-insert]',
                  'Machine > Eject CD-ROM [cdrom-eject]', 'Machine > Choose drive D: … [drive-d]',
                  'View > Scale 1x [scale-1] cmd+1', 'View > Scale 2x [scale-2] cmd+2',
                  'View > Fullscreen [fullscreen] ctrl+cmd+f', 'Neo98 > Quit Neo98 [quit] cmd+q',
                  'Network > Internet connected [network-online] (enabled)'):
        check(f'menu has {title}', any(title in line for line in items))
    check('menu shows the folder CD-ROM', any('CD-ROM D: cd-folder' in line for line in items))
    check('menu shows D: and the CD-ROM on E:',
          any('CD-ROM E: cd-folder' in line for line in items) and any(f'D: {drive_d}' in line for line in items))
    # The cable: out from the menu, still out after the reboot (--offline),
    # in again from the menu.
    network = re.findall(r'^NEO98 HOST network (online|offline) address=', log, re.M)
    check('network cable out and in again', network == ['offline', 'online'])
    unplugged = [line for line in items if 'Network > ' in line and 'disconnected' in line]
    # Four lists after the cable went out: two before the reboot, one with
    # the CD-ROM on E:, one after the reboot.
    check('menu shows the pulled cable, also after the reboot', len(unplugged) == 4)
    check('menu item unchecked without the cable',
          sum('Network > Internet connected [network-online]' in line and '(enabled)' not in line for line in items) == 4)
    reports = re.findall(r'^NEO98 HOST report (.*)$', log, re.M)
    check('two reports, item and shortcut', len(reports) == 2 and all(Path(p, 'report.json').exists() for p in reports))
    views = re.findall(r'^NEO98 HOST view scale=(\d) fullscreen=(\d) window=(\d+)x(\d+)', log, re.M)
    check('scale 2 doubles the window', ('2', '0', '1600', '1200') in views)
    check('full screen on and off', any(v[1] == '1' for v in views) and views[-1] == ('1', '0', '800', '600'))
    restarts = re.findall(r'^NEO98 HOST restart reason=(.*)$', log, re.M)
    check('no restart for the media', restarts == [])
    drives = re.findall(r'^NEO98 HOST (?:drive|medium) ([A-Z]): (cdrom|fixed|readonly) (.*)$', log, re.M)
    check('folder as CD-ROM D:', ('D', 'cdrom', str(cd)) in drives)
    attach = re.search(r'^NEO98 HOST hdiutil attach (.*) -> (.*)$', log, re.M)
    mount = attach and attach[2]
    if images:
        check('ISO attached through hdiutil', attach and attach[1] == str(iso))
        check('image mount point as CD-ROM', mount and ('D', 'cdrom', mount) in drives)
        check('image detached on eject', mount and f'NEO98 HOST hdiutil detach {mount} (ejected)' in log
              and not Path(mount).exists())
    check('D: is the chosen folder', ('D', 'fixed', str(drive_d)) in drives)
    check('CD-ROM moves to E: beside D:', ('E', 'cdrom', str(cd)) in drives)
    check('Cmd+Shift+R reboots with a report', re.search(r'^NEO98 HOST reboot report=\S+', log, re.M))
    check('quit ends with exit code 0', result.returncode == 0)
    check('guest ran, then again after the reboot', log.count('NEO98 HOST running') == 2)
    check('no host failure', 'NEO98 HOST failed' not in log)
    report = {'log': str(log_path), 'exit_code': result.returncode, 'steps': steps, 'checks': checks, 'image_checks_skipped': not images,
              'menu': [line[len('NEO98 HOST menu-item '):] for line in items[:24]]}
    (out / 'results.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    failed = [name for name, okay in checks.items() if not okay]
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if failed:
        print(f'HOST MENU FAIL: {len(failed)} of {len(checks)} checks; see {log_path}')
        return 1
    print(f'HOST MENU PASS: {len(checks)} checks; {out / "results.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
