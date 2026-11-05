#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Check persistent program clones without changing their source."""
from pathlib import Path
import tempfile

import programs_drive


def main():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source = root / 'source'
        source.mkdir()
        original = source / 'game.dat'
        original.write_bytes(b'original')
        parent = root / 'scratch'
        arguments = programs_drive.writable_snapshot(source, parent)
        assert arguments[0] == '--drive'
        snapshot = Path(arguments[1].split('=', 1)[1])
        assert snapshot != source
        assert (snapshot / 'game.dat').read_bytes() == b'original'
        (snapshot / 'game.dat').write_bytes(b'changed')
        (snapshot / 'created.dat').write_bytes(b'new')
        assert original.read_bytes() == b'original'
        assert not (source / 'created.dat').exists()
        assert programs_drive.writable_snapshot(source, parent) == arguments
        assert (snapshot / 'game.dat').read_bytes() == b'changed'
        assert (snapshot / 'created.dat').read_bytes() == b'new'
    print('PROGRAMS DRIVE SNAPSHOT PASS source unchanged, guest writes persistent')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
