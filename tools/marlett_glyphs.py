#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Raster the Marlett cells the original DrawFrameControl asks GDI to draw.

USER.EXE only chooses a character and a cell size; the pixels come from GDI's
TrueType rasterizer, which is not part of USER.EXE. This module is that
rasterizer's replacement. Unlike the first two rounds it no longer fills the
outlines of the original MARLETT.TTF as they stand: it runs the font's own
hinting programs through the TrueType instruction interpreter in
tools/truetype_hint.py and scan-converts the gridfitted result with the dropout
control the font asks for. The cells therefore carry the shapes the font author
designed for each ppem, not a naive fill of the outlines.

The interpreter is checked against the `hdmx` table of the same file, the
per-ppem hinted advance widths Microsoft's own rasterizer produced when the font
was built.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from truetype_hint import HintedFont, verify    # noqa: E402
from provenance import stamp as stamp_provenance    # noqa: E402

# Every cell the implemented DrawFrameControl body can select.
CELLS = '012rs34568ahopbcdefgijklmn'
DEFAULT_FONT = ROOT / 'build/nonclient-fidelity/fonts/MARLETT.TTF'


def table(path=None, sizes=range(1, 33)):
    """(font, cells) with cells[(size, code)] = (rows of bits, width, advance)."""
    path = Path(path or DEFAULT_FONT)
    font = HintedFont(path.read_bytes())
    cells = {}
    for size in sizes:
        for code in CELLS:
            rendered = font.cell(code, size)
            if rendered:
                cells[(size, code)] = rendered
    return font, cells


def header(cells, sizes, destination):
    lines = ['// Marlett cells gridfitted by the original hinting programs of the',
             '// local MARLETT.TTF and scan-converted with the dropout control the',
             '// font asks for. Generated locally; do not redistribute.',
             '#pragma once',
             'namespace Win98Marlett {',
             'static const char CellCodes[] = "%s";' % CELLS,
             'static const unsigned MinimumSize = %d, MaximumSize = %d;' % (min(sizes), max(sizes)),
             'static const unsigned CellRows[%d][%d][%d] = {' % (len(sizes), len(CELLS), max(sizes))]
    for size in sizes:
        row = []
        for code in CELLS:
            rows = cells[(size, code)][0] + [0] * (max(sizes) - size)
            row.append('{' + ','.join('%uu' % bits for bits in rows) + '}')
        lines.append('{' + ','.join(row) + '},')
    lines.append('};')
    lines.append('static const unsigned CellAdvance[%d][%d] = {' % (len(sizes), len(CELLS)))
    for size in sizes:
        lines.append('{' + ','.join('%uu' % cells[(size, code)][2] for code in CELLS) + '},')
    lines.append('};')
    lines += ['inline int Index(char code){for(int i=0;CellCodes[i];++i)if(CellCodes[i]==code)return i;return -1;}',
              'inline unsigned Row(int size,char code,int row){',
              '    const int index=Index(code);',
              '    if(index<0||size<(int)MinimumSize||size>(int)MaximumSize||row<0||row>=size)return 0;',
              '    return CellRows[size-MinimumSize][index][row];}',
              'inline unsigned Advance(int size,char code){',
              '    const int index=Index(code);',
              '    if(index<0||size<(int)MinimumSize||size>(int)MaximumSize)return 0;',
              '    return CellAdvance[size-MinimumSize][index];}',
              '}']
    Path(destination).write_text('\n'.join(lines) + '\n')


def provenance(font_path, destination, header_path, cells, sizes, checks):
    digest = hashlib.sha256(Path(font_path).read_bytes()).hexdigest()
    record = {
        'font': {'file': 'MARLETT.TTF', 'sha256': digest,
                 'source': 'local Windows 98 SE installation, WINDOWS/FONTS'},
        'renderer': 'tools/truetype_hint.py',
        'applied': ['fpgm once per font', 'prep once per ppem',
                    'cvt scaled to the ppem', 'glyph programs per glyph',
                    'IUP of untouched points',
                    'monochrome scan conversion at pixel centres',
                    'dropout control from SCANCTRL and SCANTYPE'],
        'not_proven': ['the Win98 GDI TrueType rasterizer itself was not executed; '
                       'only the installed font was searched'],
        'verification': checks,
        'cells': len(cells), 'codes': CELLS,
        'sizes': [min(sizes), max(sizes)],
        'generated_sha256': {'marlett_glyphs.h':
                             hashlib.sha256(Path(header_path).read_bytes()).hexdigest()}}
    stamp_provenance(record, 'marlett-provenance.json')
    Path(destination).write_text(json.dumps(record, indent=2) + '\n')
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', type=Path, default=DEFAULT_FONT)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/generated/marlett_glyphs.h')
    parser.add_argument('--show', action='store_true', help='print the rendered cells')
    parser.add_argument('--sizes', type=int, nargs=2, default=(1, 32))
    arguments = parser.parse_args()
    sizes = list(range(arguments.sizes[0], arguments.sizes[1] + 1))
    _font, cells = table(arguments.font, sizes)
    if arguments.show:
        for size in (10, 13, 14, 16):
            for code in CELLS:
                rows = cells[(size, code)][0]
                print('%r size %d advance %d' % (code, size, cells[(size, code)][2]))
                for bits in rows:
                    print('   ' + ''.join('#' if (bits >> c) & 1 else '.' for c in range(size)))
        return
    arguments.out.parent.mkdir(parents=True, exist_ok=True)
    header(cells, sizes, arguments.out)
    checks = verify(arguments.font)
    record = provenance(arguments.font, arguments.out.parent / 'marlett-provenance.json',
                        arguments.out, cells, sizes, checks)
    if checks['hdmx']['mismatch']:
        raise SystemExit('The hinted advances disagree with hdmx: %s'
                         % checks['hdmx']['mismatch'][:8])
    print('marlett_glyphs.h %s cells=%d sizes=%d-%d font=%s hdmx=%d/%d'
          % (record['generated_sha256']['marlett_glyphs.h'], len(cells), min(sizes), max(sizes),
             record['font']['sha256'], checks['hdmx']['checked'] - len(checks['hdmx']['mismatch']),
             checks['hdmx']['checked']))


if __name__ == '__main__':
    main()
