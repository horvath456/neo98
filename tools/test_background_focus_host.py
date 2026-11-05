#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""SetFocus and SetActiveWindow of a background program leave the foreground window on top.

The autorun window of Plus! 98 calls SetFocus on one of its buttons whenever
the pointer moves over it. With the setup wizard of another process in the
foreground this brought the autorun window, which is larger, in front of the
wizard and hid it. Under Win98 the activation runs in the caller's own queue
 and ends in SetWindowPos(HWND_TOP), and Windows 98 answers HWND_TOP
for a queue that is not the foreground (and has no permission, ) with
the place behind the windows of the other tasks.

Two processes of one guest program:

- "Back" shows a large window with two buttons and a second top-level window
  "Back two", then starts itself again as "Front";
- "Front" shows a small window over the large one and takes the foreground,
  as every started program does;
- "Back" then calls, on a timer, SetFocus on a button of its first window
  (the activation is in its own queue), SetActiveWindow on its second window,
  BringWindowToTop and SetWindowPos(HWND_TOP) on its first. After each call
  the foreground window must still be Front's and Front must be the top
  window; its own queue has the new focus and active window.
- the pointer moves over "Back": WM_MOUSEMOVE sets the focus on the other
  button, as AUTORUN.EXE does; Front stays on top.
- a click on "Back" is input and brings it to the front.

    build/analysis-venv/bin/python tools/test_background_focus_host.py [--no-build]
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
I(U, SetFocus, U);
I(U, GetFocus, void);
I(U, SetActiveWindow, U);
I(U, GetActiveWindow, void);
I(U, GetForegroundWindow, void);
I(int, BringWindowToTop, U);
I(int, SetWindowPos, U, U, int, int, int, int, U);
I(U, GetTopWindow, U);
I(U, GetWindow, U, U);
I(U, FindWindowA, const char *, const char *);
I(int, IsWindowVisible, U);
I(int, SetWindowTextA, U, const char *);
I(U, SetTimer, U, U, U, void *);
I(int, KillTimer, U, U);
I(U, GetStockObject, int);
I(U, GetModuleHandleA, const char *);
I(const char *, GetCommandLineA, void);
I(U, GetModuleFileNameA, U, char *, U);
I(int, CreateProcessA, const char *, char *, void *, void *, int, U, void *, const char *, void *, void *);
I(void, ExitProcess, U);

static U back, two, one, other, front, phase, bad, hovered;

/* 1 when a comes before b in the order of the top-level windows. */
static int Above(U a, U b)
{
    U h;
    for (h = GetTopWindow(0); h; h = GetWindow(h, 2))
    {
        if (h == a) return 1;
        if (h == b) return 0;
    }
    return 0;
}
static int FrontStays(void)
{
    return GetForegroundWindow() == front && Above(front, back) && Above(front, two);
}
static void Name(U hwnd, const char *prefix, U value)
{
    char text[40];
    U n = 0;
    while (*prefix) text[n++] = *prefix++;
    text[n++] = "0123456789abcdef"[(value >> 4) & 15];
    text[n++] = "0123456789abcdef"[value & 15];
    text[n] = 0;
    SetWindowTextA(hwnd, text);
}
static U __stdcall Procedure(U hwnd, U message, U wparam, U lparam)
{
    if (message == 0x113 && hwnd == back)   /* WM_TIMER */
    {
        if (!front)
        {
            front = FindWindowA(0, "Front ready");
            if (!front || GetForegroundWindow() != front) return 0;
            if (!FrontStays()) bad |= 1;
            return 0;
        }
        switch (phase++)
        {
        case 0:   /* the focus, with the activation of its window in this queue */
            SetFocus(one);
            if (GetFocus() != one || GetActiveWindow() != back) bad |= 2;
            if (!FrontStays()) bad |= 4;
            break;
        case 1:   /* another window of this queue becomes its active window */
            SetActiveWindow(two);
            if (GetActiveWindow() != two) bad |= 8;
            if (!FrontStays()) bad |= 16;
            if (!Above(two, back)) bad |= 32;
            break;
        case 2:
            BringWindowToTop(back);
            if (!FrontStays()) bad |= 64;
            if (!Above(back, two)) bad |= 32;
            break;
        case 3:
            SetWindowPos(two, 0, 0, 0, 0, 0, 3);
            if (!FrontStays()) bad |= 128;
            if (!Above(two, back)) bad |= 32;
            break;
        case 4:
            KillTimer(back, 1);
            Name(back, "Back phases ", bad);
            break;
        }
        return 0;
    }
    if (message == 0x200 && hwnd == back && phase > 4 && !hovered)   /* WM_MOUSEMOVE */
    {
        hovered = 1;
        SetFocus(other);
        Name(back, "Back hover ", (GetFocus() == other ? 0 : 1) | (GetActiveWindow() == back ? 0 : 2) |
                                 (FrontStays() ? 0 : 4));
        return 0;
    }
    if (message == 0x201 && hwnd == back)   /* WM_LBUTTONDOWN */
    {
        Name(back, "Back clicked ", (GetForegroundWindow() == back ? 0 : 1) | (Above(back, front) ? 0 : 2));
        return 0;
    }
    if (message == 2 && (hwnd == back || (!back && !two))) ExitProcess(0);
    return DefWindowProcA(hwnd, message, wparam, lparam);
}
void __stdcall mainCRTStartup(void)
{
    WNDCLASSA c = {0};
    MSG message;
    const char *line = GetCommandLineA();
    int is_front = 0;
    while (*line) { if (line[0] == ' ' && line[1] == 'f') is_front = 1; ++line; }
    c.proc = Procedure;
    c.instance = GetModuleHandleA(0);
    c.brush = GetStockObject(is_front ? 0 : 2);
    c.name = "BgFocus";
    RegisterClassA(&c);
    if (is_front)
    {
        U window = CreateWindowExA(0, "BgFocus", "Front", 0x00cf0000, 120, 110, 200, 120, 0, 0, c.instance, 0);
        ShowWindow(window, 1);
        UpdateWindow(window);
        SetWindowTextA(window, "Front ready");
    }
    else
    {
        static char name[300];
        static U startup[17], information[4];
        U n;
        back = CreateWindowExA(0, "BgFocus", "Back", 0x00cf0000, 40, 40, 400, 300, 0, 0, c.instance, 0);
        one = CreateWindowExA(0, "BUTTON", "One", 0x50000000, 10, 200, 80, 24, back, 1, c.instance, 0);
        other = CreateWindowExA(0, "BUTTON", "Other", 0x50000000, 100, 200, 80, 24, back, 2, c.instance, 0);
        two = CreateWindowExA(0, "BgFocus", "Back two", 0x00cf0000, 330, 250, 200, 120, 0, 0, c.instance, 0);
        ShowWindow(two, 1);
        ShowWindow(back, 1);
        UpdateWindow(back);
        n = GetModuleFileNameA(0, name, 280);
        name[n++] = ' '; name[n++] = 'f'; name[n] = 0;
        startup[0] = sizeof startup;
        if (!CreateProcessA(0, name, 0, 0, 0, 0, 0, 0, startup, information)) SetWindowTextA(back, "Back no process");
        SetTimer(back, 1, 200, 0);
    }
    while (GetMessageA(&message, 0, 0, 0) > 0) DispatchMessageA(&message);
    ExitProcess(0);
}
'''

IMPORTS = {
    'USER32': ['RegisterClassA@4', 'CreateWindowExA@48', 'DefWindowProcA@16', 'GetMessageA@16', 'DispatchMessageA@4',
               'ShowWindow@8', 'UpdateWindow@4', 'SetFocus@4', 'GetFocus@0', 'SetActiveWindow@4', 'GetActiveWindow@0',
               'GetForegroundWindow@0', 'BringWindowToTop@4', 'SetWindowPos@28', 'GetTopWindow@4', 'GetWindow@8',
               'FindWindowA@8', 'IsWindowVisible@4', 'SetWindowTextA@8', 'SetTimer@16', 'KillTimer@8'],
    'GDI32': ['GetStockObject@4'],
    'KERNEL32': ['GetModuleHandleA@4', 'GetCommandLineA@0', 'GetModuleFileNameA@12', 'CreateProcessA@40',
                 'ExitProcess@4'],
}


def build_guest(out, env):
    (out / 'bgfocus.c').write_text(SOURCE)
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
                    '-fno-stack-protector', '-c', str(out / 'bgfocus.c'), '-o', str(out / 'bgfocus.obj')],
                   env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/nodefaultlib', '/machine:x86',
                    '/timestamp:0', '/out:' + str(drive / 'BGFOCUS.EXE'), str(out / 'bgfocus.obj'), *libraries],
                   env=env, check=True)
    return drive


def main():
    out = neo98.output_directory('background-focus-host')
    env = neo98.tool_environment()
    drive = build_guest(out, env)
    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', '-c', '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    # Back lies at 40,40 with 400 by 300, Front at 120,110 with 200 by 120 and
    # Back two at 330,250: the point 70,300 is on Back alone.
    steps = ['0:wait=|Front ready', '0:wait=|Back phases 00', '1:move=70,300', '1:move=72,302',
             '1:wait=|Back hover 00', '2:click=70,300', '2:wait=|Back clicked 00', '3:menu=quit']
    run_env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
                   NEO98_HOST_DEADLINE='60')
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        run_env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'D:\\BGFOCUS.EXE', '--quiet',
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
        'the second process took the foreground': waits.get('Front ready') == 'reached',
        'SetFocus, SetActiveWindow, BringWindowToTop and HWND_TOP in the background leave Front on top':
            waits.get('Back phases 00') == 'reached',
        'SetFocus on WM_MOUSEMOVE in the background leaves Front on top': waits.get('Back hover 00') == 'reached',
        'a click brings the background window to the front': waits.get('Back clicked 00') == 'reached',
        'no hang reported': 'NEO98 HOST hang begin' not in text,
        'host ended': code == 0,
    }
    (out / 'results.json').write_text(json.dumps({'log': str(log_path), 'exit_code': code, 'checks': checks},
                                                 indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'BACKGROUND FOCUS FAIL; see {log_path}')
        return 1
    print('BACKGROUND FOCUS PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
