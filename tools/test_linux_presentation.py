#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check idle uploads, resize redraws and keyboard updates in the SDL/X11 host."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
from native_app import ROOT, build_host
import neo98
import win98_c


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path)
    args = parser.parse_args()
    win98_c.use_pristine()
    binary = args.runner or build_host()
    out = ROOT / 'build/native-linux/presentation'
    out.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    definition = out/'kernel.def'
    definition.write_text('LIBRARY KERNEL32.DLL\nEXPORTS\nGetStdHandle@4\nReadFile@20\nWriteFile@20\nExitProcess@4\n')
    library, obj, console = out/'kernel.lib', out/'console.obj', out/'CONSOLE.EXE'
    commands = [
        ['llvm-dlltool','-m','i386','-k','-d',str(definition),'-l',str(library)],
        ['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector',
         '-c',str(ROOT/'tools/linux_console_fixture.c'),'-o',str(obj)],
        ['lld-link','/machine:x86','/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/fixed','/safeseh:no',
         str(obj),str(library),'/out:'+str(console)],
    ]
    for command in commands: subprocess.run(command,env=env,check=True)
    reports = {}
    for name in ('idle', 'resize-idle', 'resize-type', 'console', 'console-seamless'):
        registry = out / f'{name}.registry'
        registry.unlink(missing_ok=True)
        shots = {key: out / f'{name}-{key}.png' for key in ('before', 'resized', 'typed')}
        steps = f'1:shot={shots["before"]};'
        if name == 'resize-type':
            steps += f'1.2:resize=1000,750;1.8:shot={shots["resized"]};2:type=abc;2.8:shot={shots["typed"]};'
        if name == 'resize-idle':
            steps += f'1.3:resize=1000,750;1.8:shot={shots["resized"]};'
        if name.startswith('console'):
            steps += f'1.8:type=abc;2:key=0d;2.8:shot={shots["typed"]};'
        steps += '3.5:menu=quit'
        env = dict(os.environ, NEO98_HOST_INPUT=steps, NEO98_PRESENT_STATS='1', SDL_AUDIODRIVER='dummy',
                   SDL_VIDEODRIVER='x11', GDK_BACKEND='x11')
        env.pop('NEO98_GPU', None)
        command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
                   '--exe', r'C:\WINDOWS\NOTEPAD.EXE', '--registry', str(registry), '--ram-drive']
        if name.startswith('console'):
            command[command.index('--exe')+1] = str(console)
            if name == 'console-seamless': command.append('--seamless')
        elif name in ('idle', 'resize-idle'):
            command[command.index('--exe')+1] = r'C:\WINDOWS\WINVER.EXE'
        run = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=30)
        log = run.stdout + run.stderr
        (out / f'{name}.log').write_text(log)
        match = re.search(r'NEO98 PRESENT uploads=(\d+) presentations=(\d+) seconds=([0-9.]+)', log)
        assert run.returncode == 0 and match, log[-2000:]
        uploads, presents = map(int, match.groups()[:2])
        if name == 'idle':
            # Startup can paint the desktop before WINVER's first frame.
            # GTK and the window manager expose the canvas during startup
            # animations. Those redraws must reuse the existing texture.
            assert 1 <= uploads <= 3 and uploads <= presents <= 32, (uploads, presents)
        elif name == 'resize-idle':
            assert shots['before'].read_bytes() == shots['resized'].read_bytes(), 'Resize changed the static guest frame'
            assert 1 <= uploads <= 3 and presents > uploads, (uploads, presents)
        elif name == 'resize-type':
            assert shots['before'].read_bytes() == shots['resized'].read_bytes(), 'Resize changed the guest framebuffer'
            assert shots['before'].read_bytes() != shots['typed'].read_bytes(), 'Typing did not reach the guest image'
            # GTK may coalesce the resize with a guest caret blink. The
            # static resize case above checks the extra presentation alone.
            assert uploads >= 2 and presents >= uploads, (uploads, presents)
        else:
            assert shots['before'].read_bytes() != shots['typed'].read_bytes(), 'Console output did not update the image'
            assert uploads >= 2, (uploads, presents)
        reports[name] = {'uploads': uploads, 'presentations': presents, 'seconds': float(match[3])}
        print(name, reports[name], flush=True)
    (out / 'results.json').write_text(json.dumps(reports, indent=2) + '\n')
    assert not win98_c.pristine_problems(win98_c.FOLDER), 'The test modified its source tree'
    print('LINUX PRESENTATION PASS: idle, resize redraw, typed pixels')


if __name__ == '__main__':
    main()
