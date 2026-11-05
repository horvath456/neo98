#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""The round-two Marlett renderer, kept so the audit can show what changed.

This is the unhinted renderer rounds one and two shipped: it fills the original
MARLETT.TTF outlines with a scanline rasterizer and never runs the font hinting
programs. It is retained only as the "before" side of the round-three report.

Original docstring follows.

Raster the Marlett cells the original DrawFrameControl asks GDI to draw.

USER.EXE only chooses a character and a cell size; the pixels come from GDI's
TrueType renderer, which is not part of USER.EXE and is not implemented in
this project. This module is that renderer's replacement: it reads the original
MARLETT.TTF outlines from the local Windows installation and fills them with our
own scanline rasterizer. The original hinting programs (fpgm, prep, cvt) are not
interpreted, so these cells are honest Marlett shapes, not proven Win98 pixels.
"""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Every cell the implemented DrawFrameControl body can select.
CELLS = '012rs34568ahopbcdefgijklmn'


def _tables(data):
    count = struct.unpack_from('>H', data, 4)[0]
    tables = {}
    for index in range(count):
        tag, _sum, offset, length = struct.unpack_from('>4sIII', data, 12 + index * 16)
        tables[tag.decode('ascii')] = (offset, length)
    return tables


class Marlett:
    def __init__(self, data):
        self.data = data
        self.tables = _tables(data)
        head = self.tables['head'][0]
        self.units = struct.unpack_from('>H', data, head + 18)[0]
        long_loca = struct.unpack_from('>h', data, head + 50)[0]
        os2 = self.tables['OS/2'][0]
        self.ascent = struct.unpack_from('>H', data, os2 + 74)[0]
        self.descent = struct.unpack_from('>H', data, os2 + 76)[0]
        glyphs = struct.unpack_from('>H', data, self.tables['maxp'][0] + 4)[0]
        loca = self.tables['loca'][0]
        if long_loca:
            self.loca = [struct.unpack_from('>I', data, loca + i * 4)[0] for i in range(glyphs + 1)]
        else:
            self.loca = [struct.unpack_from('>H', data, loca + i * 2)[0] * 2 for i in range(glyphs + 1)]
        hhea = self.tables['hhea'][0]
        metrics = struct.unpack_from('>H', data, hhea + 34)[0]
        hmtx = self.tables['hmtx'][0]
        self.advances = [struct.unpack_from('>H', data, hmtx + i * 4)[0] for i in range(metrics)]
        self.cmap = self._cmap()

    def _cmap(self):
        data = self.data
        base = self.tables['cmap'][0]
        count = struct.unpack_from('>H', data, base + 2)[0]
        for index in range(count):
            platform, encoding, offset = struct.unpack_from('>HHI', data, base + 4 + index * 8)
            table = base + offset
            if struct.unpack_from('>H', data, table)[0] != 4:
                continue
            segments = struct.unpack_from('>H', data, table + 6)[0] // 2
            ends = [struct.unpack_from('>H', data, table + 14 + i * 2)[0] for i in range(segments)]
            starts = [struct.unpack_from('>H', data, table + 16 + segments * 2 + i * 2)[0] for i in range(segments)]
            deltas = [struct.unpack_from('>h', data, table + 16 + segments * 4 + i * 2)[0] for i in range(segments)]
            range_base = table + 16 + segments * 6
            ranges = [struct.unpack_from('>H', data, range_base + i * 2)[0] for i in range(segments)]
            mapping = {}
            for i in range(segments):
                for code in range(starts[i], min(ends[i], 0xfffe) + 1):
                    if ranges[i]:
                        at = range_base + i * 2 + ranges[i] + (code - starts[i]) * 2
                        glyph = struct.unpack_from('>H', data, at)[0]
                        if glyph:
                            glyph = (glyph + deltas[i]) & 0xffff
                    else:
                        glyph = (code + deltas[i]) & 0xffff
                    if glyph:
                        mapping[code] = glyph
            # Marlett is a symbol font: its cmap lives in the F0xx private area.
            for code in list(mapping):
                if 0xf000 <= code <= 0xf0ff:
                    mapping.setdefault(code & 0xff, mapping[code])
            return mapping
        raise ValueError('MARLETT.TTF has no format 4 cmap')

    def contours(self, glyph, transform=(1.0, 0.0, 0.0, 1.0, 0.0, 0.0)):
        """Flattened outlines of one glyph in font units."""
        data = self.data
        start, end = self.loca[glyph], self.loca[glyph + 1]
        if start >= end:
            return []
        base = self.tables['glyf'][0] + start
        count = struct.unpack_from('>h', data, base)[0]
        a, b, c, d, e, f = transform
        def place(x, y):
            return (a * x + c * y + e, b * x + d * y + f)
        if count < 0:
            return self._composite(base + 10, transform)
        ends = [struct.unpack_from('>H', data, base + 10 + i * 2)[0] for i in range(count)]
        points = ends[-1] + 1 if ends else 0
        at = base + 10 + count * 2
        at += 2 + struct.unpack_from('>H', data, at)[0]
        flags = []
        while len(flags) < points:
            flag = data[at]; at += 1
            flags.append(flag)
            if flag & 8:
                repeat = data[at]; at += 1
                flags.extend([flag] * repeat)
        def coordinates(short_bit, same_bit):
            nonlocal at
            values, value = [], 0
            for flag in flags:
                if flag & short_bit:
                    delta = data[at]; at += 1
                    value += delta if flag & same_bit else -delta
                elif not flag & same_bit:
                    value += struct.unpack_from('>h', data, at)[0]; at += 2
                values.append(value)
            return values
        xs = coordinates(2, 16)
        ys = coordinates(4, 32)
        result, first = [], 0
        for last in ends:
            raw = [(place(xs[i], ys[i]), bool(flags[i] & 1)) for i in range(first, last + 1)]
            first = last + 1
            if raw:
                result.append(_flatten(raw))
        return result

    def _composite(self, at, transform):
        data = self.data
        result = []
        while True:
            flags, index = struct.unpack_from('>HH', data, at); at += 4
            if flags & 1:
                dx, dy = struct.unpack_from('>hh', data, at); at += 4
            else:
                dx, dy = struct.unpack_from('>bb', data, at); at += 2
            a = d = 1.0; b = c = 0.0
            if flags & 8:
                a = d = struct.unpack_from('>h', data, at)[0] / 16384.0; at += 2
            elif flags & 0x40:
                a = struct.unpack_from('>h', data, at)[0] / 16384.0
                d = struct.unpack_from('>h', data, at + 2)[0] / 16384.0; at += 4
            elif flags & 0x80:
                a, b, c, d = [v / 16384.0 for v in struct.unpack_from('>4h', data, at)]; at += 8
            outer = transform
            inner = (a * outer[0] + b * outer[2], a * outer[1] + b * outer[3],
                     c * outer[0] + d * outer[2], c * outer[1] + d * outer[3],
                     dx * outer[0] + dy * outer[2] + outer[4],
                     dx * outer[1] + dy * outer[3] + outer[5])
            result.extend(self.contours(index, inner))
            if not flags & 0x20:
                break
        return result

    def cell(self, code, size):
        """One rendered cell: (rows of pixel bits, width, advance) or None."""
        if size <= 0 or size > 64:
            return None
        glyph = self.cmap.get(ord(code))
        if glyph is None:
            return None
        # usWinAscent plus usWinDescent is the em here, so the requested cell
        # height is the pixel size and the baseline sits on the cell bottom.
        scale = size / float(self.units)
        baseline = self.ascent * scale
        edges = []
        for contour in self.contours(glyph):
            for index in range(len(contour)):
                (x0, y0), (x1, y1) = contour[index], contour[(index + 1) % len(contour)]
                edges.append((x0 * scale, baseline - y0 * scale, x1 * scale, baseline - y1 * scale))
        rows = []
        for row in range(size):
            bits = 0
            for column in range(size):
                if _covered(edges, column + 0.5, row + 0.5):
                    bits |= 1 << column
            rows.append(bits)
        advance = self.advances[glyph if glyph < len(self.advances) else -1]
        return rows, size, max(1, int(round(advance * scale)))


def _flatten(raw, steps=12):
    """Expand quadratic on-/off-curve points into a polygon."""
    points = []
    count = len(raw)
    if not any(on for _p, on in raw):
        raw = [((( raw[0][0][0] + raw[-1][0][0]) / 2.0, (raw[0][0][1] + raw[-1][0][1]) / 2.0), True)] + raw
        count = len(raw)
    start = next(i for i in range(count) if raw[i][1])
    order = [raw[(start + i) % count] for i in range(count)] + [raw[start]]
    current = order[0][0]
    points.append(current)
    index = 1
    while index < len(order):
        point, on = order[index]
        if on:
            points.append(point)
            current = point
            index += 1
            continue
        following, following_on = order[index + 1] if index + 1 < len(order) else order[0]
        if not following_on:
            following = ((point[0] + following[0]) / 2.0, (point[1] + following[1]) / 2.0)
            step = 1
        else:
            step = 2
        for i in range(1, steps + 1):
            t = i / float(steps)
            u = 1.0 - t
            points.append((u * u * current[0] + 2 * u * t * point[0] + t * t * following[0],
                           u * u * current[1] + 2 * u * t * point[1] + t * t * following[1]))
        current = following
        index += step
    return points


def _covered(edges, x, y):
    """Non-zero winding at one pixel center."""
    winding = 0
    for x0, y0, x1, y1 in edges:
        if y0 == y1:
            continue
        if (y0 <= y < y1) or (y1 <= y < y0):
            t = (y - y0) / (y1 - y0)
            if x0 + t * (x1 - x0) > x:
                winding += 1 if y1 > y0 else -1
    return winding != 0


def table(path=None, sizes=range(1, 33)):
    path = path or ROOT / 'build/nonclient-fidelity/fonts/MARLETT.TTF'
    font = Marlett(Path(path).read_bytes())
    cells = {}
    for size in sizes:
        for code in CELLS:
            rendered = font.cell(code, size)
            if rendered:
                cells[(size, code)] = rendered
    return font, cells


def header(cells, sizes, destination):
    lines = ['// Marlett cells rendered locally from the original MARLETT.TTF outlines.',
             '// The original TrueType hinting is not interpreted. Do not redistribute.',
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', type=Path, default=ROOT / 'build/nonclient-fidelity/fonts/MARLETT.TTF')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/generated/marlett_glyphs.h')
    parser.add_argument('--show', action='store_true', help='print the rendered cells')
    parser.add_argument('--sizes', type=int, nargs=2, default=(1, 32))
    arguments = parser.parse_args()
    sizes = list(range(arguments.sizes[0], arguments.sizes[1] + 1))
    font, cells = table(arguments.font, sizes)
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
    digest = hashlib.sha256(arguments.out.read_bytes()).hexdigest()
    print('marlett_glyphs.h %s cells=%d sizes=%d-%d font=%s' %
          (digest, len(cells), min(sizes), max(sizes),
           hashlib.sha256(Path(arguments.font).read_bytes()).hexdigest()))


if __name__ == '__main__':
    main()
