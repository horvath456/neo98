#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Import a Win98 folder or ZIP, or run the existing ISO installer."""
import argparse
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import sys
import tempfile
import time
import zipfile
import win98_c
import win98_source

REQUIRED = ('WINDOWS/EXPLORER.EXE', 'WINDOWS/SYSTEM.DAT', 'WINDOWS/USER.DAT', 'WINDOWS/FONTS/VGAFIX.FON')


def validate(root):
    root = Path(root)
    missing = [name for name in REQUIRED if not (root / name).is_file()]
    if missing:
        raise RuntimeError('Windows 98 files missing: ' + ', '.join(missing))
    return root


def import_machine(source, target):
    source, target = Path(source).resolve(), Path(target).absolute()
    if target.exists() or win98_c.journal(target).exists():
        raise RuntimeError(f'Destination folder already exists: {target}')
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.neo98-import-', dir=target.parent) as temporary:
        staging = Path(temporary) / 'C'
        if source.is_dir():
            validate(source)
            if target.is_relative_to(source):
                raise RuntimeError('The destination must not be inside the source folder.')
            print('setup: copy Windows 98 folder', flush=True)
            shutil.copytree(source, staging)
            journal = win98_c.journal(source)
            if journal.is_file(): shutil.copy2(journal, Path(temporary) / 'C.registry')
        else:
            print('setup: validate and extract ZIP', flush=True)
            extracted = Path(temporary) / 'zip'
            with zipfile.ZipFile(source) as archive:
                for item in archive.infolist():
                    path = PurePosixPath(item.filename)
                    mode = item.external_attr >> 16
                    if path.is_absolute() or '..' in path.parts or '\\' in item.filename or stat.S_ISLNK(mode):
                        raise RuntimeError('The ZIP contains an unsafe path.')
                archive.extractall(extracted)
            candidates = [p.parent for p in extracted.rglob('WINDOWS') if p.is_dir() and (p / 'EXPLORER.EXE').is_file()]
            if len(candidates) != 1:
                raise RuntimeError('The ZIP must contain exactly one Windows 98 drive C:.')
            validate(candidates[0]); shutil.move(str(candidates[0]), staging)
            journal = win98_c.journal(candidates[0])
            if journal.is_file(): shutil.copy2(journal, Path(temporary) / 'C.registry')
        validate(staging)
        # An imported original gets the same host settings as an ISO install.
        # An existing NEO98 machine retains its journal and configuration.
        if not win98_c.ready(staging):
            import win98_folder
            import win98_time_zone
            import win98_glide_driver
            journal = Path(temporary) / 'C.registry'
            win98_folder.cleanup(staging, staging, journal)
            win98_time_zone.apply(staging, journal)
            win98_glide_driver.apply(staging, journal)
            print('setup: write checksums', flush=True)
            count = win98_c.write_checksums(staging)
            (staging / win98_c.MARKER).write_text(json.dumps({'imported': True, 'checksums': count,
                'created': time.strftime('%Y-%m-%dT%H:%M:%S')}, indent=2) + '\n')
        (staging / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
        os.rename(staging, target)
        journal = Path(temporary) / 'C.registry'
        if journal.is_file(): os.rename(journal, win98_c.journal(target))
    print(f'setup: Windows 98 ready in {target}', flush=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('import', 'iso', 'check', 'auto'))
    parser.add_argument('source', type=Path)
    parser.add_argument('--target', type=Path)
    parser.add_argument('--work', type=Path)
    parser.add_argument('--preview', type=Path)
    args = parser.parse_args(argv)
    try:
        if args.mode == 'auto':
            args.mode = ('import' if args.source.is_dir() or
                         (args.source.suffix.lower() == '.zip' and
                          not win98_source.zip_contains_iso(args.source, machine_files=REQUIRED)) else 'iso')
        if args.mode == 'check': validate(args.source)
        elif not args.target: raise RuntimeError('Destination folder missing.')
        elif args.mode == 'import': import_machine(args.source, args.target)
        else:
            import setup_win98
            command = ['--user', '--iso', str(args.source), '--target', str(args.target), '--key-stdin']
            if args.work: command += ['--work', str(args.work)]
            if args.preview: command += ['--preview', str(args.preview)]
            return setup_win98.main(command)
        return 0
    except (RuntimeError, OSError, zipfile.BadZipFile) as error:
        print(f'setup: ERROR: {error}', flush=True)
        return 1

if __name__ == '__main__':
    sys.exit(main())
