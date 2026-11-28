#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The local atoms of the 16-bit KERNEL live in the program's data segment.

KRNL386 keeps them in the local heap of the segment in DS: the word at DS:8
points to the table, an entry is a fixed block {next, usage, length, name},
and a string atom is the near pointer of its entry shifted right by two.
Programs read the name there themselves; SETUPX copies the file names of its
copy queue out of its own segment so, and found every name empty while the
port kept these atoms in a table outside the segment.
tools/local_atoms_win16_fixture.asm checks the table, the entry behind
GetAtomHandle, the usage counts, the case of the names, DeleteAtom,
GetAtomName and the integer atoms, one letter per check into C:\\ATOM16.TXT.

    build/analysis-venv/bin/python tools/test_win16_local_atoms_host.py [--no-build]
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
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win16_fixture  # noqa: E402
import win98_c  # noqa: E402

CHECKS = {
    'a': 'AddAtom without a table makes one of 37 buckets at DS:8, InitAtomTable answers it',
    'b': 'the atom is C000h or more, GetAtomHandle is the atom shifted left by two, and the entry there '
         'has the length, a usage of 1 and the name',
    'c': 'AddAtom of the name in other letters answers the same atom, usage 2',
    'd': 'FindAtom in other letters answers it too',
    'e': 'GetAtomName gives the name as it was given and its length, with five bytes four characters',
    'f': 'DeleteAtom answers zero twice, then FindAtom answers zero',
    'g': 'MAKEINTATOM(123) and "#456" answer 123 and 456, GetAtomName(456) is "#456"',
    'h': 'two more names have two atoms, each name at its own entry',
}


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('win16-local-atoms')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    win16_fixture.assemble(ROOT / 'tools/local_atoms_win16_fixture.asm', out / 'ATOM16.EXE', 'ATOM16',
                           neo98.tool_environment())
    shutil.copyfile(out / 'ATOM16.EXE', drive / 'ATOM16.EXE')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='60',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\ATOM16.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=180)
    written = win98_c.path('C:\\ATOM16.TXT', drive)
    data = written.read_bytes() if written else b''
    shutil.rmtree(drive, ignore_errors=True)
    failed = 0
    for index, (letter, name) in enumerate(CHECKS.items()):
        okay = data[index:index + 1] == letter.encode()
        failed += not okay
        print(f"{'ok  ' if okay else 'FAIL'} {letter} {name}")
    if failed:
        print('written:', data, '\n    log:', out / 'log.txt')
        return 1
    print(f'win16 local atoms: all {len(CHECKS)} checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
