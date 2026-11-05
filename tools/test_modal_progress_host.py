#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""An open message box that follows the mouse is no hang (host title).

The host's hang observation counts API calls and other turns of the guest
(tools/host_pacing.h, TotalCalls). A modal loop of USER, here MessageBoxA,
retrieves and dispatches its messages without an API call of the program; the
title said "haengt seit 3 s" as soon as the pointer moved over the box
(Clickomania's error box, entry). Win98 has no such notion; the box
answers the mouse. Each message the modal loop retrieves now counts as a turn
(Application::ModalMessage).

A small guest program calls MessageBoxA and nothing else. After it is up the
script moves the pointer over the box for ten seconds; the host log must not
report "hang begin". The counter check: a program that spins without any API
call is reported within a few seconds.

    build/analysis-venv/bin/python tools/test_modal_progress_host.py [--no-build]
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

SOURCE = r'''
__declspec(dllimport) int __stdcall MessageBoxA(unsigned, const char *, const char *, unsigned);
__declspec(dllimport) void __stdcall ExitProcess(unsigned);
void __stdcall mainCRTStartup(void)
{
    MessageBoxA(0, "Modal loop progress", "NEO98 test", 0);
    ExitProcess(0);
}
void __stdcall spinCRTStartup(void)
{
    for (volatile unsigned n = 0;; ++n) {}
}
'''


def build_guest(out, env):
    (out / 'msgbox.c').write_text(SOURCE)
    (out / 'USER32.def').write_text('LIBRARY USER32.DLL\nEXPORTS\nMessageBoxA@16\n')
    (out / 'KERNEL32.def').write_text('LIBRARY KERNEL32.DLL\nEXPORTS\nExitProcess@4\n')
    drive = out / 'drive-d'
    drive.mkdir(exist_ok=True)
    for command in (['llvm-dlltool', '-m', 'i386', '-k', '-d', out / 'USER32.def', '-l', out / 'user32.lib'],
                    ['llvm-dlltool', '-m', 'i386', '-k', '-d', out / 'KERNEL32.def', '-l', out / 'kernel32.lib'],
                    ['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                     '-fno-stack-protector', '-c', out / 'msgbox.c', '-o', out / 'msgbox.obj'],
                    ['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/nodefaultlib', '/machine:x86',
                     '/timestamp:0', '/out:' + str(drive / 'MSGBOX.EXE'), out / 'msgbox.obj',
                     out / 'user32.lib', out / 'kernel32.lib'],
                    ['lld-link', '/entry:spinCRTStartup', '/subsystem:windows', '/nodefaultlib', '/machine:x86',
                     '/timestamp:0', '/out:' + str(drive / 'SPIN.EXE'), out / 'msgbox.obj',
                     out / 'user32.lib', out / 'kernel32.lib']):
        subprocess.run(list(map(str, command)), env=env, check=True)
    return drive


def main():
    out = neo98.output_directory('modal-progress-host')
    env = neo98.tool_environment()
    drive = build_guest(out, env)
    # A writable C: of its own: an APFS clone of the real Win98 folder.
    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', '-c', '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    # The box stands in the middle of the 800x600 screen. The pointer walks
    # over it twice a second from 12 s to 24 s; the hang indicator fires after
    # 3 s without progress once input came after the last call.
    steps = [f'{12 + i / 2:.1f}:move={330 + (i % 8) * 20},{280 + (i % 3) * 6}' for i in range(24)]
    shot = out / 'box.ppm'
    shot.unlink(missing_ok=True)
    steps.append(f'24.5:shot={shot}')
    run_env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_MENU='27:quit')
    run_env.pop('NEO98_HOST_ATTACHED', None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'D:\\MSGBOX.EXE', '--quiet',
               '--drive-c', str(drive_c), '--drive', f'D={drive}', '--no-registry-file',
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=run_env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=300)
    text = log_path.read_text(errors='replace')
    spin_env = dict(run_env, NEO98_HOST_INPUT='', NEO98_HOST_MENU='12:quit')
    spin_command = [part if part != 'D:\\MSGBOX.EXE' else 'D:\\SPIN.EXE' for part in command]
    spin_log = out / 'spin.log'
    with spin_log.open('w') as log:
        spin = subprocess.run(spin_command, cwd=ROOT, env=spin_env, stdout=log, stderr=subprocess.STDOUT,
                              stdin=subprocess.DEVNULL, timeout=300)
    spin_text = spin_log.read_text(errors='replace')
    shutil.rmtree(drive_c, ignore_errors=True)
    checks = {
        'pointer moved over the box': text.count('NEO98 HOST input move') >= 20,
        'screenshot written': shot.exists(),
        'no hang reported while the box follows the mouse': 'NEO98 HOST hang begin' not in text,
        'host ended': result.returncode == 0,
        'a program that spins is reported': 'NEO98 HOST hang begin' in spin_text,
    }
    report = {'log': str(log_path), 'exit_code': result.returncode, 'checks': checks}
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'MODAL PROGRESS FAIL; see {log_path}')
        return 1
    print('MODAL PROGRESS PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
