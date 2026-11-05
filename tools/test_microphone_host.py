#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The microphone in the host without a window: the SDL provider of
tools/host_audio.h, the test device and the switch "Mikrofon in Windows".

Every run is neo98-host with SDL_VIDEODRIVER=dummy and SDL_AUDIODRIVER=dummy
over a clone of the Win98 folder, with WAVEIN.EXE (tools/wave_in_fixture.c)
as its program. No run opens the real microphone and none makes macOS ask:

1. test: NEO98_TEST_MICROPHONE=sine, the host leaves SDL alone and the
   guest records 441 Hz in eight formats.
2. sdl: no test device. The host opens SDL's capture device of the dummy
   driver, which has no hardware (and so no permission to ask for), at the
   first waveInStart, pushes its silence into the ring and closes it again;
   the guest records silence through the whole path a real microphone takes.
3. off: --no-microphone, the switch off at start: Windows has no recording
   device, and the host never opens one.
4. menu: the switch turned off by the menu item while the program runs
   (menu=microphone): the device goes away.

    build/analysis-venv/bin/python tools/test_microphone_host.py [--no-build]


"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
from test_device_manager_host import host  # noqa: E402
from wave_in_components import IMPORTS  # noqa: E402

win98_c.use_pristine()


def fixture(out):
    """WAVEIN.EXE, built the way tools/test_wave_in_guest.py builds it."""
    env = neo98.tool_environment()
    libs = []
    for dll, entries in IMPORTS.items():
        definition, library = out / (dll + '.def'), out / (dll + '.lib')
        definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' + ''.join(f'{n}@{b}\n' for n, b in entries.items()))
        subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)], env=env, check=True)
        libs.append(str(library))
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-c', str(ROOT / 'tools/wave_in_fixture.c'), '-o', str(out / 'fixture.obj')],
                   env=env, check=True)
    exe = out / 'WAVEIN.EXE'
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:console', '/timestamp:0', '/nodefaultlib',
                    '/machine:x86', '/out:' + str(exe), str(out / 'fixture.obj'), *libs], env=env, check=True)
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/microphone-host')
    arguments = parser.parse_args()
    out = arguments.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if arguments.no_build else build_host()
    exe = fixture(out)

    def prepare(run):
        shutil.copy(exe, run / 'c/WAVEIN.EXE')

    runs = {
        'test': ('sine', {'NEO98_TEST_MICROPHONE': 'sine'}, [], []),
        'sdl': ('silence', {}, [], []),
        'off': ('none', {'NEO98_TEST_MICROPHONE': 'sine'}, ['--no-microphone'], []),
        'menu': ('toggle', {'NEO98_TEST_MICROPHONE': 'sine'}, [], ['2:menu=microphone']),
    }
    results, failed = {}, []
    for name, (kind, environment, extra, steps) in runs.items():
        env = dict(environment)
        env.setdefault('NEO98_TEST_MICROPHONE', '')
        code, text, run = host(binary, out, name, 'C:\\WAVEIN.EXE', f'WAVEIN.EXE {kind}', steps, 120,
                               win98_c.FOLDER, prepare=prepare, extra=extra, environment=env)
        lines = [line for line in text.splitlines() if line.startswith(('NEO98 MICROPHONE', 'NEO98 HOST exited',
                                                                        'NEO98 HOST failed', 'NEO98 HOST microphone'))]
        exited = any(line.startswith('NEO98 HOST exited code=0 ') for line in lines)
        checks = {'exit code 0': exited}
        if name == 'test':
            checks['the test device, not SDL'] = any('source=test' in line for line in lines) and \
                not any('driver=' in line for line in lines)
        if name == 'sdl':
            checks['SDL capture of the dummy driver'] = any('NEO98 MICROPHONE driver=dummy' in line for line in lines)
            checks['opened at the first start'] = any('start state=running source=host' in line for line in lines)
            checks['closed with the last stream'] = any(line == 'NEO98 MICROPHONE stop' for line in lines)
        if name == 'off':
            checks['never opened'] = not any('NEO98 MICROPHONE start' in line for line in lines)
        if name == 'menu':
            checks['the menu turned it off'] = any(line == 'NEO98 HOST microphone off' for line in lines)
        results[name] = {'kind': kind, 'code': code, 'lines': lines, 'checks': checks}
        print(name, 'PASS' if all(checks.values()) else 'FAIL', json.dumps(checks))
        for line in lines:
            print('   ', line)
        if not all(checks.values()):
            failed.append(name)
            print(text[-3000:])
        shutil.rmtree(run / 'c', ignore_errors=True)
    (out / 'results.json').write_text(json.dumps({'status': 'FAIL' if failed else 'PASS', 'runs': results},
                                                 indent=2) + '\n')
    if failed:
        raise SystemExit('MICROPHONE HOST FAIL ' + ' '.join(failed))
    print('MICROPHONE HOST PASS')


if __name__ == '__main__':
    main()
