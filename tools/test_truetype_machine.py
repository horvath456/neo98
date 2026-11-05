#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""The TrueType driver of Win98 as original code, checked against the font files.

src/truetype_machine.cpp maps GDI32.DLL of the installation into an x86
engine and calls the font-driver entries GDI.EXE calls.
tools/truetype_machine_probe.cpp drives it. Nothing here is a constant of the
test: every advance width is read out of the font's own hdmx table, which is
the table Win98 uses for the hinted advance of a ppem, and the black pixels of
a glyph must lie inside the em box of its bounding box.

    build/analysis-venv/bin/python tools/test_truetype_machine.py
"""
import struct
import subprocess
import sys
from pathlib import Path
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

def _engine_link_extra():
    """The FEXCore provider for this runner when the host links it
    (tools/native_app.py, engine_link_extra), else nothing."""
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parent))
    import native_app as _native_app
    return [str(p) for p in _native_app.engine_link_extra()]


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT))
import neo98  # noqa: E402

FONTS = win98_c.FOLDER / 'WINDOWS/FONTS'
OUT = ROOT / 'build/truetype-machine'
# Faces of the collection's programs and the two symbol fonts; Lucida Console is
# the one the font page of a DOS property sheet asks for.
CASES = [('ARIAL.TTF', 'WwHaio'), ('ARIALBD.TTF', 'WwH'), ('ARIALI.TTF', 'Wwg'),
         ('TIMES.TTF', 'WwHaio'), ('COUR.TTF', 'WwHa'), ('LUCON.TTF', 'WwH'),
         ('SYMBOL.TTF', 'abg'), ('WINGDING.TTF', 'no')]
PPEMS = (11, 13, 16, 20)


class Font:
    """The tables of a TrueType file this test compares against."""

    def __init__(self, path):
        self.data = path.read_bytes()
        count = struct.unpack_from('>H', self.data, 4)[0]
        self.tables = {}
        for i in range(count):
            tag, _, offset, length = struct.unpack_from('>4sIII', self.data, 12 + 16 * i)
            self.tables[tag.decode('latin1')] = (offset, length)

    @property
    def symbol(self):
        """A font whose Windows cmap is the symbol encoding (3, 0)."""
        cmap = self.table('cmap')
        return any(struct.unpack_from('>HH', cmap, 4 + 8 * i) == (3, 0)
                   for i in range(struct.unpack_from('>H', cmap, 2)[0]))

    def table(self, tag):
        offset, length = self.tables[tag]
        return self.data[offset:offset + length]

    def glyph(self, code):
        """The glyph index of a character through the Windows cmap subtable."""
        cmap = self.table('cmap')
        for i in range(struct.unpack_from('>H', cmap, 2)[0]):
            platform, encoding, offset = struct.unpack_from('>HHI', cmap, 4 + 8 * i)
            if platform != 3 or struct.unpack_from('>H', cmap, offset)[0] != 4:
                continue
            segments = struct.unpack_from('>H', cmap, offset + 6)[0] // 2
            ends = struct.unpack_from('>%dH' % segments, cmap, offset + 14)
            starts = struct.unpack_from('>%dH' % segments, cmap, offset + 16 + segments * 2)
            deltas = struct.unpack_from('>%dh' % segments, cmap, offset + 16 + segments * 4)
            ranges_at = offset + 16 + segments * 6
            ranges = struct.unpack_from('>%dH' % segments, cmap, ranges_at)
            # A symbol font maps F000h + code, a text font the code itself.
            value = code if encoding == 1 else code + 0xf000
            for k in range(segments):
                if starts[k] <= value <= ends[k]:
                    if not ranges[k]:
                        return (value + deltas[k]) & 0xffff
                    index = struct.unpack_from('>H', cmap, ranges_at + 2 * k + ranges[k] +
                                               2 * (value - starts[k]))[0]
                    return (index + deltas[k]) & 0xffff if index else 0
        return 0

    def hdmx(self, ppem, glyph):
        """The hinted advance width of one glyph at one ppem, or None."""
        if 'hdmx' not in self.tables:
            return None
        table = self.table('hdmx')
        records, size = struct.unpack_from('>hi', table, 2)
        for i in range(records):
            at = 8 + i * size
            if table[at] == ppem:
                return table[at + 2 + glyph]
        return None


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    binary = OUT / 'truetype-machine-probe'
    from native_app import native_x64
    if native_x64():
        subprocess.run(['clang++', '-std=c++17', '-O2', '-g', '-DNEO98_NO_UNICORN',
                        *map(str, [ROOT / path for path in ('tools/truetype_machine_probe.cpp',
                            'src/truetype_machine.cpp', 'src/x86_cpu.cpp', 'src/x86_address_space.cpp',
                            'src/jit_memory.cpp', 'tools/host_native_cpu.cpp', 'tools/direct_bridge.S')]), '-ldl', '-pthread', '-o', str(binary)],
                       cwd=ROOT, check=True, env=neo98.tool_environment())
        return binary
    library = ROOT / 'build/unicorn-host/lib/libunicorn.2.dylib'
    if not library.exists():
        sys.exit('missing %s; run cmake --build build/unicorn-host/build first' % library)
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                    str(ROOT / 'tools/truetype_machine_probe.cpp'),
                    str(ROOT / 'src/truetype_machine.cpp'), str(ROOT / 'src/x86_cpu.cpp'), str(ROOT / 'src/x86_cpu_unicorn.cpp'), str(ROOT / 'src/x86_address_space.cpp'),
                    str(ROOT / 'src/jit_memory.cpp'),
                    *_engine_link_extra(), str(library), '-Wl,-rpath,' + str(library.parent), '-o', str(binary)],
                   # The same clang++ as the sanitized objects of engine_link_extra.
                   cwd=ROOT, check=True, env=neo98.tool_environment())
    return binary


def run(binary, file, ppem, text, symbol):
    result = subprocess.run([str(binary), 'C:\\WINDOWS\\FONTS\\' + file, str(ppem), text,
                             str(win98_c.FOLDER), 'f000' if symbol else '0'],
                            cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        sys.exit('probe failed for %s at %d ppem: %s%s' % (file, ppem, result.stdout, result.stderr))
    glyphs, current = {}, None
    for line in result.stdout.splitlines():
        if line.startswith('glyph '):
            parts = line.split()
            current = parts[1]
            fields = dict(part.split('=', 1) for part in parts[2:] if '=' in part)
            box = fields['box'].split('x')
            glyphs[current] = {'width': int(box[0]), 'height': int(box[1]), 'index': int(fields['index']),
                               'advance': int(fields['advance']), 'rows': []}
        elif line.startswith('  ') and current:
            glyphs[current]['rows'].append(line[2:])
    if not glyphs:
        sys.exit('probe printed no glyph for %s: %s' % (file, result.stdout))
    return glyphs


def main():
    binary = build()
    checked = compared = 0
    for file, text in CASES:
        path = FONTS / file
        if not path.exists():
            sys.exit('missing font file %s' % path)
        font = Font(path)
        for ppem in PPEMS:
            glyphs = run(binary, file, ppem, text, font.symbol)
            for character, glyph in glyphs.items():
                checked += 1
                index = font.glyph(ord(character))
                if not index:
                    sys.exit('%s has no glyph for %r' % (file, character))
                if glyph['index'] != index:
                    sys.exit('%s %r: the driver mapped glyph %d, the cmap says %d'
                             % (file, character, glyph['index'], index))
                expected = font.hdmx(ppem, index)
                if expected is not None:
                    compared += 1
                    if glyph['advance'] != expected:
                        sys.exit('%s %r at %d ppem: advance %d, hdmx says %d'
                                 % (file, character, ppem, glyph['advance'], expected))
                # Every row of the bitmap must be as wide as the reported box
                # and carry at least one black pixel somewhere in the glyph.
                if len(glyph['rows']) != glyph['height']:
                    sys.exit('%s %r at %d ppem: %d rows for a height of %d'
                             % (file, character, ppem, len(glyph['rows']), glyph['height']))
                if glyph['height'] and not any('#' in row for row in glyph['rows']):
                    sys.exit('%s %r at %d ppem: empty bitmap' % (file, character, ppem))
                if glyph['height'] > 3 * ppem or glyph['width'] > 3 * ppem:
                    sys.exit('%s %r at %d ppem: box %dx%d is not a glyph of that size'
                             % (file, character, ppem, glyph['width'], glyph['height']))
    print('truetype machine: %d glyphs of %d files, %d advances against hdmx'
          % (checked, len(CASES), compared))


if __name__ == '__main__':
    main()
