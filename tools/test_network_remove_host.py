#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The Network applet of Windows 98 removes a protocol, on neo98-kms.

Over a copy of setup's drive C: the Network applet (NETCPL.CPL) lists
Client for Microsoft Networks, Microsoft Family Logon, the Dial-Up Adapter,
the network card and TCP/IP bound to each adapter:

  remove   TCP/IP -> Dial-Up Adapter is selected and removed: the list shows
           the other five
  apply    OK applies the change: NETDI copies the files of the network and
           asks to restart, without a stop. NETDI loads DEVMGR32 with
           LoadLibraryEx32W, whose DllMain connects the flat thunk of the
           16-bit SYSDM.CPL; ThunkConnect32 loads that module, which no one
           had loaded, and the run stopped at address 0 when NETDI called
           DEVMGR32.
  restart  the applet of a new start shows the list that OK applied. NETDI
           made new instances of the client and Family Logon under
           Enum\\Network and deleted the old ones with the protocol; the
           journal of the registry brought every instance of the hives back
           at the next start, as it does for the devices a machine finds
           again, and the list showed the client and Family Logon twice and
           the removed protocol again.

    build/analysis-venv/bin/python tools/test_network_remove_host.py [--no-build] [--kms KMS]
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
import win98_c  # noqa: E402
from test_media_center_host import read_png  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/network-remove'
# The Network sheet at its first place on 1024x768: the fifth line of its
# list (TCP/IP -> Dial-Up Adapter), Remove, OK, and a point of the face of
# the box that asks to restart, and its No.
FIFTH, REMOVE, OK = (200, 217), (247, 276), (303, 492)
RESTART_BOX, NO = (560, 430), (551, 420)
LINES = 7


def run(kms, drive, steps, limit):
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(WORK)))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    result = subprocess.run([str(kms), '--drive-c', str(drive), '--desktop', '1024x768',
                             '--exe', 'C:\\WINDOWS\\RUNDLL32.EXE',
                             '--command', 'RUNDLL32.EXE SHELL32.DLL,Control_RunDLL NETCPL.CPL',
                             '--time-limit', str(limit)], cwd=ROOT, env=env, capture_output=True, text=True,
                            timeout=limit + 60)
    return result.stdout + result.stderr


def lines(picture):
    """The dark pixels of each line of the list of components, without the
    icons and the focus rectangle: a signature of the names it shows."""
    if not picture:
        return []
    width, _, data = picture
    return [sum(data[(y * width + x) * 3:(y * width + x) * 3 + 3] == b'\0\0\0'
                for y in range(144 + 17 * i, 156 + 17 * i) for x in range(112, 380)) for i in range(LINES)]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--kms', type=Path, default=BINARY)
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host()
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    drive = WORK / 'c'
    pristine = win98_c.use_pristine()
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), win98_c.journal(drive))
    click = lambda at, point: f'{at}:click={point[0]},{point[1]}'
    try:
        # Each click and picture once the machine is quiet, not at a time.
        log = run(args.kms, drive, ['0:wait=#32770', '0:idle=500', '0:shot={out}/start.png', click(0.5, FIFTH),
                                    '0.5:idle=300', click(1, REMOVE), '1:idle=500', '1:shot={out}/removed.png',
                                    click(1.5, OK), f'1.5:wait={RESTART_BOX[0]},{RESTART_BOX[1]},c0c0c0',
                                    '1.5:idle=300', '1.5:shot={out}/restart.png', click(2, NO), '2:idle=500',
                                    '2.5:quit=1'], 180)
        log += run(args.kms, drive, ['0:wait=#32770', '0:idle=500', '0:shot={out}/restarted.png', '0.5:quit=1'], 120)
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        win98_c.journal(drive).unlink(missing_ok=True)
    (WORK / 'log.txt').write_text(log)
    stops = [line for line in log.splitlines() if 'CRASHREPORT' in line or 'not ported' in line
             or 'no native provider' in line or 'Guest execution at address' in line]
    pictures = {name: read_png(WORK / f'{name}.png') if (WORK / f'{name}.png').is_file() else None
                for name in ('start', 'removed', 'restart', 'restarted')}
    start, removed, restarted = (lines(pictures[name]) for name in ('start', 'removed', 'restarted'))
    timed_out = [line for line in log.splitlines() if 'timed out after' in line]
    checks = {
        f'every dialog and quiet moment came: {timed_out}': not timed_out,
        f'the list shows the six components of setup: {start}': len([n for n in start if n]) == 6,
        f'remove: TCP/IP -> Dial-Up Adapter leaves the list: {removed}':
            start and removed == start[:4] + start[5:] + [0],
        'apply: OK copies the files and asks to restart': pictures['restart'] is not None
            and f'NEO98 KMS input pixel {RESTART_BOX[0]},{RESTART_BOX[1]} is c0c0c0' in log,
        'apply: the run goes on without a stop': not stops,
        f'restart: the list is the one OK applied: {restarted}': removed and restarted == removed,
    }
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    if stops:
        print('    stop:', stops[0])
    print(f'Pictures and log in {WORK}', flush=True)
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'network remove: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
