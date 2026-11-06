#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""SetForegroundWindow does not wait for a thread that sleeps in KERNEL32.

DIRECTDB.DLL of Outlook Express compacts a store on a second thread. That
thread shows the dialog with the progress bar and calls SetForegroundWindow on
it, while the first thread, which owns the active window (the splash screen),
waits for the second thread's event with WaitForSingleObject(INFINITE). Under
Win98 the change of the other queue is left in that queue and its
thread carries it out when it next reads messages. NEO98 sent WM_NCACTIVATE to
the sleeping thread and waited for the answer: both threads stood, and every
window of the program stayed unpainted.

The guest program does the same with two windows:

- thread 1 shows "First", starts thread 2 and waits for its event, INFINITE;
- thread 2 shows "Second", calls SetForegroundWindow on it, names the window
  "Second foreground" when GetForegroundWindow answers it, and sets the event;
- thread 1 then reads its messages: WM_ACTIVATEAPP(FALSE) and WM_KILLFOCUS are
  there, and it names its window "First heard".

    build/analysis-venv/bin/python tools/test_foreground_sleeping_thread_host.py [--no-build]
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
win98_c.use_pristine()  # tests start from setup's C:, never the user's

SOURCE = r'''
typedef unsigned U;
typedef struct { U style; void *proc; int a, b; U instance, icon, cursor, brush; const char *menu, *name; } WNDCLASSA;
typedef struct { U hwnd, message, wparam, lparam, time; int x, y; } MSG;
#define I(r, n, ...) __declspec(dllimport) r __stdcall n(__VA_ARGS__)
I(U, RegisterClassA, const WNDCLASSA *);
I(U, CreateWindowExA, U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
I(U, DefWindowProcA, U, U, U, U);
I(int, GetMessageA, MSG *, U, U, U);
I(U, DispatchMessageA, const MSG *);
I(int, ShowWindow, U, int);
I(int, UpdateWindow, U);
I(int, SetForegroundWindow, U);
I(U, GetForegroundWindow, void);
I(int, SetWindowTextA, U, const char *);
I(U, GetStockObject, int);
I(U, GetModuleHandleA, const char *);
I(U, CreateEventA, void *, int, int, const char *);
I(int, SetEvent, U);
I(U, WaitForSingleObject, U, U);
I(U, CreateThread, void *, U, void *, void *, U, U *);
I(void, ExitProcess, U);

static U first, second, done, heard;

static U __stdcall Procedure(U hwnd, U message, U wparam, U lparam)
{
    if (hwnd == first && message == 0x1c && !wparam) heard |= 1;   /* WM_ACTIVATEAPP(FALSE) */
    if (hwnd == first && message == 8) heard |= 2;                 /* WM_KILLFOCUS */
    if (hwnd == first && heard == 3) { heard = 7; SetWindowTextA(first, "First heard"); }
    if (message == 2 && hwnd == first) ExitProcess(0);
    return DefWindowProcA(hwnd, message, wparam, lparam);
}
static U __stdcall Worker(void *unused)
{
    MSG message;
    (void)unused;
    second = CreateWindowExA(0, "FgSleep", "Second", 0x00cf0000, 260, 120, 220, 120, 0, 0, GetModuleHandleA(0), 0);
    ShowWindow(second, 1);
    SetForegroundWindow(second);
    if (GetForegroundWindow() == second) SetWindowTextA(second, "Second foreground");
    SetEvent(done);
    while (GetMessageA(&message, 0, 0, 0) > 0) DispatchMessageA(&message);
    return 0;
}
void __stdcall mainCRTStartup(void)
{
    WNDCLASSA c = {0};
    MSG message;
    U id;
    c.proc = Procedure;
    c.instance = GetModuleHandleA(0);
    c.brush = GetStockObject(0);
    c.name = "FgSleep";
    RegisterClassA(&c);
    first = CreateWindowExA(0, "FgSleep", "First", 0x00cf0000, 20, 20, 220, 120, 0, 0, c.instance, 0);
    ShowWindow(first, 1);
    UpdateWindow(first);
    done = CreateEventA(0, 0, 0, 0);
    CreateThread(0, 0, Worker, 0, 0, &id);
    WaitForSingleObject(done, 0xffffffffu);
    while (GetMessageA(&message, 0, 0, 0) > 0) DispatchMessageA(&message);
    ExitProcess(0);
}
'''

IMPORTS = {
    'USER32': ['RegisterClassA@4', 'CreateWindowExA@48', 'DefWindowProcA@16', 'GetMessageA@16', 'DispatchMessageA@4',
               'ShowWindow@8', 'UpdateWindow@4', 'SetForegroundWindow@4', 'GetForegroundWindow@0',
               'SetWindowTextA@8'],
    'GDI32': ['GetStockObject@4'],
    'KERNEL32': ['GetModuleHandleA@4', 'CreateEventA@16', 'SetEvent@4', 'WaitForSingleObject@8', 'CreateThread@24',
                 'ExitProcess@4'],
}


def build_guest(out, env):
    (out / 'fgsleep.c').write_text(SOURCE)
    drive = out / 'drive-d'
    drive.mkdir(exist_ok=True)
    libraries = []
    for dll, names in IMPORTS.items():
        definition, library = out / f'{dll}.def', out / f'{dll}.lib'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' + ''.join(name + '\n' for name in names))
        subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)],
                       env=env, check=True)
        libraries.append(str(library))
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-c', str(out / 'fgsleep.c'), '-o', str(out / 'fgsleep.obj')],
                   env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/nodefaultlib', '/machine:x86',
                    '/timestamp:0', '/out:' + str(drive / 'FGSLEEP.EXE'), str(out / 'fgsleep.obj'), *libraries],
                   env=env, check=True)
    return drive


def main():
    out = neo98.output_directory('foreground-sleeping-thread-host')
    env = neo98.tool_environment()
    drive = build_guest(out, env)
    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    steps = ['0:wait=|Second foreground', '0:wait=|First heard', '0:idle=300', '0:windows=1', '1:menu=quit']
    run_env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
                   NEO98_HOST_DEADLINE='120')
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        run_env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'D:\\FGSLEEP.EXE', '--quiet',
               '--drive-c', str(drive_c), '--drive', f'D={drive}', '--no-registry-file',
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        try:
            code = subprocess.run(command, cwd=ROOT, env=run_env, stdout=log, stderr=subprocess.STDOUT,
                                  stdin=subprocess.DEVNULL, timeout=180).returncode
        except subprocess.TimeoutExpired:
            code = 'timeout'
    shutil.rmtree(drive_c, ignore_errors=True)
    text = log_path.read_text(errors='replace')
    waits = dict(re.findall(r'^NEO98 HOST input wait=\|(.+) (reached|timed out) after', text, re.M))
    checks = {
        'the second thread got the foreground and went on': waits.get('Second foreground') == 'reached',
        'the first thread heard WM_ACTIVATEAPP and WM_KILLFOCUS after its wait': waits.get('First heard') == 'reached',
        'no hang reported': 'NEO98 HOST hang begin' not in text,
        'host ended': code == 0,
    }
    (out / 'results.json').write_text(json.dumps({'log': str(log_path), 'exit_code': code, 'checks': checks},
                                                 indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'FOREGROUND SLEEPING THREAD FAIL; see {log_path}')
        return 1
    print('FOREGROUND SLEEPING THREAD PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
