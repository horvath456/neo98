#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""hide/destroy a popup and immediately show an existing saved popup.

Run under a shared serial test lock with --no-build after building the host.
NEO98_CPU selects FEX or Unicorn. No window is created between hide and show.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from test_savebits_restore_host import ROOT, neo98, build_host, fixture, image, RED, win98_c


def main():
    out = neo98.output_directory('savebits-reshow-host')
    drive = out / 'drive-c'
    shutil.rmtree(drive, ignore_errors=True)
    for path in out.glob('*.ppm'):
        path.unlink()
    exe = fixture(out)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    if '--binary' in sys.argv:
        binary = Path(sys.argv[sys.argv.index('--binary') + 1]).resolve()
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
    cases = {'show-window': ('C', 150), 'set-window-pos': ('D', 150),
             'destroy-window': ('E', 150), 'move-window': ('G', 150), 'shrink-window': ('H', 250)}
    steps = ['0:wait=HOST_SAVEBITS_W', '0:idle=300']
    for name, (key, _) in cases.items():
        for code, title in [('B', 'reuse-ready'), (key, 'reuse-shown'), ('F', 'reuse-hidden')]:
            steps += [f'0:key={ord(code):02X}', f'0:wait=|{title}', '0:idle=300']
            if title == 'reuse-shown':
                steps += [f'0:shot={out / (name + "-shown.ppm")}']
        steps += [f'0:shot={out / (name + ".ppm")}']
    steps += ['0:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_DEADLINE='120')
    env.pop('NEO98_HOST_ATTACHED', None)
    env.pop('NEO98_HOST_MENU', None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', str(exe), '--quiet', '--drive-c', str(drive),
               '--no-registry-file', '--crash-report-dir', str(out / 'reports')]
    with (out / 'host.log').open('w') as log:
        process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log,
                                   stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        try:
            code = process.wait(timeout=150)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=30)
            raise
    shutil.rmtree(drive)
    log = (out / 'host.log').read_text(errors='replace')
    waits = re.findall(r'^NEO98 HOST input wait=(.*) (reached|timed out)', log, re.M)
    checks = {'host ended': code == 0,
              'every step reached': len(waits) == 1 + 3 * len(cases) and all(s == 'reached' for _, s in waits)}
    pixels = {}
    for name, (_, left) in cases.items():
        path, shown = out / (name + '.ppm'), out / (name + '-shown.ppm')
        checks[name + ': screenshots'] = path.exists() and shown.exists()
        if not checks[name + ': screenshots']:
            continue
        at = image(path)
        pixels[name] = sum(at(x, y) != RED for y in range(120, 300) for x in range(left, 400))
        checks[name + ': replacement painted'] = image(shown)(270, 280) != RED
        checks[name + ': original background restored'] = pixels[name] == 0
    result = {'checks': checks, 'wrong_pixels': pixels, 'cpu': env.get('NEO98_CPU', 'fex')}
    (out / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    for name, okay in checks.items():
        print('PASS' if okay else 'FAIL', name)
    print('wrong pixels:', pixels)
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
