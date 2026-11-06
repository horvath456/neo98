#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reject Media Center and private build paths in release bundles."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

PATTERNS = [re.compile(pattern) for pattern in (
    rb'/Use[r]s/[^/\x00\n]+/', rb'(?<![:/])/ho[m]e/[^/\x00\n]+/',
    # Product keys use only this alphabet; Unicode character names in bundled
    # libraries (1DOTS-2DOTS-...) do not.
    rb'\b[BCDFGHJKMPQRTVWXY2346789]{5}(?:-[BCDFGHJKMPQRTVWXY2346789]{5}){4}\b')]


# Fixed example text inside bundled third-party code, not a path of a machine:
# the docstring of ntpath.splitroot in Python's standard library.
HARMLESS = (b"/User" b"s/Barney') == ('C:', '/', 'User" b"s/Barney')",)
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


def smoke_qemu(toolkit, seconds=3):
    """Start TCG from the packaged toolkit, without a guest disk or display."""
    from setup_toolkit import clean_env
    command = [toolkit / 'bin/qemu-system-i386', '-L', toolkit / 'share/qemu',
               '-M', 'pc', '-accel', 'tcg', '-display', 'none', '-nodefaults']
    with tempfile.TemporaryFile() as log:
        with subprocess.Popen(list(map(str, command)), stdout=log, stderr=log,
                              env=clean_env(toolkit)) as process:
            try:
                process.wait(timeout=seconds)
            except subprocess.TimeoutExpired:
                pass
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
        log.seek(0)
        output = log.read().decode('utf-8', errors='replace')
    if 'jit buffer' in output.lower() or process.returncode not in (0, -15):
        raise RuntimeError(f'Toolkit QEMU TCG smoke test failed ({process.returncode}):\n{output}')
    print('Toolkit QEMU TCG smoke test: passed')


def check(bundle):
    errors = check_host_media(bundle)
    for path in sorted(bundle.rglob('*')):
        if path.is_file():
            data = path.read_bytes()
            for text in HARMLESS:
                data = data.replace(text, b'')
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
