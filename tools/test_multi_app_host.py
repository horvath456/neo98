#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Several programs at once and a change of the display mode.

Explorer is the shell, the host runs headless (SDL's dummy driver,
NEO98_HOST_SCREEN=1100x700) with --seamless, and the programs start the way a
double click starts them. tools/multi_app_ddraw_fixture.c is a DirectDraw
program that writes the primary surface through the pointer of its Lock.

four Notepad, Calculator and two DirectDraw programs in a window that draw
       on the primary surface; then a fifth takes the exclusive mode and
       640x480x16, draws, and ends in that mode. The host goes on, no process
       failed, every other program got WM_DISPLAYCHANGE, the two in a window
       draw again afterwards, and the desktop is back.
two Two DirectDraw programs that both want the exclusive mode, one after
       the other and at once, opened before either takes exclusive mode.
       What the original DDRAW does with them: the one
       that loses the foreground is minimized and the desktop's mode comes
       back, the other takes the exclusive mode; the surfaces of the resting
       one are lost (Restore answers DDERR_WRONGMODE) until it is chosen
       again, then it has its mode back and draws, and the other rests.
crash The same as `four`, but the fifth crashes in its mode.
handover Two DirectDraw programs in a window; the one that asked for the
       address of the screen first ends, the other draws on, takes a mode of
       its own and ends in it (the host died here with SIGBUS).

    build/analysis-venv/bin/python tools/test_multi_app_host.py [--no-build] [--only NAME] [--binary PATH]

Results in build/multi-app-host/results.json.
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
import first_logon
win98_c.use_pristine()  # tests start from setup's C:, never the user's

IMPORTS = {'KERNEL32': {'ExitProcess': 4, 'GetModuleHandleA': 4},
           'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16, 'GetMessageA': 16,
                      'DispatchMessageA': 4, 'SetWindowTextA': 8, 'SetTimer': 16},
           'GDI32': {'GetStockObject': 4}, 'DDRAW': {'DirectDrawCreate': 12}}
SCREEN = (1100, 700)
NAMES = 'ABCD'


def fixtures(out):
    environment = neo98.tool_environment()
    dlltool = shutil.which('llvm-dlltool', path=environment['PATH'])
    assert dlltool, 'llvm-dlltool not found'
    libraries = []
    for dll, names in IMPORTS.items():
        definition, library = out / (dll + '.def'), out / (dll + '.lib')
        definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' +
                              ''.join(f'{n}@{b}\n' for n, b in names.items()))
        subprocess.run([dlltool, '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)],
                       env=environment, check=True)
        libraries.append(str(library))
    programs = {}
    for index, name in enumerate(NAMES):
        obj, exe = out / f'MULTI{name}.obj', out / f'MULTI{name}.EXE'
        subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                        '-fno-stack-protector', f"-DNAME='{name}'", f'-DCOLOUR={0x33 + 0x22 * index}',
                        '-c', str(ROOT / 'tools/multi_app_ddraw_fixture.c'), '-o', str(obj)],
                       env=environment, check=True)
        subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0',
                        '/nodefaultlib', '/machine:x86', '/out:' + str(exe), str(obj), *libraries],
                       env=environment, check=True)
        programs[name] = exe
    return programs


def run(binary, out, name, steps, programs_folder, extra_env=None, prepare=None):
    drive = out / f'drive-c-{name}'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
    # Start after RunOnce/Active Setup, with the corresponding registry.
    # A pristine tree without its journal starts installation again.
    logged_on, journal = first_logon.session()
    # -f: a read-only file of the first copy is replaced, not refused.
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto', '-f']), '-R', str(logged_on) + '/.', str(drive)], check=True)
    registry = out / f'{name}.registry'
    shutil.copyfile(journal, registry)
    if prepare:
        prepare(drive)
    reports = out / f'reports-{name}'
    shutil.rmtree(reports, ignore_errors=True)
    steps = [s.replace('{C}', str(drive)) for s in steps]
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
               '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--quiet', '--drive-c', str(drive), '--registry', str(registry),
               '--drive-readonly', f'P={programs_folder}', '--crash-report-dir', str(reports), '--seamless']
    deadline = int(os.environ.get('NEO98_MULTI_APP_DEADLINE', '900'))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_HOST_SCREEN='%dx%d' % SCREEN, NEO98_HOST_DEADLINE=str(deadline),
               NEO98_SETTINGS_DIR=str(out/'settings'), NEO98_LOG_DIR=str(out/'logs'), **(extra_env or {}))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    log_path = out / f'{name}.log'
    with log_path.open('w') as log:
        process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL)
        try:
            code = process.wait(timeout=deadline + 60)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=20)
            code = 'timeout'
    report_names = sorted(p.name for p in reports.iterdir()) if reports.exists() else []
    shutil.rmtree(drive, ignore_errors=True)
    if not os.environ.get('NEO98_MULTI_APP_KEEP'):
        shutil.rmtree(reports, ignore_errors=True)
    return code, log_path.read_text(errors='replace'), report_names


def numbered(steps):
    """Give the steps rising times, one second apart; `pause=N` adds N seconds.

    The clock of the script stands still during a wait, so a pause counts from
    the moment the wait before it was met. (`idle=` is of no use here: a
    program that draws by a timer is never idle.)
    """
    result, at = [], 0
    for step in steps:
        if step.startswith('pause='):
            at += int(step[6:])
            continue
        result.append(f'{at}:{step}')
        at += 1
    return result


def waits(text):
    return re.findall(r'^NEO98 HOST input wait=(\S.*) (reached|timed out) after', text, re.M)


def common(code, text, reports):
    w = waits(text)
    return {
        'host ended by its menu, not by a signal': code == 0 and 'menu-script quit ok' in text,
        'every wait reached': bool(w) and all(state == 'reached' for _, state in w),
        'no process failed over the screen surface': 'DIB section mapping release failed' not in text,
        'no crash report but the expected ones': True,
    }, w


def start_four(programs):
    """Notepad, Calculator and the two DirectDraw programs in a window."""
    return ['wait=Shell_TrayWnd', 'idle=3000',
            'open={C}/WINDOWS/NOTEPAD.EXE', 'wait=Notepad', 'open={C}/WINDOWS/CALC.EXE', 'wait=SciCalc',
            f'open={programs["C"]}', 'wait=|C ready', 'key=57', 'wait=|C windowed drawing',
            f'open={programs["D"]}', 'wait=|D ready', 'key=57', 'wait=|D windowed drawing']


def scenario_four(binary, out, programs, crash=False):
    name = 'crash' if crash else 'four'
    steps = start_four(programs) + [
        f'open={programs["A"]}', 'wait=|A ready', 'key=45', 'wait=|A exclusive drawing', 'pause=2',
        # While A has the screen in another mode the two primary surfaces are
        # lost and cannot be restored (DDERR_WRONGMODE), as under Win98.
        'wait=|C windowed 8876024b lost', 'wait=|D windowed 8876024b lost',
        'key=4b' if crash else 'key=58',
        # After its end they restore themselves and draw again.
        'wait=|C windowed drawing', 'wait=|D windowed drawing', 'pause=2', 'windows=1',
        'choose=C windowed drawing', 'pause=1', 'key=43', 'wait=|C changes 1',
        # D lies at 220,160 with 320 by 240; this point is on it alone.
        'click=520,390', 'pause=1', 'key=43', 'wait=|D changes 1',
        'wait=Notepad', 'wait=SciCalc', 'windows=1', 'menu=quit']
    code, text, reports = run(binary, out, name, numbered(steps), programs['A'].parent)
    checks, _ = common(code, text, reports)
    overlays = re.findall(r'^NEO98 HOST seamless overlay=(\d) .*\((.*)\)', text, re.M)
    unexpected = [r for r in reports if not (crash and 'c0000005' in r)]
    checks['no crash report but the expected ones'] = not unexpected
    checks['the program mode took the overlay and gave it back'] = \
        [o[0] for o in overlays][-2:] == ['0', '1'] and ('0', 'program mode') in overlays
    checks['the screen is the desktop again'] = \
        re.search(r'overlay=1 window=%dx%d' % SCREEN, text.split('wait=|A exclusive drawing reached')[-1]) is not None
    return checks


def scenario_two(binary, out, programs):
    lost = 'exclusive 8876024b lost'   # Restore answers DDERR_WRONGMODE while the other has its mode
    steps = ['pause=3', 'button=OK', 'wait=Shell_TrayWnd', 'idle=3000',
             # One after the other: A takes the mode and gives it back, B takes it and ends in it.
             f'open={programs["A"]}', 'wait=|A ready', 'key=45', 'wait=|A exclusive drawing',
             'key=52', 'wait=|A normal',
             f'open={programs["B"]}', 'wait=|B ready', 'key=45', 'wait=|B exclusive drawing', 'key=58', 'pause=4',
             # Both programs must be open before taking the mode: serve_opens
             # deliberately refuses host launches during exclusive mode.
             f'open={programs["B"]}', 'wait=|B ready',
             'choose=A normal', 'pause=1', 'key=45', 'wait=|A exclusive drawing',
             'choose=B ready', 'key=45', 'wait=|B exclusive drawing',
             f'wait=|A {lost}', 'windows=1',
             # A comes back when it is chosen: B rests minimized, A has its mode and draws.
             f'choose=A {lost}', 'wait=|A exclusive drawing', f'wait=|B {lost}', 'windows=1',
             # And the other way round.
             f'choose=B {lost}', 'wait=|B exclusive drawing', f'wait=|A {lost}', 'windows=1',
             # B ends in its mode; A is chosen, draws, and ends.
             'key=58', 'pause=4', f'choose=A {lost}', 'wait=|A exclusive drawing', 'key=58', 'pause=4',
             'windows=1', 'menu=quit']
    code, text, reports = run(binary, out, 'two', numbered(steps), programs['A'].parent)
    checks, _ = common(code, text, reports)
    checks['no crash report but the expected ones'] = not reports
    lists = re.findall(r'^NEO98 HOST seamless item id=\w+ (minimized )?frame=\S+ .* title=(.*)$', text, re.M)
    checks['the program without the mode rests minimized'] = \
        ('minimized ', f'A {lost}') in lists and ('minimized ', f'B {lost}') in lists
    checks['the screen is the desktop again at the end'] = \
        re.findall(r'^NEO98 HOST seamless overlay=(\d)', text, re.M)[-1:] == ['1']
    return checks


def scenario_handover(binary, out, programs):
    steps = ['wait=Shell_TrayWnd', 'idle=3000',
             f'open={programs["C"]}', 'wait=|C ready', 'key=57', 'wait=|C windowed drawing',
             f'open={programs["A"]}', 'wait=|A ready', 'key=57', 'wait=|A windowed drawing',
             # C was the first to ask for the screen's address and ends; A goes on drawing,
             # then takes a mode of its own and ends in it.
             'close=C windowed drawing', 'pause=4', 'windows=1', 'wait=|A windowed drawing',
             'key=45', 'wait=|A exclusive drawing', 'pause=2', 'key=58', 'pause=4', 'windows=1', 'menu=quit']
    code, text, reports = run(binary, out, 'handover', numbered(steps), programs['A'].parent)
    checks, _ = common(code, text, reports)
    checks['no crash report but the expected ones'] = not reports
    checks['the first program is gone'] = 'title=C ' not in text.split('NEO98 HOST input close')[-1]
    checks['the screen is the desktop again at the end'] = \
        re.findall(r'^NEO98 HOST seamless overlay=(\d)', text, re.M)[-1:] == ['1']
    return checks


def main():
    out = neo98.output_directory(os.environ.get('NEO98_MULTI_APP_OUT', 'multi-app-host'))
    for old in list(out.glob('*.log')):
        old.unlink()
    folder = out / 'programs'
    folder.mkdir(exist_ok=True)
    programs = fixtures(folder)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--binary' in sys.argv:
        binary = Path(sys.argv[sys.argv.index('--binary') + 1])
    elif '--no-build' not in sys.argv:
        binary = build_host()
    only = sys.argv[sys.argv.index('--only') + 1] if '--only' in sys.argv else None
    scenarios = {'four': lambda: scenario_four(binary, out, programs),
                 'crash': lambda: scenario_four(binary, out, programs, crash=True),
                 'two': lambda: scenario_two(binary, out, programs),
                 'handover': lambda: scenario_handover(binary, out, programs)}
    results, okay = {}, True
    for name, scenario in scenarios.items():
        if only and name != only:
            continue
        results[name] = scenario()
        for check, passed in results[name].items():
            print(f"{'ok  ' if passed else 'FAIL'} {name}: {check}")
            okay = okay and passed
    (out / 'results.json').write_text(json.dumps(results, indent=1))
    if not okay:
        print(f'MULTI APP FAIL; see {out}')
        return 1
    print('MULTI APP PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
