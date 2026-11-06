#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The setup step that puts the CD's cabinets on C: (tools/win98_cabs.py).

A small ISO9660 image with a WIN98 folder (made by hdiutil) stands in for the
Windows 98 CD. The step must copy every file of WIN98 into
WINDOWS\\OPTIONS\\CABS of a set-up folder, append SourcePath to the registry
journal once, note "cabs" in the done-marker, and do nothing the second time.

    build/analysis-venv/bin/python tools/test_win98_cabs.py
"""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
import win98_c  # noqa: E402
import win98_cabs  # noqa: E402
import win98_folder  # noqa: E402


def main():
    out = neo98.output_directory('win98-cabs')
    shutil.rmtree(out, ignore_errors=True)
    source = out / 'cd' / 'WIN98'
    source.mkdir(parents=True)
    files = {'BASE4.CAB': b'MSCF' + bytes(range(256)) * 40, 'PRECOPY1.CAB': b'MSCF' + b'\x5a' * 70000,
             'SETUP.EXE': b'MZ' + b'\0' * 1000, 'SMARTDRV.EXE': b'MZ' + b'\0' * 1000}
    for name, data in files.items():
        (source / name).write_bytes(data)
    (out / 'cd' / 'README.TXT').write_text('not part of WIN98\n')
    iso = out / 'win98.iso'
    if sys.platform == 'darwin':
        subprocess.run(['/usr/bin/hdiutil', 'makehybrid', '-iso', '-o', str(iso), str(out / 'cd')],
                       check=True, capture_output=True)
    elif shutil.which('xorriso'):
        subprocess.run(['xorriso', '-as', 'mkisofs', '-quiet', '-o', str(iso), str(out / 'cd')],
                       check=True, capture_output=True)
    else:
        print('SKIP: needs xorriso to make the test ISO')
        return 77
    folder = out / 'c'
    (folder / 'WINDOWS').mkdir(parents=True)
    (folder / win98_c.MARKER).write_text(json.dumps({'files': 1}) + '\n')
    journal = out / 'c.registry'
    first = win98_cabs.apply(iso, folder, journal)
    second = win98_cabs.apply(iso, folder, journal)
    target = folder.joinpath(*win98_cabs.TARGET)
    (target / 'BASE4.CAB').write_bytes(b'X' * len(files['BASE4.CAB']))
    win98_cabs.copy(iso, folder)
    body = journal.read_bytes()
    records = [r for r in win98_folder.journal_walk(body[len(win98_folder.JOURNAL_SIGNATURE):])]
    checks = {
        'every file of WIN98 is copied byte for byte':
            all((target / name).read_bytes() == data for name, data in files.items()),
        'nothing outside WIN98 is copied': sorted(p.name for p in target.iterdir()) == sorted(files),
        'SourcePath goes into the journal once': body.count(b'SourcePath') == 1
            and (win98_cabs.SOURCE + '\\').encode() in body and any(r[2] == 'SourcePath' for r in records),
        'the done-marker notes the step': json.loads((folder / win98_c.MARKER).read_text()).get('cabs')
            == win98_cabs.VERSION,
        'the second run does nothing': first and second is None,
        'a changed cabinet with the same size is repaired':
            (target / 'BASE4.CAB').read_bytes() == files['BASE4.CAB'],
    }
    for name, ok in checks.items():
        print(('ok  ' if ok else 'FAIL'), name)
    print(f'{sum(checks.values())}/{len(checks)} checks pass')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
