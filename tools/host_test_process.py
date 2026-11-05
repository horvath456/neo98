#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded logs and owned process groups for host test runs."""
import os
from pathlib import Path
import signal
import shutil
import subprocess
import time


def stop_group(process, grace=0.25):
    """Only the group started by run_logged belongs to this test."""
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except (ProcessLookupError, PermissionError):
        return
    until = time.monotonic() + grace
    while time.monotonic() < until:
        # Do not reap the leader here: its PID keeps our group identity
        # reserved until the final wait, including an immediate stop.
        try:
            os.killpg(process.pid, 0)
        except (ProcessLookupError, PermissionError):
            return
        time.sleep(0.01)
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass
    process.wait(timeout=5)


def run_logged(command, log, *, env=None, cwd='/', timeout=300,
               log_limit=32 << 20, output_limit=4 << 20, stop_grace=0.25, on_line=None,
               min_free_bytes=0):
    """Return bounded text; on_line(line, process) sees output while it runs."""
    log = Path(log)
    with log.open('wb') as handle:
        process = subprocess.Popen(command, cwd=cwd, env=env, stdout=handle,
                                   stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                   start_new_session=True)
        deadline = time.monotonic() + timeout
        reader = log.open('rb') if on_line else None
        pending = b''
        next_disk_check = 0
        try:
            while True:
                if min_free_bytes and time.monotonic() >= next_disk_check:
                    next_disk_check = time.monotonic() + 0.5
                    if shutil.disk_usage(log.parent).free < min_free_bytes:
                        code = 'disk reserve reached'
                        break
                if log.stat().st_size > log_limit:
                    code = 'log size limit'
                    break
                code = process.poll()
                if reader:
                    pending += reader.read(log_limit + 1)
                    lines = pending.split(b'\n')
                    pending = lines.pop()
                    for line in lines:
                        on_line((line + b'\n').decode(errors='replace'), process)
                    if code is not None and pending:
                        on_line(pending.decode(errors='replace'), process)
                        pending = b''
                if code is not None:
                    break
                if time.monotonic() >= deadline:
                    code = 'timeout'
                    break
                time.sleep(0.01)
        finally:
            # A child can keep stdout open even after its parent exited.
            stop_group(process, stop_grace)
            process.wait(timeout=5)
            if reader:
                reader.close()
    size = log.stat().st_size
    if size > output_limit:
        marker = b'\n[host test log truncated: beginning and end preserved]\n'
        first_size = (output_limit - len(marker)) // 2
        last_size = output_limit - len(marker) - first_size
        with log.open('rb') as handle:
            first = handle.read(first_size)
            handle.seek(-last_size, 2)
            last = handle.read(last_size)
        log.write_bytes(first + marker + last)
    return code, log.read_text(errors='replace')
