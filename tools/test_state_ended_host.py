#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A machine is saved after a process or a thread of it has ended.

Each case runs on a private copy of setup's drive C:, saves the machine in
neo98-host (wstate=save) and loads the state in a new host process
(--state-load):

  child     tools/state_ended_fixture.c starts itself once more, and the
            process that started last ends first. Every process binds the
            providers of USER to itself when it starts; the one that ends
            hands their contexts to a process that stays
            (User::DetachProcess), or the next state refuses the pointer to
            its freed Application
  thread    a thread of the fixture ends with ExitThread from its window
            procedure, after a wait two callbacks deep, and its handle stays
            open. The finished thread keeps no depth of callbacks
            (Application::Run), or the writer looks for the registers of
            marks that are gone
  explorer  the shell with My Computer open, saved after Notepad was
            started from Start > Run and closed again, while the shell's
            child processes (MPREXE, the spooler) live on; after the load
            the Start menu opens

The host needs an X server for its GTK menu bar:

    xvfb-run -a python3 tools/test_state_ended_host.py [--no-build] [--binary HOST] [--only CASE,...] [--keep]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
import shutil as _shutil
if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture; docs/BUILDING.md
    _skip('needs i686-w64-mingw32-gcc')
import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-host'
WORK = ROOT / 'build/state-ended'
CASES = ('child', 'thread', 'explorer')


def fresh(source, drive):
    shutil.rmtree(drive, ignore_errors=True)
    win98_c.journal(drive).unlink(missing_ok=True)
    subprocess.run(['cp', '-a', '--reflink=auto', str(source), str(drive)], check=True)
    if win98_c.journal(source).is_file():
        shutil.copy2(win98_c.journal(source), win98_c.journal(drive))


def run(binary, folder, name, drive, steps, options, deadline=90):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US',
               NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_DEADLINE=str(deadline))
    for variable in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_TRACE_CALLS', 'NEO98_CPU',
                     'NEO98_STATE_IMAGE', 'NEO98_STATE_EXEC'):
        env.pop(variable, None)
    # The previous host of this drive has ended; its lock names a process
    # that is gone.
    Path(str(drive) + '.host-lock').unlink(missing_ok=True)
    command = [str(binary), '--drive-c', str(drive), '--desktop', '800x600', *options,
               '--crash-report-dir', str(folder / 'reports')]
    try:
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, errors='replace',
                                stdin=subprocess.DEVNULL, timeout=deadline + 60)
        text = result.stdout + result.stderr
    except subprocess.TimeoutExpired as error:
        text = error.stdout if isinstance(error.stdout, str) else (error.stdout or b'').decode(errors='replace')
    (folder / f'{name}.log').write_text(text)
    return text


def failures(text):
    return [line for line in text.splitlines()
            if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line
            or line.startswith('FAULT BOX') or 'ERROR: AddressSanitizer' in line or 'runtime error:' in line]


def saved(text):
    match = re.search(r'^NEO98 HOST state saved \S+ bytes=(\d+) .* processes=(\d+) threads=(\d+)', text, re.M)
    return dict(bytes=int(match[1]), processes=int(match[2]), threads=int(match[3])) if match else None


def refusal(text):
    """Why the state was not written: the writer's reason before a refusal
    to wait for the moment of a save."""
    failed = re.findall(r'^NEO98 HOST state save failed: (.*)$', text, re.M)
    refused = re.findall(r'^NEO98 HOST state save refused[^:]*: (.*)$', text, re.M)
    return (failed or refused or [None])[-1]


def loaded(text):
    return re.search(r'^NEO98 HOST state loaded \S+ ms=[\d.]+ end-pointers=0$', text, re.M) is not None


def kernel_version(drive):
    """(major, minor) of the file version of the drive's KERNEL32.DLL."""
    path = win98_c.path('C:\\WINDOWS\\SYSTEM\\KERNEL32.DLL', drive)
    data = path.read_bytes() if path else b''
    at = data.find(struct.pack('<I', 0xFEEF04BD))
    if at < 0:
        return (4, 10)
    high = struct.unpack_from('<I', data, at + 8)[0]
    return (high >> 16, high & 0xffff)


def visible(text, cls, title=None):
    return any(m[0] == cls and (title is None or m[1] == title)
               for m in re.findall(r'^NEO98 HOST (?:top|child) id=\w+ visible=1 .* class=(\S+) title=(.*)$', text, re.M))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--binary', type=Path, default=BINARY, help='the host to test (default: the built one)')
    parser.add_argument('--only', help='a comma-separated choice of ' + ', '.join(CASES))
    parser.add_argument('--keep', action='store_true', help='keep the drives and the states')
    args = parser.parse_args()
    cases = args.only.split(',') if args.only else list(CASES)
    unknown = set(cases) - set(CASES)
    if unknown:
        raise SystemExit(f'unknown cases: {", ".join(sorted(unknown))}')
    if not native_app.native_x64():
        raise SystemExit('this test runs the Linux x64 host')
    pristine = win98_c.use_pristine()
    if not args.no_build and args.binary == BINARY:
        native_app.build_host()
    if not Path(str(args.binary) + '.layout').is_file():
        raise SystemExit(f'{args.binary} has no layout table and saves no Windows programs; see '
                         f'{args.binary}.layout-problems')
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    fixture = WORK / 'ENDED.EXE'
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/state_ended_fixture.c'), '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32', '-o', str(fixture)], check=True)
    results, failed = {}, []

    def check(name, condition):
        results[name] = bool(condition)
        print(('ok   ' if condition else 'FAIL ') + name, flush=True)
        if not condition:
            failed.append(name)

    def case(name, options, save_steps, load_steps):
        folder = WORK / name
        folder.mkdir()
        drive, state = folder / 'c', folder / 'machine.state'
        fresh(pristine, drive)
        shutil.copy(fixture, drive / 'ENDED.EXE')
        before = run(args.binary, folder, 'save', drive, save_steps, [*options, '--state-save', str(state)])
        after = ''
        if state.is_file() and saved(before):
            after = run(args.binary, folder, 'load', drive, load_steps, ['--state-load', str(state)])
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
            state.unlink(missing_ok=True)
        why = refusal(before)
        if why and not saved(before):
            print(f'    {name}: {why}', flush=True)
        return before, after

    for name, code in (('child', 3), ('thread', 7)):
        if name not in cases:
            continue
        ready = f'Ended ready {code}'
        before, after = case(name, ['--exe', 'C:\\ENDED.EXE', '--command', f'ENDED.EXE {name}'],
                             [f'0:wait=Ended|{ready}', '1:tops=1', '1.5:pause=1', '1.6:wstate=save', '3:menu=quit'],
                             ['1:tops=1', '1.5:pause=0', '2.5:tops=1', '3:menu=quit'])
        what = ('the process that started last has ended' if name == 'child' else
                'a thread ended inside a callback and its handle is open')
        check(f'{name}: the machine is saved after {what}',
              f'NEO98 HOST input wait=Ended|{ready} reached' in before and saved(before) and not failures(before))
        check(f'{name}: the state loads in a new host and its window stands',
              loaded(after) and visible(after, 'Ended', ready) and not failures(after))

    if 'explorer' in cases:
        # The Run box's thread goes on for a while after Notepad has
        # started; a save waits (each step at most two seconds) until it has
        # ended, and the last state that could be written counts.
        saves = [f'{at}:wstate=save' for at in (14, 18, 22, 26, 30, 34)]
        before, after = case('explorer', ['--exe', 'C:\\WINDOWS\\EXPLORER.EXE'],
                             ['0:wait=Shell_TrayWnd', '3:dblclick=30,20', '4:wait=CabinetWClass',
                              '6:click=20,590', '7:type=r', '8:wait=|Run', '9:type=notepad', '9.5:key=0d',
                              '10:wait=Notepad', '12:close=Untitled - Notepad', '13:tops=1', *saves, '36:menu=quit'],
                             ['1:tops=1', '1.5:pause=0', '2:click=20,590', '3:tops=1', '3.5:key=1b', '5:menu=quit'])
        numbers = saved(before) or {}
        check('explorer: the shell is saved after a program it started has ended',
              'NEO98 HOST input wait=Notepad reached' in before and numbers.get('processes', 0) >= 3
              and 'state save failed' not in before and not failures(before))
        check('explorer: My Computer and the taskbar stand after the load',
              loaded(after) and visible(after, 'CabinetWClass', 'My Computer') and visible(after, 'Shell_TrayWnd'))
        # The Start menu of Windows 98 is a band in a BaseBar window, that
        # of Windows 95 (KERNEL32 4.00) a popup menu.
        menu = 'BaseBar' if kernel_version(pristine) >= (4, 10) else '#32768'
        menu_before, _, menu_after = after.partition('NEO98 HOST input click 20,590')
        check(f'explorer: the Start menu of the loaded shell opens ({menu})',
              not visible(menu_before, menu) and visible(menu_after, menu) and not failures(after))

    (WORK / 'results.json').write_text(json.dumps(results, indent=1) + '\n')
    print(f'Logs in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(results)} checks failed')
    print(f'state ended host: all {len(results)} checks passed', flush=True)


if __name__ == '__main__':
    main()
