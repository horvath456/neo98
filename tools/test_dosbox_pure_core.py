#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The headless check of the DOSBox Pure core, milestone 1 of

Builds the pinned core as a static library, links tools/probe_dosbox_pure.cpp
against it with the provider of tools/host_dos_vm.cpp, writes a hand-assembled
HELLO.COM into a scratch directory that becomes C:, and runs it twice in the
same process. The program prints one line through INT 21h/09h and ends through
INT 21h/4Ch with code 42. The picture of the machine has to carry the line,
the exit code has to arrive both times, and nothing may crash.

    build/analysis-venv/bin/python tools/test_dosbox_pure_core.py
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import neo98
from build_dosbox_pure import core_dylib
from paint_demo import png

# mov ah,9 / mov dx,010Ch / int 21h / mov ax,4C2Ah / int 21h / db 'NEO98 DOS VM hello$'
HELLO = bytes.fromhex('b409ba0c01cd21b82a4ccd21') + b'NEO98 DOS VM hello$'
EXIT_CODE = 42


def build_probe(out):
    core = core_dylib()
    binary = out / 'probe-dosbox-pure'
    flags = ['-std=c++17', '-O1', '-g', '-fsanitize=address,undefined', '-pthread', f'-DNEO98_DOSBOX_CORE="{core}"']
    subprocess.run(['clang++', *flags, str(ROOT / 'tools/probe_dosbox_pure.cpp'),
                    str(ROOT / 'tools/host_dos_vm.cpp'), str(ROOT / 'src/dos_vm.cpp'), str(ROOT / 'src/sound_synth.cpp'),
                    str(ROOT / 'src/sound_microphone.cpp'), str(ROOT / 'src/sound_mixer.cpp'),
                    str(ROOT / 'src/pc_speaker.cpp'),
                    '-o', str(binary)], check=True, env=neo98.tool_environment(), cwd=ROOT)
    return binary, core


def picture(source, target):
    magic, dimensions, maximum, pixels = source.read_bytes().split(b'\n', 3)
    width, height = map(int, dimensions.split())
    assert magic == b'P6' and maximum == b'255' and len(pixels) == width * height * 3
    png(target, width, height, pixels)
    return width, height


def main():
    out = ROOT / 'build/dos-vm-test'
    drive = out / 'drive-c'
    drive.mkdir(parents=True, exist_ok=True)
    (drive / 'HELLO.COM').write_bytes(HELLO)
    binary, core = build_probe(ROOT / 'build/native-app')
    result = subprocess.run([str(binary), '--drive', f'C={drive}', '--command', 'HELLO.COM', '--seconds', '30',
                             '--output', str(out / 'hello.ppm'), '--twice'],
                            capture_output=True, text=True, timeout=300, cwd=ROOT)
    (out / 'core-probe.log').write_text(result.stdout + result.stderr)
    print(result.stdout[-3000:], end='')
    print(result.stderr[-3000:], end='', file=sys.stderr)
    runs = {}
    for match in re.finditer(r'^DOSBOX PROBE (first|second) frames=(\d+) width=(\d+) height=(\d+) lit=(\d+) '
                             r'colors=(\d+) audio=(\d+) finished=(\d) exit=(\d+) crashed=(\d) message=(.*)$',
                             result.stdout, re.M):
        runs[match[1]] = {'frames': int(match[2]), 'width': int(match[3]), 'height': int(match[4]),
                          'lit_pixels': int(match[5]), 'colors': int(match[6]), 'audio_frames': int(match[7]),
                          'finished': match[8] == '1', 'exit_code': int(match[9]), 'crashed': match[10] == '1',
                          'message': match[11]}
    report = {'core_sha256': hashlib.sha256(core.read_bytes()).hexdigest(),
              'core_version': json.loads((ROOT / 'build/dosbox-pure-host/stamp.json').read_text())['version'],
              'program': 'HELLO.COM, ' + HELLO.hex(), 'runs': runs, 'probe_exit_code': result.returncode,
              'sanitizers': ['address', 'undefined'],
              'boundary': 'The core alone behind the provider, without USER, Unicorn or SDL. It certifies the '
                          'static library, the drives, the autoexec and the exit code; the window is milestone 2.'}
    if (out / 'hello.ppm').exists():
        report['picture'] = {'file': 'hello.png', 'size': list(picture(out / 'hello.ppm', out / 'hello.png'))}
    (out / 'core-results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert result.returncode == 0, f'probe exited {result.returncode}; see {out}/core-probe.log'
    for name in ('first', 'second'):
        assert f'DOSBOX TEXT {name} columns=80 rows=25 hello=1' in result.stdout,result.stdout
        run = runs.get(name)
        assert run, f'no {name} run reported'
        assert run['finished'] and not run['crashed'], f'{name} run did not finish cleanly: {run}'
        assert run['exit_code'] == EXIT_CODE, f'{name} run exit code {run["exit_code"]}'
        assert run['width'] >= 320 and run['height'] >= 200, f'{name} run has no picture'
        assert run['lit_pixels'] >= 300 and run['colors'] >= 2, f'{name} run shows no text: {run}'
    print('DOSBOX PURE CORE PASS', json.dumps(runs['first']))
    return report


if __name__ == '__main__':
    main()
