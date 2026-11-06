#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Trackpad fractions and wheel bursts through SDL, USER, and guest callbacks."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import neo98
from native_app import build_host
import win98_c
import first_logon
win98_c.use_pristine()


def main():
    out = neo98.output_directory('host-wheel')
    env = neo98.tool_environment()
    libraries = []
    imports = {'KERNEL32': {'ExitProcess': 4}, 'USER32': {
        'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16,
        'SetFocus': 4, 'SetCapture': 4, 'GetMessageA': 16,
        'DispatchMessageA': 4, 'SetWindowTextA': 8, 'SetForegroundWindow': 4}}
    for dll, names in imports.items():
        definition, library = out / (dll + '.def'), out / (dll + '.lib')
        definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' +
                              ''.join(f'{name}@{size}\n' for name, size in names.items()))
        subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition),
                        '-l', str(library)], env=env, check=True)
        libraries.append(str(library))
    obj, exe = out / 'fixture.obj', out / 'WHEEL.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding',
                    '-fno-builtin', '-fno-stack-protector', '-c',
                    str(ROOT / 'tools/host_wheel_fixture.c'), '-o', str(obj)], env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0',
                    '/nodefaultlib', '/machine:x86', '/out:' + str(exe), str(obj), *libraries],
                   env=env, check=True)
    drive = out / 'drive-c'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
    registry = out / 'registry.bin'
    registry.unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    steps = ['0:wait=HOST_WHEEL|wheel ready', '0:move=500,400',
             '1:wheel=-0.25', '1.1:wheel=-0.25', '1.2:wheel=-0.25', '1.3:wheel=-0.25',
             '1.4:wait=HOST_WHEEL|wheel one', '2:wheel=1',
             '2.1:wait=HOST_WHEEL|wheel two', '3:wheel=3',
             '3.1:wait=HOST_WHEEL|wheel three', '4:keydown=10', '4:keydown=11', '4.1:wheel=-2']
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_DEADLINE='120',
               NEO98_HOST_MENU='10:quit')
    env.pop('NEO98_HOST_ATTACHED', None)
    with (out / 'host.log').open('w') as log:
        run = subprocess.run([str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                              '--system-tree', '--exe', str(exe), '--quiet', '--drive-c', str(drive),
                              '--registry', str(registry), '--crash-report-dir', str(out / 'reports')],
                             cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=150)
    text = (out / 'host.log').read_text(errors='replace')
    assert run.returncode == 0 and 'NEO98 HOST exited code=0 ' in text, text[-4000:]
    print('HOST WHEEL PASS: fractional trackpad, both directions, multi-notch wheel, focused child, parent forwarding, capture ignored, screen coordinates, Ctrl+Shift, filtered retrieval past key packets')
    explorer = out / 'explorer'
    drive, registry = first_logon.clone(explorer)
    shots = {name: (explorer / (name + '.ppm')).resolve() for name in ('before', 'after', 'restored', 'trackpad')}
    for shot in shots.values():
        shot.unlink(missing_ok=True)
    steps = ['0:wait=Shell_TrayWnd', '1:guestopen=C:\\WINDOWS\\SYSTEM',
             '2:wait=CabinetWClass', '3:click=120,439', '4:idle=3000',
             '5:click=400,300', '6:idle=1000', f'7:shot={shots["before"]}',
             '8:wheel=-3', '9:idle=1000', f'10:shot={shots["after"]}',
             '11:wheel=3', '12:idle=1000', f'13:shot={shots["restored"]}', '13.5:move=400,300']
    # The same way with a trackpad: nine steps of a line, which COMCTL32 adds up to three notches.
    steps += [f'{14 + i * 0.1:.1f}:scroll=-13.4,0' + (',begin' if not i else '') for i in range(9)]
    steps += ['15.5:idle=1000', f'16.5:shot={shots["trackpad"]}', '17:wheel=3']
    # The other views of the folder (View: Details, List, Small Icons; Alt is
    # 12h) and its tree of folders (View, Explorer Bar, Folders): a picture,
    # three notches down, a picture, three back, a picture.
    views = {}
    time = 18.0
    for name, keys in (('details', '44'), ('list', '4c'), ('small', '4d'), ('folders', '45,4f')):
        views[name] = [(explorer / f'{name}-{i}.ppm').resolve() for i in range(3)]
        for shot in views[name]:
            shot.unlink(missing_ok=True)
        steps += [f'{time:.1f}:keydown=12', f'{time + 0.1:.1f}:key=56', f'{time + 0.2:.1f}:keyup=12']
        steps += [f'{time + 0.6 + 0.4 * i:.1f}:key={key}' for i, key in enumerate(keys.split(','))]
        # The tree of folders is on the left; the pointer goes over it.
        steps += [f'{time + 3.0:.1f}:move={"150,330" if name == "folders" else "400,300"}', f'{time + 3.5:.1f}:shot={views[name][0]}',
                  f'{time + 4.0:.1f}:wheel=-3', f'{time + 5.5:.1f}:shot={views[name][1]}',
                  f'{time + 6.0:.1f}:wheel=3', f'{time + 7.5:.1f}:shot={views[name][2]}']
        time += 8.5
    env.update(NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_MENU=f'{int(time) + 15}:quit', NEO98_HOST_DEADLINE='180')
    with (explorer / 'host.log').open('w') as log:
        run = subprocess.run([str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                              '--system-tree', '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--quiet',
                              '--drive-c', str(drive), '--registry', str(registry),
                              '--crash-report-dir', str(explorer / 'reports')],
                             cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=240)
    assert run.returncode == 0

    def contents(path):
        magic, size, depth, body = path.read_bytes().split(b'\n', 3)
        assert (magic, size, depth) == (b'P6', b'800 600', b'255')
        # The original list view; omit the pointer and the surrounding shell.
        return b''.join(body[(y * 800 + 220) * 3:(y * 800 + 550) * 3] for y in range(171, 460))

    before, after, restored, trackpad = (contents(shots[name]) for name in ('before', 'after', 'restored', 'trackpad'))
    assert before != after, 'original Explorer list did not scroll'
    assert before == restored, 'opposite wheel burst did not restore the original list'
    assert trackpad == after, 'nine lines of the trackpad did not scroll what three notches do'
    print('EXPLORER WHEEL PASS: original SHELL32/COMCTL32 list scrolls and returns to the same pixels, a trackpad scrolls it the same way')

    def part(path, left, right):
        magic, size, depth, body = path.read_bytes().split(b'\n', 3)
        assert (magic, size, depth) == (b'P6', b'800 600', b'255')
        return b''.join(body[(y * 800 + left) * 3:(y * 800 + right) * 3] for y in range(190, 450))

    for name, (start, down, back) in views.items():
        assert all(shot.is_file() for shot in (start, down, back)), f'{name}: the screenshots were not taken'
        # With the tree of folders open the list of files stands further right.
        left, right = (70, 200) if name == 'folders' else (230, 550)
        assert part(start, left, right) != part(down, left, right), f'{name}: three notches did not scroll'
        assert part(start, left, right) == part(back, left, right), f'{name}: three notches back did not show the start again'
    assert part(views['folders'][0], 420, 560) == part(views['folders'][1], 420, 560), \
        'the wheel over the tree of folders scrolled the list of files too'
    print('EXPLORER VIEWS PASS: Details, List and Small Icons scroll under the wheel and return; the tree of folders '
          'scrolls under the pointer and the list of files beside it stays')


if __name__ == '__main__':
    main()
