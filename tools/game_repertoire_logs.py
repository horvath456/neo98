#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Bounded, redacted evidence export for repertoire sweep helpers."""
from collections import deque
import gzip
import re
from game_repertoire import check_reserve, log_fragments

SECRET = re.compile(r'password|serial.?number|product.?key|CD.?key', re.I)
SELECT = re.compile(r'EXCEPTION|CRASHREPORT|FAULT BOX|unsupported|NEO98 HOST (exited|failed|deadline|menu-script|input|stats)|ContextCreate|DrawPrimitives2|GLIDE|OpenGL|DDHAL|[Aa]ssert|[Aa]bort|[Tt]erminat|libc\+\+|std::|[Pp]anic')


def export_log(source, dest, work, full=False):
    """Keep short excerpts and at most 256 KiB of compressed sanitized trace.

    Omit overlong lines entirely so a secret split across chunks cannot leak.
    No array, string or compression buffer grows with the source file.
    """
    first, hits = [], []
    last, selected = deque(maxlen=120), deque(maxlen=55)
    packed = dest/'sanitized.log.gz'
    raw = packed.open('wb') if full else None
    zipstream = gzip.GzipFile(filename='', fileobj=raw, mode='wb', mtime=0) if full else None
    omitted = 0
    long_line = False
    try:
        for line in log_fragments(source, work):
            if not line.endswith('\n') and len(line) == 64 * 1024:
                long_line = True
            if long_line:
                if line.endswith('\n'):
                    long_line = False
                    omitted += 1
                continue
            if SECRET.search(line):
                continue
            short = line[:1500]
            if len(first) < 30:
                first.append(short)
            last.append(short)
            if SELECT.search(line):
                if len(hits) < 20:
                    hits.append(short)
                selected.append(short)
            if zipstream:
                zipstream.write(line.encode())
                if raw.tell() > 256 * 1024:
                    zipstream.close()
                    zipstream = None
                    raw.close()
                    packed.unlink()
        check_reserve(work)
        (dest/'excerpt.log').write_text(''.join(first + ['\n... selected lines ...\n'] + hits + list(selected) + ['\n... final raw lines ...\n'] + list(last)) + f'\nOmitted overlong lines: {omitted}\n')
    finally:
        if zipstream:
            zipstream.close()
        if raw:
            raw.close()
        if packed.exists() and packed.stat().st_size > 256 * 1024:
            packed.unlink()
