#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Room on the disk for the tests of saved states, and what they leave.

A game machine of a test is a clone of the Win98 folder with its states (a
quarter to half a gigabyte each as `du` counts it), and a host that does not
come to an end can write a log without end. So every test of saved states
asks for room before it starts (require), and when a run is over keeps only
what tells what happened (tidy): its logs, cut to their ends, its pictures
and its results. A machine folder stays only when the test was asked to
keep it (--keep).
"""
from __future__ import annotations

import shutil
import sys
from pathlib import Path

LOG_CAP = 4 << 20


def free_gib(path='/System/Volumes/Data' if sys.platform == 'darwin' else '/'):
    try:
        return shutil.disk_usage(path).free / (1 << 30)
    except OSError:
        return shutil.disk_usage('/').free / (1 << 30)


def require(gib=8.0):
    """Ends the test when less room is free than it may need."""
    free = free_gib()
    if free < gib:
        raise SystemExit(f'only {free:.1f} GiB free on the disk; this test wants {gib:.0f}. Nothing was started.')


def tidy(folder, keep=False, remove=('machine', 'tmp', 'logs', 'drive-c', 'drive-c-saved', 'drive-c-again', 'bare-host')):
    """What a run leaves in its folder: the big parts go, a log keeps its last megabytes."""
    folder = Path(folder)
    if not folder.is_dir():
        return
    if not keep:
        for name in remove:
            shutil.rmtree(folder / name, ignore_errors=True)
    for log in folder.rglob('*.log'):
        try:
            size = log.stat().st_size
            if size > LOG_CAP:
                with log.open('rb') as handle:
                    handle.seek(size - LOG_CAP)
                    tail = handle.read()
                log.write_bytes(b'[the first %d bytes of this log were cut]\n' % (size - LOG_CAP) + tail)
        except OSError:
            pass
