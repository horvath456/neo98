#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""native ListView nonclient rectangles and a control audit.

Run through a shared serial test lock. No visible window or sound.
"""
import argparse
import json
import re
import shutil
from pathlib import Path
import subprocess
import sys
sys.path[:0] = [str(Path(__file__).resolve().parent), str(Path(__file__).resolve().parents[1])]
import neo98
import win98_c
from native_app import ROOT, build_host
from test_device_manager_host import host

IMPORTS = {
    'KERNEL32': {'GetModuleHandleA': 4, 'ExitProcess': 4, 'CreateFileA': 28, 'CloseHandle': 4, 'WriteFile': 20, 'LoadLibraryA': 4, 'GetProcAddress': 8},
    'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DestroyWindow': 4, 'GetWindowLongA': 8,
               'SetWindowLongA': 12, 'DefWindowProcA': 16, 'SendMessageA': 16,
               'GetClientRect': 8, 'GetWindowRect': 8, 'SetWindowPos': 28,
               'SetScrollInfo': 16, 'ShowScrollBar': 12, 'GetMessageA': 16, 'TranslateMessage': 4, 'DispatchMessageA': 4},
}


def build_fixture(out, original=False, interactive=False, audit=False):
    out.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    def run(args):
        subprocess.run(list(map(str, args)), env=env, check=True)
    libs = []
    for dll, entries in IMPORTS.items():
        definition, lib = out / f'{dll}.def', out / f'{dll}.lib'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' + ''.join(f'{n}@{b}\n' for n, b in entries.items()))
        run(['llvm-dlltool', '-m', 'i386', '-k', '-d', definition, '-l', lib])
        libs.append(lib)
    obj, exe = out / 'fixture.obj', out / 'LISTNC.EXE'
    run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
         '-fno-stack-protector', *(['-DORIGINAL'] if original else []), *(['-DINTERACTIVE'] if interactive else []), *(['-DAUDIT'] if audit else []), '-c', ROOT / 'tools/listview_nc_fixture.c', '-o', obj])
    run(['lld-link', '/entry:mainCRTStartup', '/subsystem:console', '/timestamp:0', '/nodefaultlib',
         '/machine:x86', '/out:' + str(exe), obj, *libs])
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--interactive', action='store_true')
    parser.add_argument('--audit', action='store_true', help='Include the other native controls')
    parser.add_argument('--original', action='store_true', help='Run the same contract with original COMCTL32')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/listview-nc-host')
    args = parser.parse_args()
    out = args.out.resolve()
    binary = ROOT / 'build/native-app/neo98-host' if args.no_build else build_host()
    exe = build_fixture(out / 'fixture', args.original, args.interactive, args.audit)
    steps = []
    if args.interactive:
        steps = ['0:wait=STATIC|NC contract', '0:idle=1000']
        def capture(name):
            steps.extend(['0:move=350,250', '0:idle=300', '0:lists=1', f'0:shot={{out}}/{name}.ppm'])
        capture('top')
        steps += ['0:click=240,165']; capture('arrow')
        steps += ['0:press=240,47','0:idle=200','0:move=240,145','0:idle=200','0:release=240,145']; capture('thumb')
        steps += ['0:press=240,145','0:idle=200','0:move=240,20','0:idle=200','0:release=240,20']; capture('back')
        steps += ['0:menu=quit']
    code, log, run = host(binary, out, 'contract', r'D:\LISTNC.EXE', 'LISTNC.EXE', steps, 30,
                         win98_c.use_pristine(), drives=['D=' + str(exe.parent)], keep=True)
    guest_log = run/'c/NCRESULT.TXT'
    if guest_log.exists():
        log += guest_log.read_text(errors='replace')
        (run/'log.txt').write_text(log)
    shutil.rmtree(run/'c')
    lines = [line for line in log.splitlines() if line.startswith(('PASS ', 'FAIL '))]
    checks = {'host completed': code == 0 and 'NC fixture complete' in log,
              'all assertions pass': bool(lines) and not any(line.startswith('FAIL ') for line in lines)}
    first = []
    if args.interactive:
        from PIL import Image
        first = [int(x) for x in re.findall(r'list hwnd=[0-9a-f]+ rows=40 columns=1 first=(\d+)', log)]
        checks = {'host completed': code == 0 and 'timed out' not in log,
                  'four list snapshots': len(first) == 4}
        if len(first) == 4:
            checks.update({'initial row zero': first[0] == 0, 'arrow advances': first[1] > 0,
                           'thumb advances further': first[2] > first[1], 'thumb returns to top': first[3] == 0})
        frames = {}
        for p in run.glob('*.ppm'):
            frames[p.stem] = Image.open(p).convert('RGB')
            frames[p.stem].save(p.with_suffix('.png'))
        checks['four screenshots'] = len(frames) == 4
        if 'top' in frames:
            client = frames['top'].crop((20, 25, 225, 169)).getcolors(40000) or []
            arrows = [frames['top'].crop(box).getcolors(100) or []
                      for box in ((236, 24, 245, 33), (236, 159, 245, 169))]
            checks['list client is painted'] = sum(n for n, c in client if c == (255, 255, 255)) > 20000
            checks['list rows are painted'] = sum(n for n, c in client if c == (0, 0, 0)) > 100
            checks['scrollbar arrows are painted'] = all(sum(n for n, c in colors if c == (0, 0, 0)) > 4 for colors in arrows)
    result = {'checks': checks, 'first_rows': first, 'passed': sum(line.startswith('PASS ') for line in lines),
              'total': len(lines), 'assertions': lines, 'log': str(run / 'log.txt')}
    (out / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return int(not all(checks.values()))


if __name__ == '__main__':
    raise SystemExit(main())
