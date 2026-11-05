#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""One host per drive C: (tools/host_single.h, docs/BUILDING.md).

A host runs the machine of a cloned machine folder under SDL's dummy video
driver. While it runs, further starts on the same folder must end at once,
bring the first to the front and hand it the program they were asked for:

- the app without arguments (nothing to hand over, the window comes forward),
- the app with --exe and a guest path,
- the command line of the development with a program outside the drives,
- `tools/host_run.py` (`neo98.py run`) with --drive-c,
- once more after the first host restarted the machine (the lock holds across
  the execv).

The log of the running host is not replaced by a second start. A host on
another drive C: starts beside the first as before. After the first host
ended the lock is free.

    build/analysis-venv/bin/python tools/test_single_instance_host.py [--no-build]

Results in build/single-host/results.json.
"""
from __future__ import annotations

import fcntl
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
import win98_c  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
from test_mac_app_host import clone_machine  # noqa: E402


def wait_for(path, pattern, timeout=400, count=1):
    """Until the log holds `patterncount` times."""
    end = time.time() + timeout
    while time.time() < end:
        if len(re.findall(pattern, path.read_text(errors='replace'), re.M)) >= count:
            return True
        time.sleep(0.5)
    return False


def locked(path):
    """Whether a process holds the lock file."""
    try:
        with open(path) as handle:
            try:
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError:
                return True
    except OSError:
        pass
    return False


def main():
    out = neo98.output_directory('single-host')
    for name in ('settings', 'logs', 'outside', 'other-c', 'tmp', 'machine'):
        shutil.rmtree(out / name, ignore_errors=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    pristine = win98_c.use_pristine()
    machine = clone_machine(out / 'machine')
    drive_c = machine / 'C'
    lock = machine / 'C.host-lock'
    settings_dir, logs = out / 'settings', out / 'logs'
    settings_dir.mkdir()
    (out / 'tmp').mkdir(exist_ok=True)
    (settings_dir / 'settings.ini').write_text(f'# Test\nmachine = {machine}\nscale = 1\n')
    outside = out / 'outside'
    outside.mkdir()
    shutil.copyfile(win98_c.path('C:\\WINDOWS\\CALC.EXE', drive_c), outside / 'CALC.EXE')
    font = ROOT / 'build/generated/notepad-fixedsys.fnt'
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)

    base = {'HOME': str(Path.home()), 'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy',
            'NEO98_SETTINGS_DIR': str(settings_dir), 'NEO98_LOG_DIR': str(logs), 'TMPDIR': str(out / 'tmp'),
            'NEO98_HOST_INPUT_ONCE': '1'}
    # The first host waits for the windows the later starts bring.
    steps = ['0:wait=Shell_TrayWnd', '1:idle=1500', '0:wait=Notepad', '0:wait=SciCalc', '0:wait=#32770',
             '1:menu=reboot', '0:wait=Shell_TrayWnd', '1:idle=1500', '0:wait=Notepad', '1:menu=quit']
    first_log = out / 'first.log'
    second_log = out / 'second.log'
    second_log.write_text('')
    seconds = []

    def second(command, environment=None, cwd='/'):
        started = time.time()
        result = subprocess.run(command, cwd=cwd, env=environment or base, capture_output=True, text=True,
                                errors='replace', stdin=subprocess.DEVNULL, timeout=120)
        with second_log.open('a') as log:
            log.write(f'$ {command}\n{result.stdout}{result.stderr}exit {result.returncode}\n')
        seconds.append(time.time() - started)
        return result.returncode, result.stdout + result.stderr

    with first_log.open('w') as log:
        first = subprocess.Popen([str(binary)], cwd='/', env=dict(base, NEO98_HOST_INPUT=';'.join(steps)),
                                 stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    try:
        check('first host reaches the desktop', wait_for(first_log, r'^NEO98 HOST input wait=Shell_TrayWnd reached'))
        check('lock beside C is held and names the process',
              locked(lock) and lock.read_text().strip() == str(first.pid))

        # The app without arguments; its log would go to the log folder.
        code, text = second([str(binary)], dict(base, NEO98_LOG_TO_FILE='1'))
        check('second app start ends with 0 and names the owner',
              code == 0 and f'runs in process {first.pid}; handed over: front' in text)
        check('second start leaves the log folder alone', not (logs / 'host-session.log').exists())
        check('first host comes to the front',
              wait_for(first_log, r'^NEO98 HOST second start: front$', 20)
              and wait_for(first_log, r'^NEO98 HOST brought to front$', 20))

        # The app with a program on C:.
        code, text = second([str(binary), '--exe', 'C:\\WINDOWS\\NOTEPAD.EXE'])
        check('second start with --exe hands the guest path over',
              code == 0 and 'handed over: open C:\\WINDOWS\\NOTEPAD.EXE' in text)
        check('first host starts it', wait_for(first_log, r'^NEO98 HOST input wait=Notepad reached', 260))

        # The command line of the development, a program outside the drives.
        code, text = second([str(binary), '-', str(font), '--exe', str(outside / 'CALC.EXE'), '--system-tree',
                             str(drive_c), '--drive-c', str(drive_c), '--registry', str(machine / 'C.registry')])
        check('development command line hands a host path over',
              code == 0 and f'handed over: open {(outside / "CALC.EXE").resolve()}' in text
              and 'NEO98 HOST drive C:' not in text)
        check('first host mounts the folder and starts it',
              wait_for(first_log, r'^NEO98 HOST input wait=SciCalc reached', 260))

        # neo98.py run; its session log stays as it is.
        session = ROOT / 'build/host-session.log'
        before = session.read_bytes() if session.exists() else None
        environment = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        code, text = second([sys.executable, str(ROOT / 'tools/host_run.py'), '--no-build', '--no-programs-drive',
                             '--drive-c', str(drive_c), '--exe', 'C:\\WINDOWS\\WINVER.EXE'], environment, cwd=ROOT)
        check('host_run.py hands over and ends', code == 0 and 'handed over: open C:\\WINDOWS\\WINVER.EXE' in text)
        check('host_run.py keeps the session log',
              (session.read_bytes() if session.exists() else None) == before)
        check('first host starts that too', wait_for(first_log, r'^NEO98 HOST input wait=#32770 reached', 260))

        # A host on another drive C: is not concerned.
        other = out / 'other-c'
        code, text = second([str(binary), '-', str(font), '--system-tree', str(pristine), '--exe',
                             'C:\\WINDOWS\\NOTEPAD.EXE', '--quiet', '--drive-c', str(other), '--no-registry-file'],
                            dict(base, NEO98_HOST_INPUT='0:wait=Notepad;1:menu=quit'))
        check('a host on another drive C: runs beside it',
              code == 0 and 'wait=Notepad reached' in text and 'handed over' not in text)
        check('and released its own lock', (out / 'other-c.host-lock').exists() and not locked(out / 'other-c.host-lock'))

        # The restart keeps the lock.
        check('first host restarted', wait_for(first_log, r'^NEO98 HOST input wait=Shell_TrayWnd reached', 400, 2))
        check('lock still held by the same process', locked(lock) and lock.read_text().strip() == str(first.pid))
        code, text = second([str(binary), '--exe', 'C:\\WINDOWS\\NOTEPAD.EXE'])
        check('second start after the restart hands over', code == 0 and 'handed over: open' in text)
        check('first host ends in order', first.wait(timeout=400) == 0)
    finally:
        if first.poll() is None:
            first.terminate()
            try:
                first.wait(timeout=20)
            except subprocess.TimeoutExpired:
                first.terminate()
    log = first_log.read_text(errors='replace')
    waits = re.findall(r'^NEO98 HOST input wait=(.*) (reached|timed out)', log, re.M)
    check('every wait was reached', len(waits) == 6 and all(state == 'reached' for _, state in waits))
    check('lock is free after the end', not locked(lock))
    check('nothing handed over is left', not (machine / 'C.host-open').exists()
          or not (machine / 'C.host-open').stat().st_size)
    check('a second start takes less than five seconds', seconds and max(seconds[:4] + seconds[5:]) < 5)
    check('a second start does not activate itself',
          'NEO98 HOST activation' not in second_log.read_text(errors='replace').split('other-c')[0])
    check('first host: activation at start, per second start, at the restart',
          log.count('NEO98 HOST activation (start): no screen') == 2
          and log.count('NEO98 HOST activation (second start): no screen') == 5)
    check('no host failure', 'NEO98 HOST failed' not in log and 'libc++abi' not in log)

    for name in ('machine', 'other-c', 'tmp'):
        shutil.rmtree(out / name, ignore_errors=True)
    (out / 'results.json').write_text(json.dumps({'checks': checks, 'log': str(first_log),
                                                  'second': str(second_log)}, indent=2) + '\n')
    failed = [name for name, okay in checks.items() if not okay]
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if failed:
        print(f'{len(failed)} of {len(checks)} checks failed; logs {first_log}, {second_log}')
        return 1
    print(f'single host: {len(checks)} checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
