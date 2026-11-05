#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Read back the PDF file the LPT1 sink made from a print job.

tools/printer_port_host.h turns every PostScript job into a PDF file with Ghostscript
(ps2pdf or gs). The print tests use this module to check that file: Ghostscript's txtwrite
device writes one text file per page, so the number of files is the number of pages and their
content is the text the page shows. With -dTextFormat=1 the same device writes every run of
text with its font name and size, which `spans` returns.

    build/analysis-venv/bin/python tools/print_pdf.py FILE.pdf
"""
from pathlib import Path
import html
import re
import shutil
import subprocess
import sys
import tempfile


def ghostscript():
    """The Ghostscript the host sink uses, or None when the Mac has none."""
    return shutil.which('gs')


def pages(pdf, scratch):
    """The text of every page of a PDF file, in page order."""
    gs = ghostscript()
    if not gs:
        raise RuntimeError('Ghostscript (gs) is not installed: brew install ghostscript')
    scratch = Path(scratch)
    scratch.mkdir(parents=True, exist_ok=True)
    for old in scratch.glob('page-*.txt'):
        old.unlink()
    subprocess.run([gs, '-q', '-dSAFER', '-dBATCH', '-dNOPAUSE', '-sDEVICE=txtwrite',
                    '-sOutputFile=' + str(scratch / 'page-%03d.txt'), str(pdf)], check=True, capture_output=True)
    files = sorted(scratch.glob('page-*.txt'))
    return [file.read_text(errors='replace') for file in files]


def spans(pdf):
    """Every run of text of a PDF file as (font, size in points, text), in page order."""
    gs = ghostscript()
    if not gs:
        raise RuntimeError('Ghostscript (gs) is not installed: brew install ghostscript')
    out = subprocess.run([gs, '-q', '-dSAFER', '-dBATCH', '-dNOPAUSE', '-sDEVICE=txtwrite', '-dTextFormat=1',
                          '-sOutputFile=-', str(pdf)], check=True, capture_output=True).stdout.decode('latin-1')
    result = []
    for match in re.finditer(r'<span [^>]*font="([^"]*)" size="([^"]*)">(.*?)</span>', out, re.S):
        text = ''.join(html.unescape(c) for c in re.findall(r' c="((?:[^"]|&quot;)*)"', match[3]))
        result.append((match[1], float(match[2]), text))
    return result


def main():
    with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / 'build') as scratch:
        for number, text in enumerate(pages(sys.argv[1], scratch), 1):
            print(f'--- page {number}')
            print(text)
    for font, size, text in spans(sys.argv[1]):
        print(f'{font} {size:g} pt: {text.strip()}')


if __name__ == '__main__':
    main()
