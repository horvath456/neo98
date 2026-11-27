#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A 32-bit and a 16-bit window procedure in front of each other.

A WH_CBT hook out of a 32-bit DLL (tools/subclass_mix_fixture.c) runs in
the process of a 16-bit program (tools/subclass_mix_win16_fixture.asm) and
puts its procedure in front of one window when it is made and in front of
another after the program has put its own 16-bit procedure there. What the
16-bit side gets from SetWindowLong and GetWindowLong(GWL_WNDPROC) for a
32-bit procedure is a far pointer that CallWindowProc takes back as that
procedure, and what the 32-bit side gets for a 16-bit one is a value
CallWindowProcA takes back; written back, either is the same procedure.
Both orders reach every procedure once, in the order they were put there.

    build/analysis-venv/bin/python tools/test_win16_subclass_mix_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['nasm', 'i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixtures with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)

import json
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win16_fixture  # noqa: E402
import win98_c  # noqa: E402


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('win16-subclass-mix')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    (drive / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    env = neo98.tool_environment()
    source = ROOT / 'tools/subclass_mix_fixture.c'
    common = ['i686-w64-mingw32-gcc', str(source), '-O1', '-nostartfiles']
    subprocess.run([*common, '-DBUILD_DLL', '-shared', '-Wl,--entry,_DllMain@12', '-Wl,--kill-at',
                    '-luser32', '-lkernel32', '-o', str(drive / 'SUBMIX.DLL')], env=env, check=True)
    subprocess.run([*common, '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32',
                    '-o', str(drive / 'SUBMIX.EXE')], env=env, check=True)
    win16_fixture.assemble(ROOT / 'tools/subclass_mix_win16_fixture.asm', out / 'SUBMIX16.EXE', 'SUBMIX16', env)
    shutil.copyfile(out / 'SUBMIX16.EXE', drive / 'SUBMIX16.EXE')
    run_env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='60',
                   NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        run_env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\SUBMIX.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=run_env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=180)
    written = win98_c.path('C:\\SUBMIX16.TXT', drive)
    data = written.read_bytes() if written else b''
    shutil.rmtree(drive, ignore_errors=True)
    complete = len(data) == 8 + 2 * 14 + 2
    words = struct.unpack_from('<2I2I3H2I4H', data) if complete else (0,) * 15
    class16, sub16, old_a, got_a, chain_a, view_a, again_a, old_b, got_b, chain_b, view_b, again_b, back_b = (
        words[0], words[1], words[2], words[3], words[4], words[5], words[6], words[7], words[8], words[9],
        words[10], words[11], words[12])
    log_text = (out / 'log.txt').read_text(errors='replace')
    checks = {
        'the 16-bit program wrote its results': complete,
        'no callback nesting without end': 'callback nesting exceeded' not in log_text,
        f'A: SetWindowLong answers a far pointer for the 32-bit procedure: {old_a:08x}':
            old_a not in (0, class16, sub16),
        f'A: GetWindowLong answers the 16-bit procedure: {got_a:08x}': got_a == sub16,
        f'A: 401h goes 16-bit, 32-bit, class: {chain_a:#x}': chain_a == 0x132,
        f'A: the 32-bit side reads a procedure that is not its own: {view_a}': view_a == 2,
        f'A: the 32-bit side writes back what it read, the chain stays: {again_a:#x}': again_a == 0x132,
        f'B: SetWindowLong answers the class procedure: {old_b:08x}': old_b == class16,
        f'B: GetWindowLong answers a far pointer for the 32-bit procedure: {got_b:08x}':
            got_b not in (0, class16, sub16),
        f'B: 401h goes 32-bit, 16-bit, class: {chain_b:#x}': chain_b == 0x123,
        f'B: the 32-bit side reads its own procedure: {view_b}': view_b == 1,
        f'B: the 32-bit side writes back what it read, the chain stays: {again_b:#x}': again_b == 0x123,
        f'B: the 16-bit side writes back the far pointer it read, the chain stays: {back_b:#x}': back_b == 0x123,
    }
    (out / 'results.json').write_text(json.dumps({'written': data.hex(), 'checks': checks}, indent=1))
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    written:', data.hex(), '\n    log:', out / 'log.txt')
    print(f'win16 subclass mix host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
