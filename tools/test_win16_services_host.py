#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""KERNEL and GDI services of the 16-bit side in the host.

tools/services_win16_fixture.asm uses what 16-bit libraries of the thunk
compiler and of the common dialogs ask of KERNEL and GDI beyond the plain
gates:

  flat   __FLATCS and __FLATDS (KERNEL.454 and 455) are ring-three selectors
         over the whole address space, code and data, and a word of the
         program reads the same through __FLATDS at its linear address
  dpmi   INT 31h 000Ch sets a descriptor that 000Bh reads back; a selector
         that is not allocated is 8022h, a system descriptor 8021h
  fonts  GDI.613 EnumFontFamiliesEx calls the far procedure with the LONG,
         ENUMLOGFONTEX with the script name and NEWTEXTMETRICEX with an empty
         font signature, and takes its nine words off the stack

The program writes its words again after each part, so a part that stops the
run leaves the ones before it; the log line of the stop is printed.

    build/analysis-venv/bin/python tools/test_win16_services_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['nasm'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)

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

WORDS = 27


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('win16-services')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    win16_fixture.assemble(ROOT / 'tools/services_win16_fixture.asm', out / 'SVC16.EXE', 'SVC16',
                           neo98.tool_environment(), ('KERNEL', 'USER', 'GDI'))
    shutil.copyfile(out / 'SVC16.EXE', drive / 'SVC16.EXE')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='60',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\SVC16.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=180)
    written = win98_c.path('C:\\SVC16OUT.TXT', drive)
    data = written.read_bytes() if written else b''
    shutil.rmtree(drive, ignore_errors=True)
    if len(data) < 128:
        print('FAIL the program wrote its results', len(data), 'bytes\n    log:', out / 'log.txt')
        return 1
    w = struct.unpack_from(f'<{WORDS}H', data)
    face, script = (data[64 + i * 32:96 + i * 32].split(b'\0')[0] for i in range(2))
    checks = {
        'flat: __FLATDS is a selector': w[0] == 1,
        'flat: __FLATDS reaches 4 GiB': (w[1], w[2]) == (0xffff, 0xffff),
        'flat: __FLATDS is present writable ring-three data': w[3] == 0xf2,
        'flat: the marker reads the same through __FLATDS at its linear address': w[4] == 0x5aa5,
        'flat: __FLATCS is a selector': w[5] == 1,
        'flat: __FLATCS reaches 4 GiB': (w[6], w[7]) == (0xffff, 0xffff),
        'flat: __FLATCS is present readable ring-three code': w[8] == 0xfa,
        'flat: __FLATCS and __FLATDS differ': w[9] == 1,
        'dpmi: 0000h allocates a descriptor': w[10] == 0,
        'dpmi: 000Ch sets it': w[11] == 0,
        'dpmi: the marker reads through the descriptor 000Ch set': w[12] == 0x5aa5,
        'dpmi: 000Bh reads it back with its limit and base': w[13] == 0 and (w[14], w[15], w[16]) == (0xfff, 0, 0),
        'dpmi: 000Bh of a selector that is not allocated is 8022h': w[17] == 0x8022,
        'dpmi: 000Ch of a system descriptor is 8021h': w[18] == 0x8021,
        'fonts: GDI.52 CreateCompatibleDC': w[19] == 1,
        'fonts: GDI.613 EnumFontFamiliesEx answers what the procedure answered': w[20] == 1,
        'fonts: the far procedure gets the LONG': w[21] > 0 and (w[22], w[23]) == (0x5678, 0x1234),
        'fonts: ENUMLOGFONTEX carries the face and the script name': len(face) > 0 and len(script) > 0,
        'fonts: TrueType families come with an empty font signature': w[24] & 4 and w[25] == 0,
        'every call took its word count off the stack': w[26] == 0,
    }
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        text = (out / 'log.txt').read_text(errors='replace').splitlines()
        stops = [line for line in text if 'not ported' in line or 'no native provider' in line]
        print('    words:', [hex(v) for v in w], '\n    names:', face, script,
              '\n    stop:', stops[:1], '\n    log:', out / 'log.txt')
    print(f'win16 services host: {sum(map(bool, checks.values()))} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
