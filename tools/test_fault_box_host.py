#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The error box of an unhandled exception (src/x86_fault_box.inc).

The host runs Notepad from a clone of the pristine drive C: under SDL's dummy
video driver and opens tools/fault_box_fixture.c four times, the way a double
click does:

- FAULTP.EXE reads through a null pointer: the dialog of KERNEL32.DLL appears
  with the program's name as its title, Close ends the program;
- FAULTI.EXE runs an invalid instruction: the same dialog, Alt+D (Details>>) makes it
  grow, Enter closes it;
- FAULTQ.EXE set SEM_NOGPFAULTERRORBOX: no dialog, the program ends;
- FAULTF.EXE has a filter of its own that answers EXCEPTION_EXECUTE_HANDLER:
  the filter runs, no dialog, the program ends;
- FAULTW.EXE has a window whose procedure faults on every message: one
  dialog, not one per message;
- FAULTN.EXE asks for an NT service the port does not carry: the stop of the
  port (Fail) shows the same dialog, "an exception c0000002H", and the report
  keeps the reason of the stop.

Notepad keeps running throughout, the host writes a report for each of the
four, and the host ends in order.

    build/analysis-venv/bin/python tools/test_fault_box_host.py [--no-build]

Results in build/fault-box-host/results.json.
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
import arm64_run  # noqa: E402
import win98_c  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402

IMPORTS = {'KERNEL32': {'GetCommandLineA': 0, 'SetErrorMode': 4, 'SetUnhandledExceptionFilter': 4, 'CreateFileA': 28,
                        'WriteFile': 20, 'CloseHandle': 4, 'SetFilePointer': 16, 'ExitProcess': 4, 'GetModuleHandleA': 4, 'CreateThread': 24, 'Sleep': 4},
           'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16, 'GetMessageA': 16,
                      'DispatchMessageA': 4, 'MessageBoxA': 16, 'PostMessageA': 16}}


def build_fixture(out):
    env = neo98.tool_environment()
    out.mkdir(parents=True, exist_ok=True)

    def run(args):
        subprocess.run(list(map(str, args)), env=env, check=True)
    libs = []
    for dll, entries in IMPORTS.items():
        definition, lib = out / f'{dll}.def', out / f'{dll}.lib'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' + ''.join(f'{n}@{b}\n' for n, b in entries.items()))
        run(['llvm-dlltool', '-m', 'i386', '-k', '-d', definition, '-l', lib])
        libs.append(lib)
    obj, exe = out / 'fixture.obj', out / 'FAULT.EXE'
    run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
         '-c', ROOT / 'tools/fault_box_fixture.c', '-o', obj])
    run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0', '/nodefaultlib',
         '/machine:x86', '/out:' + str(exe), obj, *libs])
    return exe


def main():
    out = neo98.output_directory('fault-box-host')
    for name in ('drive-c', 'reports', 'fixture'):
        shutil.rmtree(out / name, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    pristine = win98_c.use_pristine()
    exe = build_fixture(out / 'fixture')
    drive_c = out / 'drive-c'
    subprocess.run(['cp', '-c', '-R', str(pristine) + '/', str(drive_c)], check=True)
    (drive_c / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    for letter in 'PIQFWN':
        shutil.copyfile(exe, drive_c / f'FAULT{letter}.EXE')
    steps = ['0:wait=Notepad', '1:idle=1000',
             f'1:open={drive_c / "FAULTP.EXE"}', '0:wait=#32770|Faultp', '1:idle=500', f'0:shot={out / "box.bmp"}',
             '1:key=0d', '1:idle=1500',
             f'1:open={drive_c / "FAULTI.EXE"}', '0:wait=#32770|Faulti', '1:idle=500',
             '1:keydown=12', '0:key=44', '0:keyup=12', '1:idle=500', f'0:shot={out / "details.bmp"}',
             '1:key=0d', '1:idle=1500',
             f'1:open={drive_c / "FAULTQ.EXE"}', '3:idle=1500',
             f'1:open={drive_c / "FAULTF.EXE"}', '3:idle=1500',
             f'1:open={drive_c / "FAULTW.EXE"}', '0:wait=#32770|Faultw', '1:idle=1500', '1:key=0d', '1:idle=1500',
             f'1:open={drive_c / "FAULTN.EXE"}', '0:wait=#32770|Faultn', '1:idle=500', '1:key=0d', '1:idle=1500',
             '0:wait=Notepad', '1:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_HOST_DEADLINE='110')
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_KEYMAP'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
               '--exe', 'C:\\WINDOWS\\NOTEPAD.EXE', '--drive-c', str(drive_c), '--no-registry-file',
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL)
        try:
            code = process.wait(timeout=130)
        except subprocess.TimeoutExpired:
            arm64_run.end(process)
            code = 'timeout'
    text = log_path.read_text(errors='replace')
    record = next((p for p in drive_c.iterdir() if p.name.upper() == 'FAULTBOX.TXT'), None)
    written = record.read_text(errors='replace') if record else ''
    boxes = re.findall(r'FAULT BOX (\w+): (.*)$', text, re.M)
    waits = re.findall(r'^NEO98 HOST input wait=(.*) (reached|timed out)', text, re.M)
    grown = re.search(r'FAULT BOX details height (\d+) to (\d+)', text)
    reports = re.findall(r'CRASHREPORT (\S+)', text)
    checks = {
        'null pointer: the dialog of KERNEL32 with the program as title':
            any(t == 'Faultp' and re.fullmatch(r'FAULTP caused an invalid page fault in module FAULTP\.EXE at '
                                                 r'[0-9a-f]{4}:[0-9a-f]{8}\.', m) for t, m in boxes),
        'invalid instruction: the dialog names it':
            any(t == 'Faulti' and re.fullmatch(r'FAULTI executed an invalid instruction in module FAULTI\.EXE at '
                                                 r'[0-9a-f]{4}:[0-9a-f]{8}\.', m) for t, m in boxes),
        'a stop of the port: the same dialog with STATUS_NOT_IMPLEMENTED':
            any(t == 'Faultn' and re.fullmatch(r'FAULTN caused an exception c0000002H in module FAULTN\.EXE at '
                                                 r'[0-9a-f]{4}:[0-9a-f]{8}\.', m) for t, m in boxes),
        'the report of that stop names the stop':
            any('NT service ffffh through int 2Eh is not ported yet' in
                (Path(r, 'report.md').read_text(errors='replace') if Path(r, 'report.md').exists() else '')
                for r in reports),
        'the dialogs appeared and Notepad is still there': len(waits) == 6 and all(s == 'reached' for _, s in waits),
        'Close ended each': text.count('FAULT BOX closed') == 4,
        'a window procedure that faults on every message: one fault, one box':
            sum(1 for t, _ in boxes if t == 'Faultw') == 1,
        'Details>> made the dialog grow': bool(grown) and int(grown.group(2)) > int(grown.group(1)) + 50,
        'SEM_NOGPFAULTERRORBOX and the own filter: no dialog': sum(1 for t, _ in boxes if t not in ('Faultw', 'Faultn')) == 2,
        'the own filter ran': 'f\nfilter called' in written,
        'every program started and none survived its fault':
            [line for line in written.splitlines() if line] == ['p', 'i', 'q', 'f', 'filter called', 'w', 'n'],
        'a report of the host for each': len(reports) == 6 and all(Path(r, 'report.json').exists() for r in reports),
        'the session went on and ended in order': code == 0 and 'NEO98 HOST failed' not in text,
    }
    shutil.rmtree(drive_c, ignore_errors=True)
    shutil.rmtree(out / 'reports', ignore_errors=True)
    (out / 'results.json').write_text(json.dumps({'checks': checks, 'boxes': boxes, 'written': written, 'log': str(log_path)}, indent=2) + '\n')
    failed = [name for name, okay in checks.items() if not okay]
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if failed:
        print(f'{len(failed)} of {len(checks)} checks failed; log {log_path}')
        return 1
    print(f'fault box: {len(checks)} checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
