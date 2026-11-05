#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The program collection of the reference system as drive D:, without a copy.

`the NEO98_COLLECTION directory` is 2.2 GB with the two folders GAMES
and Programme. It was the C: of the reference system, and it is the corpus the check
matrix of PLAN.md step 6 runs. The interactive host keeps one persistent APFS
clone as D:. The matrix keeps a separate clone per run because those runs have
to stay independent of each other.

The ordinary Explorer host run creates a persistent APFS block clone under
`build/` and mounts that clone writable. Programs may save beside their EXE and
see the save on the next run, while the source and its directory indexes remain
unchanged. `--programs-drive-readonly` mounts the source itself for a diagnostic
run that must reject writes.

    build/analysis-venv/bin/python tools/programs_drive.py --scan

The scan answers the one question the guest paths raise. The reference system had the
collection under C:, so a program could carry `C:\\GAMES\\...` in its image or in
a configuration file; on D: such a path would miss. It counts the files that do.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HOME = Path.home()
from external_paths import external_path
COLLECTION = (external_path('NEO98_COLLECTION') if os.environ.get('NEO98_COLLECTION')
              else ROOT / 'build/external/set-NEO98_COLLECTION')
GAMES = COLLECTION / 'GAMES'
LETTER = 'D'
# What the guest calls the collection once it is mounted, and the guest path of
# GAMES on it. The matrix used to put the contents of GAMES straight under D:\;
# since 19 September 2026 the mount is the whole directory, so the guest sees
# the layout the reference system had, one drive letter over.
ROOT_PATH = f'{LETTER}:'
GAMES_PATH = f'{LETTER}:\\GAMES'


def available():
    return COLLECTION.is_dir()


def mount_arguments(writable=False, directory=None):
    """The runner arguments that mount the collection, or nothing when it is absent."""
    directory = Path(directory) if directory else COLLECTION
    if not directory.is_dir():
        return []
    option = '--drive' if writable else '--drive-readonly'
    return [option, f'{LETTER}={directory}']


def writable_snapshot(directory=None, parent=None):
    """Create or reuse a persistent APFS clone that the guest may change."""
    source = Path(directory) if directory else COLLECTION
    if not source.is_dir():
        return []
    parent = Path(parent) if parent else ROOT / 'build/programs-drive'
    parent.mkdir(parents=True, exist_ok=True)
    snapshot = parent / 'drive-d'
    if not snapshot.exists():
        temporary = parent / 'drive-d.new'
        shutil.rmtree(temporary, ignore_errors=True)
        result = subprocess.run(['cp', '-Rc', str(source), str(temporary)],
                                capture_output=True, text=True)
        if result.returncode:
            shutil.rmtree(temporary, ignore_errors=True)
            raise RuntimeError('cannot create the APFS program clone; no full-copy fallback is used: ' +
                               (result.stderr.strip() or f'cp exited {result.returncode}'))
        temporary.rename(snapshot)
    return mount_arguments(True, snapshot)


def mounts_letter(arguments, letter=LETTER):
    """Whether the caller already mounted this letter, so the default stays out."""
    letter = letter.upper()
    for text in arguments:
        head = text.split('=', 1)[0].strip().upper()
        if head == letter:
            return True
    return False


# A path on C: that would land inside the collection if the collection were C:.
# The match keeps the whole path, so the report can tell a run time path from the
# compiler path in the debug information of a Delphi program.
C_PATH = re.compile(rb'[Cc]:\\+(?:GAMES|PROGRAMME)\\[\\A-Za-z0-9 ._~!()-]*', re.IGNORECASE)


def scan(directory=None):
    """Every file of the collection that names a path on C: inside the collection.

    The reference system kept GAMES and Programme on C:. Mounted as D: those paths miss,
    so this is the number that decides whether the layer above the tree needs a
    C:\\GAMES of its own.
    """
    directory = Path(directory) if directory else COLLECTION
    files, programs, hits = 0, 0, []
    for folder, _, names in os.walk(directory):
        for name in names:
            path = Path(folder) / name
            try:
                data = path.read_bytes()
            except OSError:
                continue
            files += 1
            if name.upper().endswith('.EXE'):
                programs += 1
            found = sorted({m.group(0).decode('latin-1') for m in C_PATH.finditer(data)})
            if found:
                hits.append({'file': str(path.relative_to(directory)), 'paths': found,
                             'program': name.upper().endswith('.EXE')})
    return {'collection': str(directory), 'files': files, 'programs': programs,
            'hits': sorted(hits, key=lambda h: h['file']),
            'programs_with_hits': sum(1 for h in hits if h['program'])}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--scan', action='store_true',
                        help='count the files that name a C: path inside the collection')
    parser.add_argument('--directory', type=Path, default=None)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/programs-drive/scan.json')
    args = parser.parse_args(argv)
    if not (args.directory.is_dir() if args.directory else available()):
        print('Set NEO98_COLLECTION to your program collection directory', file=sys.stderr)
        return 1
    if not args.scan:
        print(' '.join(mount_arguments(directory=args.directory)))
        return 0
    report = scan(args.directory)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=1) + '\n')
    print(f"{report['files']} files, {report['programs']} programs, "
          f"{len(report['hits'])} files name a C: path inside the collection, "
          f"{report['programs_with_hits']} of them programs")
    for hit in report['hits']:
        print(f"  {hit['file']}: " + '; '.join(hit['paths'][:4]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
