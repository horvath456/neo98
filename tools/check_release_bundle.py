#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reject Media Center and private build paths in release bundles."""
from pathlib import Path
import argparse
import re

PATTERNS = [re.compile(pattern, re.I) for pattern in (
    rb'/Use[r]s/[^/\x00\n]+/', rb'(?<![:/])/ho[m]e/[^/\x00\n]+/',
    rb'\b[A-Z0-9]{5}(?:-[A-Z0-9]{5}){4}\b')]


MEDIA_FILES = {'mediactr.exe', 'mediactr.lnk', 'service.neo98'}
# Runtime log text from host_media/service.h, compiled only with host media.
# Unlike symbols, this string survives stripping on macOS and Linux.
HOST_MEDIA_MARKER = b'NEO98 HOST media '


def check_host_media(bundle):
    errors = []
    for path in sorted(bundle.rglob('*')):
        relative = path.relative_to(bundle).as_posix()
        if path.name.casefold() in MEDIA_FILES:
            errors.append(relative + ': Media Center resource in release')
        if path.is_file() and HOST_MEDIA_MARKER in path.read_bytes():
            errors.append(relative + ': host-media code in release')
    return errors


def check(bundle):
    errors = check_host_media(bundle)
    for path in sorted(bundle.rglob('*')):
        if path.is_file():
            data = path.read_bytes()
            if any(pattern.search(data) for pattern in PATTERNS):
                errors.append(path.relative_to(bundle).as_posix() + ': private path or key pattern')
    return errors


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    args = parser.parse_args()
    errors = check(args.bundle)
    if errors: parser.exit(1, '\n'.join(errors) + '\n')
    print('Release bundle scan: zero violations')
