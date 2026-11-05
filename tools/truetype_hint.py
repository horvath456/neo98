#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""A TrueType instruction interpreter and monochrome scan converter.

The Win98 frame controls are Marlett cells. USER.EXE only names a character and
a cell size; the pixels come from the TrueType rasterizer GDI owns. That
rasterizer could not be driven directly this round (for the evidence), so this module executes the font's own hinting programs and
scan-converts the result with the classic TrueType rules:

  * the control value table is scaled to the requested ppem,
  * `fpgm` runs once per font, `prep` once per ppem, the glyph program per glyph,
  * untouched points are interpolated (IUP) exactly as the spec requires,
  * the monochrome scan converter samples pixel centres and applies the dropout
    control the font asks for through SCANCTRL and SCANTYPE.

Every number stays in the font's own fixed-point formats: 26.6 for distances and
coordinates, 2.14 for unit vectors, 16.16 for the scaling factor. The arithmetic
mirrors the reference interpreter, including its rounding of MulDiv and the
sign rules of the super-rounding states.

What this proves and what it does not is stated in
The interpreter is checked against three tables the font itself carries, all
produced by Microsoft's own rasterizer when the font was built: `hdmx` (the
hinted device advance width per ppem), `VDMX` (the hinted vertical extremes per
ppem) and `LTSH` (the ppem from which advances become linear).
"""
import math
import struct
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# fixed point helpers
# ---------------------------------------------------------------------------

ONE16 = 1 << 16
ONE14 = 1 << 14


def s32(value):
    """Truncate to the 32-bit signed arithmetic the interpreter is defined in."""
    value &= 0xffffffff
    return value - (1 << 32) if value & 0x80000000 else value


def mul_div(a, b, c):
    """Reference MulDiv: multiply, divide, round the magnitude to nearest."""
    if c == 0:
        return 0x7fffffff if (a < 0) == (b < 0) else -0x7fffffff
    # Exclusive or of the three signs. A chained `!=` would compare
    # pairwise and call three negative operands positive.
    negative = (a < 0) ^ (b < 0) ^ (c < 0)
    a, b, c = abs(a), abs(b), abs(c)
    value = (a * b + (c >> 1)) // c
    return s32(-value if negative else value)


def mul_fix(a, b):
    """16.16 multiply with rounding; the scaler's FT_MulFix."""
    negative = (a < 0) != (b < 0)
    value = (abs(a) * abs(b) + 0x8000) >> 16
    return s32(-value if negative else value)


def dot14(x, y, ax, ay):
    """Project (x, y) in 26.6 onto a 2.14 unit vector."""
    return s32((x * ax + y * ay + 0x2000) >> 14)


def pix_round(value):
    return (value + 32) & -64


def pix_floor(value):
    return value & -64


def pix_ceil(value):
    return (value + 63) & -64


# ---------------------------------------------------------------------------
# font file
# ---------------------------------------------------------------------------

class Sfnt:
    """The tables of one TrueType file, unchanged."""

    def __init__(self, data):
        self.data = data
        count = struct.unpack_from('>H', data, 4)[0]
        self.tables = {}
        for index in range(count):
            tag, _sum, offset, length = struct.unpack_from('>4sIII', data, 12 + index * 16)
            self.tables[tag.decode('latin1')] = (offset, length)
        head = self.tables['head'][0]
        self.units = struct.unpack_from('>H', data, head + 18)[0]
        self.head_flags = struct.unpack_from('>H', data, head + 16)[0]
        self.long_loca = struct.unpack_from('>h', data, head + 50)[0]
        maxp = self.tables['maxp'][0]
        values = struct.unpack_from('>14H', data, maxp + 4)
        (self.glyph_count, self.max_points, self.max_contours, self.max_composite_points,
         self.max_composite_contours, self.max_zones, self.max_twilight, self.max_storage,
         self.max_functions, self.max_instruction_defs, self.max_stack,
         self.max_instructions, self.max_components, self.max_depth) = values
        loca = self.tables['loca'][0]
        if self.long_loca:
            self.loca = [struct.unpack_from('>I', data, loca + i * 4)[0]
                         for i in range(self.glyph_count + 1)]
        else:
            self.loca = [struct.unpack_from('>H', data, loca + i * 2)[0] * 2
                         for i in range(self.glyph_count + 1)]
        hhea = self.tables['hhea'][0]
        self.ascender = struct.unpack_from('>h', data, hhea + 4)[0]
        self.descender = struct.unpack_from('>h', data, hhea + 6)[0]
        metrics = struct.unpack_from('>H', data, hhea + 34)[0]
        hmtx = self.tables['hmtx'][0]
        self.metrics = []
        for index in range(metrics):
            advance, bearing = struct.unpack_from('>Hh', data, hmtx + index * 4)
            self.metrics.append((advance, bearing))
        if 'OS/2' in self.tables:
            os2 = self.tables['OS/2'][0]
            self.win_ascent = struct.unpack_from('>H', data, os2 + 74)[0]
            self.win_descent = struct.unpack_from('>H', data, os2 + 76)[0]
        else:
            self.win_ascent, self.win_descent = self.ascender, -self.descender
        self.cmap = self._cmap()
        self.fpgm = self._table('fpgm')
        self.prep = self._table('prep')
        self.cvt = []
        if 'cvt ' in self.tables:
            offset, length = self.tables['cvt ']
            self.cvt = [struct.unpack_from('>h', data, offset + i * 2)[0]
                        for i in range(length // 2)]
        self.gasp = self._gasp()

    def _table(self, tag):
        if tag not in self.tables:
            return b''
        offset, length = self.tables[tag]
        return self.data[offset:offset + length]

    def _gasp(self):
        if 'gasp' not in self.tables:
            return None
        base = self.tables['gasp'][0]
        _version, ranges = struct.unpack_from('>2H', self.data, base)
        out = []
        for index in range(ranges):
            ppem, behaviour = struct.unpack_from('>2H', self.data, base + 4 + index * 4)
            out.append((ppem, behaviour))
        return out

    def _cmap(self):
        data = self.data
        base = self.tables['cmap'][0]
        count = struct.unpack_from('>H', data, base + 2)[0]
        for index in range(count):
            _platform, _encoding, offset = struct.unpack_from('>HHI', data, base + 4 + index * 8)
            table = base + offset
            if struct.unpack_from('>H', data, table)[0] != 4:
                continue
            segments = struct.unpack_from('>H', data, table + 6)[0] // 2
            ends = [struct.unpack_from('>H', data, table + 14 + i * 2)[0] for i in range(segments)]
            starts = [struct.unpack_from('>H', data, table + 16 + segments * 2 + i * 2)[0]
                      for i in range(segments)]
            deltas = [struct.unpack_from('>h', data, table + 16 + segments * 4 + i * 2)[0]
                      for i in range(segments)]
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
            # A symbol font keeps its cmap in the F0xx private use area.
            for code in list(mapping):
                if 0xf000 <= code <= 0xf0ff:
                    mapping.setdefault(code & 0xff, mapping[code])
            return mapping
        raise ValueError('No format 4 cmap')

    def advance(self, glyph):
        if not self.metrics:
            return 0, 0
        return self.metrics[glyph] if glyph < len(self.metrics) else self.metrics[-1]

    # -- hinted reference tables the font itself carries --------------------

    def hdmx(self):
        """Per-ppem device advance widths Microsoft's rasterizer measured."""
        if 'hdmx' not in self.tables:
            return {}
        base, _length = self.tables['hdmx']
        _version, records, size = struct.unpack_from('>hhi', self.data, base)
        out = {}
        for index in range(records):
            at = base + 8 + index * size
            ppem = self.data[at]
            out[ppem] = list(self.data[at + 2:at + 2 + self.glyph_count])
        return out

    def ltsh(self):
        """Per-glyph ppem above which the advance scales linearly."""
        if 'LTSH' not in self.tables:
            return []
        base, _length = self.tables['LTSH']
        _version, count = struct.unpack_from('>2H', self.data, base)
        return list(self.data[base + 4:base + 4 + count])

    def vdmx(self):
        """Per-ppem hinted vertical extremes, as recorded when the font was built."""
        if 'VDMX' not in self.tables:
            return {}
        base, _length = self.tables['VDMX']
        _version, num_recs, num_ratios = struct.unpack_from('>3H', self.data, base)
        at = base + 6 + num_ratios * 4
        offsets = [struct.unpack_from('>H', self.data, at + i * 2)[0] for i in range(num_ratios)]
        out = {}
        for offset in offsets:
            group = base + offset
            records, _start, _end = struct.unpack_from('>H2B', self.data, group)
            for index in range(records):
                ppem, ymax, ymin = struct.unpack_from('>Hhh', self.data, group + 4 + index * 6)
                out.setdefault(ppem, (ymax, ymin))
        return out

    # -- outlines ----------------------------------------------------------

    def outline(self, glyph):
        """Unscaled points, on-curve flags and contour ends of one glyph."""
        start, end = self.loca[glyph], self.loca[glyph + 1]
        if start >= end:
            return [], [], [], (0, 0, 0, 0), b''
        base = self.tables['glyf'][0] + start
        data = self.data
        count = struct.unpack_from('>h', data, base)[0]
        box = struct.unpack_from('>4h', data, base + 2)
        if count < 0:
            raise NotImplementedError('Composite glyphs are outside this font')
        ends = [struct.unpack_from('>H', data, base + 10 + i * 2)[0] for i in range(count)]
        points = ends[-1] + 1 if ends else 0
        at = base + 10 + count * 2
        instruction_length = struct.unpack_from('>H', data, at)[0]
        at += 2
        instructions = data[at:at + instruction_length]
        at += instruction_length
        flags = []
        while len(flags) < points:
            flag = data[at]
            at += 1
            flags.append(flag)
            if flag & 8:
                repeat = data[at]
                at += 1
                flags.extend([flag] * repeat)
        flags = flags[:points]

        def coordinates(short_bit, same_bit):
            nonlocal at
            values, value = [], 0
            for flag in flags:
                if flag & short_bit:
                    delta = data[at]
                    at += 1
                    value += delta if flag & same_bit else -delta
                elif not flag & same_bit:
                    value += struct.unpack_from('>h', data, at)[0]
                    at += 2
                values.append(value)
            return values

        xs = coordinates(2, 16)
        ys = coordinates(4, 32)
        on = [bool(flag & 1) for flag in flags]
        return list(zip(xs, ys)), on, ends, box, instructions


# ---------------------------------------------------------------------------
# the interpreter
# ---------------------------------------------------------------------------

class Zone:
    """One point zone: original, scaled-original and current coordinates."""

    def __init__(self, size):
        self.orus = [[0, 0] for _ in range(size)]     # font units, unscaled
        self.org = [[0, 0] for _ in range(size)]      # scaled, unhinted
        self.cur = [[0, 0] for _ in range(size)]      # hinted
        self.on = [True] * size
        self.touch = [0] * size                       # 1 = x, 2 = y
        self.ends = []

    @property
    def count(self):
        return len(self.cur)


class Failure(Exception):
    pass


class Interpreter:
    """The TrueType virtual machine for one font at one ppem."""

    MAX_STEPS = 4000000

    def __init__(self, font, ppem):
        self.font = font
        self.ppem = ppem
        self.scale = mul_div(ppem, ONE16, font.units)   # 16.16 units -> 26.6 pixels
        self.storage = [0] * max(1, font.max_storage)
        self.functions = {}
        self.stack = []
        self.cvt = [self.scale_value(value) for value in font.cvt]
        self.twilight = Zone(max(1, font.max_twilight))
        self.glyph = Zone(1)
        self.zones = [self.twilight, self.glyph]
        self.reset_graphics()
        self.scan_control = False
        self.scan_type = 0
        self.instruction_control = 0
        self.steps = 0
        self.call_depth = 0
        if font.fpgm:
            self.run(font.fpgm, 'fpgm')
        self.saved = self.snapshot()
        if font.prep:
            self.run(font.prep, 'prep')
        self.after_prep = self.snapshot()

    # -- state -------------------------------------------------------------

    def reset_graphics(self):
        self.pv = [ONE14, 0]           # projection vector, 2.14
        self.fv = [ONE14, 0]           # freedom vector
        self.dv = [ONE14, 0]           # dual projection vector
        self.rp = [0, 0, 0]
        self.zp = [1, 1, 1]
        self.loop = 1
        self.round_state = 'RTG'
        self.period, self.phase, self.threshold = 64, 0, 32
        self.minimum_distance = 64
        self.control_value_cut_in = 68     # 17/16 pixel, the default
        self.single_width_cut_in = 0
        self.single_width_value = 0
        self.delta_base = 9
        self.delta_shift = 3
        self.auto_flip = True
        self.update_projection()

    def snapshot(self):
        return {
            'storage': list(self.storage), 'cvt': list(self.cvt),
            'functions': dict(self.functions), 'scan_control': self.scan_control,
            'scan_type': self.scan_type, 'instruction_control': self.instruction_control,
            'round_state': self.round_state, 'period': self.period, 'phase': self.phase,
            'threshold': self.threshold, 'minimum_distance': self.minimum_distance,
            'control_value_cut_in': self.control_value_cut_in,
            'single_width_cut_in': self.single_width_cut_in,
            'single_width_value': self.single_width_value,
            'delta_base': self.delta_base, 'delta_shift': self.delta_shift,
            'auto_flip': self.auto_flip}

    def restore(self, state):
        self.storage = list(state['storage'])
        self.cvt = list(state['cvt'])
        self.functions = dict(state['functions'])
        for name in ('scan_control', 'scan_type', 'instruction_control', 'round_state',
                     'period', 'phase', 'threshold', 'minimum_distance',
                     'control_value_cut_in', 'single_width_cut_in', 'single_width_value',
                     'delta_base', 'delta_shift', 'auto_flip'):
            setattr(self, name, state[name])
        self.pv = [ONE14, 0]
        self.fv = [ONE14, 0]
        self.dv = [ONE14, 0]
        self.rp = [0, 0, 0]
        self.zp = [1, 1, 1]
        self.loop = 1
        self.update_projection()

    def scale_value(self, value):
        return mul_fix(value * 64, self.scale)

    def update_projection(self):
        self.f_dot_p = s32((self.pv[0] * self.fv[0] + self.pv[1] * self.fv[1]) >> 14)
        if abs(self.f_dot_p) < 0x400:
            self.f_dot_p = 0x4000

    # -- stack -------------------------------------------------------------

    def push(self, value):
        self.stack.append(s32(value))

    def pop(self):
        if not self.stack:
            raise Failure('stack underflow')
        return self.stack.pop()

    def pop_unsigned(self):
        return self.pop() & 0xffffffff

    # -- geometry ----------------------------------------------------------

    def zone(self, index):
        return self.zones[self.zp[index]]

    def project(self, x, y):
        return dot14(x, y, self.pv[0], self.pv[1])

    def dual_project(self, x, y):
        return dot14(x, y, self.dv[0], self.dv[1])

    def point_project(self, zone, index, original=False):
        point = zone.org[index] if original else zone.cur[index]
        return self.project(point[0], point[1])

    def move(self, zone, index, distance, touch=True):
        if self.fv[0] != 0:
            if self.fv[0] == ONE14 and self.pv[0] == ONE14:
                delta = distance
            else:
                delta = mul_div(distance, self.fv[0], self.f_dot_p)
            zone.cur[index][0] = s32(zone.cur[index][0] + delta)
            if touch:
                zone.touch[index] |= 1
        if self.fv[1] != 0:
            if self.fv[1] == ONE14 and self.pv[1] == ONE14:
                delta = distance
            else:
                delta = mul_div(distance, self.fv[1], self.f_dot_p)
            zone.cur[index][1] = s32(zone.cur[index][1] + delta)
            if touch:
                zone.touch[index] |= 2

    def move_original(self, zone, index, distance):
        if self.fv[0] != 0:
            zone.org[index][0] = s32(zone.org[index][0]
                                     + mul_div(distance, self.fv[0], self.f_dot_p))
        if self.fv[1] != 0:
            zone.org[index][1] = s32(zone.org[index][1]
                                     + mul_div(distance, self.fv[1], self.f_dot_p))

    # -- rounding ----------------------------------------------------------

    def set_round(self, name):
        self.round_state = name
        if name == 'RTG':
            self.period, self.phase, self.threshold = 64, 0, 32
        elif name == 'RTHG':
            self.period, self.phase, self.threshold = 64, 32, 32
        elif name == 'RTDG':
            self.period, self.phase, self.threshold = 32, 0, 16
        elif name == 'RDTG':
            self.period, self.phase, self.threshold = 64, 0, 0
        elif name == 'RUTG':
            self.period, self.phase, self.threshold = 64, 0, 63
        elif name == 'ROFF':
            self.period, self.phase, self.threshold = 0, 0, 0

    def set_super(self, selector, gray45):
        grid = selector & 0xc0
        period = {0x00: 32, 0x40: 64, 0x80: 128, 0xc0: 64}[grid]
        if gray45:
            # A 45 degree grid steps by the pixel diagonal, 64 * sqrt(2).
            period = period * 0x5a82 // 0x4000
        phase = {0x00: 0, 0x10: period // 4, 0x20: period // 2,
                 0x30: period * 3 // 4}[selector & 0x30]
        bits = selector & 0x0f
        threshold = period - 1 if bits == 0 else (bits - 4) * period // 8
        self.round_state = 'SROUND'
        self.period, self.phase, self.threshold = period, phase, threshold

    def round_value(self, distance, compensation=0):
        state = self.round_state
        if state == 'ROFF':
            if distance >= 0:
                value = distance + compensation
                return max(value, 0)
            value = distance - compensation
            return min(value, 0)
        if state == 'RTG':
            if distance >= 0:
                return max(pix_round(distance + compensation), 0)
            return min(-pix_round(compensation - distance), 0)
        if state == 'RTHG':
            if distance >= 0:
                return max(pix_floor(distance + compensation) + 32, 32)
            return min(-(pix_floor(compensation - distance) + 32), -32)
        if state == 'RTDG':
            if distance >= 0:
                return max(((distance + compensation + 16) & -32), 0)
            return min(-((compensation - distance + 16) & -32), 0)
        if state == 'RDTG':
            if distance >= 0:
                return max(pix_floor(distance + compensation), 0)
            return min(-pix_floor(compensation - distance), 0)
        if state == 'RUTG':
            if distance >= 0:
                return max(pix_ceil(distance + compensation), 0)
            return min(-pix_ceil(compensation - distance), 0)
        # Super rounding, the general case.
        period = self.period or 64
        if distance >= 0:
            value = (distance - self.phase + self.threshold + compensation) & -period
            value += self.phase
            if value < 0:
                value = self.phase
        else:
            value = -((self.threshold - self.phase - distance + compensation) & -period)
            value -= self.phase
            if value > 0:
                value = -self.phase
        return value

    # -- glyph setup -------------------------------------------------------
    def load_glyph(self, index):
        font = self.font
        points, on, ends, box, instructions = font.outline(index)
        advance, bearing = font.advance(index)
        count = len(points)
        zone = Zone(count + 4)
        for i, (x, y) in enumerate(points):
            zone.orus[i] = [x, y]
            zone.org[i] = [self.scale_value(x), self.scale_value(y)]
            zone.on[i] = on[i]
        # The four phantom points: pen origin, advance, and the vertical pair.
        x_min = box[0] if count else 0
        # head.flags bit 1 places the left sidebearing point at x=0.
        # MARLETT sets it even where hmtx's bearing differs from xMin.
        pp1 = 0 if font.head_flags & 2 else x_min - bearing
        pp2 = pp1 + advance
        top = font.win_ascent
        bottom = -font.win_descent
        for index, (x, y) in enumerate(((pp1, 0), (pp2, 0), (0, top), (0, bottom))):
            zone.orus[count + index] = [x, y]
            zone.org[count + index] = [self.scale_value(x), self.scale_value(y)]
        for i in range(count, count + 4):
            zone.on[i] = True
        if self.font.head_flags & 8:
            # "Force ppem to integer values": the phantom points are grid aligned.
            zone.org[count][0] = pix_round(zone.org[count][0])
            zone.org[count + 1][0] = pix_round(zone.org[count + 1][0])
        zone.cur = [list(point) for point in zone.org]
        zone.ends = list(ends)
        self.glyph = zone
        self.zones[1] = zone
        self.point_count = count
        self.box = box
        return instructions

    def hint(self, index):
        self.restore(self.after_prep)
        instructions = self.load_glyph(index)
        if instructions and not (self.instruction_control & 1):
            self.run(instructions, 'glyf')
            self.interpolate_untouched()
        return self.glyph

    # -- IUP ---------------------------------------------------------------

    def interpolate_untouched(self):
        zone = self.glyph
        first = 0
        for last in zone.ends:
            self._iup_contour(zone, first, last, 0)
            self._iup_contour(zone, first, last, 1)
            first = last + 1

    def _iup_contour(self, zone, first, last, axis):
        mask = 1 if axis == 0 else 2
        touched = [i for i in range(first, last + 1) if zone.touch[i] & mask]
        if not touched:
            return
        if len(touched) == 1:
            only = touched[0]
            delta = zone.cur[only][axis] - zone.org[only][axis]
            if delta:
                for i in range(first, last + 1):
                    if i != only:
                        zone.cur[i][axis] = s32(zone.cur[i][axis] + delta)
            return
        count = last - first + 1
        for position in range(len(touched)):
            start = touched[position]
            end = touched[(position + 1) % len(touched)]
            index = start + 1
            while True:
                if index > last:
                    index = first
                if index == end:
                    break
                self._iup_point(zone, index, start, end, axis)
                index += 1
            if count == 1:
                break

    @staticmethod
    def _iup_point(zone, index, lower, upper, axis):
        org_l, org_u = zone.org[lower][axis], zone.org[upper][axis]
        cur_l, cur_u = zone.cur[lower][axis], zone.cur[upper][axis]
        if org_l > org_u:
            org_l, org_u = org_u, org_l
            cur_l, cur_u = cur_u, cur_l
        value = zone.org[index][axis]
        if value <= org_l:
            zone.cur[index][axis] = s32(cur_l + (value - org_l))
        elif value >= org_u:
            zone.cur[index][axis] = s32(cur_u + (value - org_u))
        elif org_u == org_l:
            zone.cur[index][axis] = cur_l
        else:
            zone.cur[index][axis] = s32(cur_l + mul_div(value - org_l, cur_u - cur_l,
                                                        org_u - org_l))

    # -- execution ---------------------------------------------------------

    def run(self, code, which):
        self.code = code
        self.which = which
        self.ip = 0
        self.call_stack = []
        self.stack = []
        self.execute()

    def execute(self):
        while self.ip < len(self.code):
            self.steps += 1
            if self.steps > self.MAX_STEPS:
                raise Failure('instruction budget exhausted')
            opcode = self.code[self.ip]
            self.ip += 1
            self.dispatch(opcode)

    # A call stack entry is (caller code, return ip, remaining loops, body start).

    def do_call(self, number, loops=1):
        if number not in self.functions:
            raise Failure('undefined function %d' % number)
        body, start = self.functions[number]
        if len(self.call_stack) >= 128:
            raise Failure('call stack overflow')
        self.call_stack.append((self.code, self.ip, loops, start))
        self.code = body
        self.ip = start

    def skip_to(self, *targets):
        """Walk forward over whole instructions to a matching ELSE/EIF."""
        depth = 0
        code = self.code
        while self.ip < len(code):
            opcode = code[self.ip]
            self.ip += 1
            if opcode == 0x58:                 # IF
                depth += 1
            elif opcode == 0x59:               # EIF
                if depth == 0 and 0x59 in targets:
                    return 0x59
                depth -= 1
            elif opcode == 0x1b:               # ELSE
                if depth == 0 and 0x1b in targets:
                    return 0x1b
            elif opcode == 0x40 or opcode == 0x41:
                count = code[self.ip]
                self.ip += 1 + count * (2 if opcode == 0x41 else 1)
            elif 0xb0 <= opcode <= 0xb7:
                self.ip += opcode - 0xb0 + 1
            elif 0xb8 <= opcode <= 0xbf:
                self.ip += (opcode - 0xb8 + 1) * 2
        raise Failure('unbalanced IF')

    def dispatch(self, opcode):
        code = self.code
        pop, push = self.pop, self.push
        # --- pushes
        if 0xb0 <= opcode <= 0xb7:
            for _ in range(opcode - 0xb0 + 1):
                push(code[self.ip])
                self.ip += 1
            return
        if 0xb8 <= opcode <= 0xbf:
            for _ in range(opcode - 0xb8 + 1):
                push(struct.unpack_from('>h', code, self.ip)[0])
                self.ip += 2
            return
        if opcode == 0x40:                      # NPUSHB
            count = code[self.ip]
            self.ip += 1
            for _ in range(count):
                push(code[self.ip])
                self.ip += 1
            return
        if opcode == 0x41:                      # NPUSHW
            count = code[self.ip]
            self.ip += 1
            for _ in range(count):
                push(struct.unpack_from('>h', code, self.ip)[0])
                self.ip += 2
            return
        # --- vectors
        if opcode in (0x00, 0x01):              # SVTCA
            vector = [ONE14, 0] if opcode & 1 else [0, ONE14]
            self.pv, self.fv, self.dv = list(vector), list(vector), list(vector)
            self.update_projection()
            return
        if opcode in (0x02, 0x03):              # SPVTCA
            axis = opcode & 1
            self.pv = [ONE14, 0] if axis else [0, ONE14]
            self.dv = list(self.pv)
            self.update_projection()
            return
        if opcode in (0x04, 0x05):              # SFVTCA
            axis = opcode & 1
            self.fv = [ONE14, 0] if axis else [0, ONE14]
            self.update_projection()
            return
        if opcode in (0x06, 0x07, 0x08, 0x09, 0x86, 0x87):
            return self.set_vector_to_line(opcode)
        if opcode == 0x0a:                      # SPVFS
            y, x = pop(), pop()
            self.pv = self.normalize(x, y)
            self.dv = list(self.pv)
            self.update_projection()
            return
        if opcode == 0x0b:                      # SFVFS
            y, x = pop(), pop()
            self.fv = self.normalize(x, y)
            self.update_projection()
            return
        if opcode == 0x0c:                      # GPV
            push(self.pv[0])
            push(self.pv[1])
            return
        if opcode == 0x0d:                      # GFV
            push(self.fv[0])
            push(self.fv[1])
            return
        if opcode == 0x0e:                      # SFVTPV
            self.fv = list(self.pv)
            self.update_projection()
            return
        if opcode == 0x0f:                      # ISECT
            return self.isect()
        # --- reference points and zones
        if opcode in (0x10, 0x11, 0x12):        # SRP0..2
            self.rp[opcode - 0x10] = pop()
            return
        if opcode in (0x13, 0x14, 0x15):        # SZP0..2
            value = pop()
            if value not in (0, 1):
                raise Failure('bad zone %d' % value)
            self.zp[opcode - 0x13] = value
            return
        if opcode == 0x16:                      # SZPS
            value = pop()
            if value not in (0, 1):
                raise Failure('bad zone %d' % value)
            self.zp = [value, value, value]
            return
        if opcode == 0x17:                      # SLOOP
            value = pop()
            self.loop = value if value > 0 else 1
            return
        if opcode == 0x18:
            self.set_round('RTG')
            return
        if opcode == 0x19:
            self.set_round('RTHG')
            return
        if opcode == 0x3d:
            self.set_round('RTDG')
            return
        if opcode == 0x7a:
            self.set_round('ROFF')
            return
        if opcode == 0x7c:
            self.set_round('RUTG')
            return
        if opcode == 0x7d:
            self.set_round('RDTG')
            return
        if opcode == 0x76:                      # SROUND
            self.set_super(pop() & 0xff, False)
            return
        if opcode == 0x77:                      # S45ROUND
            self.set_super(pop() & 0xff, True)
            return
        if opcode == 0x1a:                      # SMD
            self.minimum_distance = pop()
            return
        if opcode == 0x1d:                      # SCVTCI
            self.control_value_cut_in = pop()
            return
        if opcode == 0x1e:                      # SSWCI
            self.single_width_cut_in = pop()
            return
        if opcode == 0x1f:                      # SSW
            self.single_width_value = self.scale_value(pop())
            return
        # --- stack shuffling
        if opcode == 0x20:                      # DUP
            value = pop()
            push(value)
            push(value)
            return
        if opcode == 0x21:
            pop()
            return
        if opcode == 0x22:                      # CLEAR
            self.stack = []
            return
        if opcode == 0x23:                      # SWAP
            a, b = pop(), pop()
            push(a)
            push(b)
            return
        if opcode == 0x24:                      # DEPTH
            push(len(self.stack))
            return
        if opcode == 0x25:                      # CINDEX
            index = pop()
            if index <= 0 or index > len(self.stack):
                raise Failure('CINDEX out of range')
            push(self.stack[-index])
            return
        if opcode == 0x26:                      # MINDEX
            index = pop()
            if index <= 0 or index > len(self.stack):
                raise Failure('MINDEX out of range')
            value = self.stack.pop(len(self.stack) - index)
            push(value)
            return
        if opcode == 0x8a:                      # ROLL
            a, b, c = pop(), pop(), pop()
            push(b)
            push(a)
            push(c)
            return
        # --- flow
        if opcode == 0x1b:                      # ELSE (taken branch falls through)
            self.skip_to(0x59)
            return
        if opcode == 0x58:                      # IF
            if pop() == 0:
                if self.skip_to(0x1b, 0x59) == 0x59:
                    pass
            return
        if opcode == 0x59:                      # EIF
            return
        if opcode == 0x1c:                      # JMPR
            offset = pop()
            self.ip += offset - 1
            return
        if opcode == 0x78:                      # JROT
            condition, offset = pop(), pop()
            if condition:
                self.ip += offset - 1
            return
        if opcode == 0x79:                      # JROF
            condition, offset = pop(), pop()
            if not condition:
                self.ip += offset - 1
            return
        if opcode == 0x2c:                      # FDEF
            number = pop()
            start = self.ip
            depth = 0
            while self.ip < len(code):
                current = code[self.ip]
                self.ip += 1
                if current == 0x2c:
                    depth += 1
                elif current == 0x2d:           # ENDF
                    if depth == 0:
                        break
                    depth -= 1
                elif current in (0x40, 0x41):
                    count = code[self.ip]
                    self.ip += 1 + count * (2 if current == 0x41 else 1)
                elif 0xb0 <= current <= 0xb7:
                    self.ip += current - 0xb0 + 1
                elif 0xb8 <= current <= 0xbf:
                    self.ip += (current - 0xb8 + 1) * 2
            self.functions[number] = (code, start)
            return
        if opcode == 0x2d:                      # ENDF
            if not self.call_stack:
                raise Failure('ENDF outside a function')
            caller, return_ip, remaining, start = self.call_stack.pop()
            if remaining > 1:
                self.call_stack.append((caller, return_ip, remaining - 1, start))
                self.ip = start             # the body stays selected
                return
            self.code = caller
            self.ip = return_ip
            return
        if opcode == 0x2b:                      # CALL
            self.do_call(pop())
            return
        if opcode == 0x2a:                      # LOOPCALL
            number = pop()
            count = pop()
            if count > 0:
                self.do_call(number, count)
            return
        if opcode == 0x89:                      # IDEF
            pop()
            raise Failure('IDEF is not supported')
        # --- arithmetic and logic
        if opcode == 0x50:
            b, a = pop(), pop()
            push(1 if a < b else 0)
            return
        if opcode == 0x51:
            b, a = pop(), pop()
            push(1 if a <= b else 0)
            return
        if opcode == 0x52:
            b, a = pop(), pop()
            push(1 if a > b else 0)
            return
        if opcode == 0x53:
            b, a = pop(), pop()
            push(1 if a >= b else 0)
            return
        if opcode == 0x54:
            b, a = pop(), pop()
            push(1 if a == b else 0)
            return
        if opcode == 0x55:
            b, a = pop(), pop()
            push(1 if a != b else 0)
            return
        if opcode == 0x56:                      # ODD
            push(1 if (self.round_value(pop()) & 127) == 64 else 0)
            return
        if opcode == 0x57:                      # EVEN
            push(1 if (self.round_value(pop()) & 127) == 0 else 0)
            return
        if opcode == 0x5a:
            b, a = pop(), pop()
            push(1 if a and b else 0)
            return
        if opcode == 0x5b:
            b, a = pop(), pop()
            push(1 if a or b else 0)
            return
        if opcode == 0x5c:
            push(0 if pop() else 1)
            return
        if opcode == 0x60:
            push(s32(pop() + pop()))
            return
        if opcode == 0x61:
            b, a = pop(), pop()
            push(s32(a - b))
            return
        if opcode == 0x62:                      # DIV
            b, a = pop(), pop()
            if b == 0:
                raise Failure('divide by zero')
            push(mul_div(a, 64, b))
            return
        if opcode == 0x63:                      # MUL
            b, a = pop(), pop()
            push(mul_div(a, b, 64))
            return
        if opcode == 0x64:
            push(abs(pop()))
            return
        if opcode == 0x65:
            push(s32(-pop()))
            return
        if opcode == 0x66:
            push(pix_floor(pop()))
            return
        if opcode == 0x67:
            push(pix_ceil(pop()))
            return
        if 0x68 <= opcode <= 0x6b:              # ROUND
            push(self.round_value(pop()))
            return
        if 0x6c <= opcode <= 0x6f:              # NROUND
            push(pop())
            return
        if opcode == 0x8b:
            b, a = pop(), pop()
            push(max(a, b))
            return
        if opcode == 0x8c:
            b, a = pop(), pop()
            push(min(a, b))
            return
        # --- storage and control values
        if opcode == 0x42:                      # WS
            value, index = pop(), pop()
            if 0 <= index < len(self.storage):
                self.storage[index] = value
            return
        if opcode == 0x43:                      # RS
            index = pop()
            push(self.storage[index] if 0 <= index < len(self.storage) else 0)
            return
        if opcode == 0x44:                      # WCVTP
            value, index = pop(), pop()
            if 0 <= index < len(self.cvt):
                self.cvt[index] = value
            return
        if opcode == 0x70:                      # WCVTF
            value, index = pop(), pop()
            if 0 <= index < len(self.cvt):
                self.cvt[index] = self.scale_value(value)
            return
        if opcode == 0x45:                      # RCVT
            index = pop()
            push(self.cvt[index] if 0 <= index < len(self.cvt) else 0)
            return
        if opcode == 0x4b:                      # MPPEM
            push(self.ppem)
            return
        if opcode == 0x4c:                      # MPS
            push(self.ppem * 72 // 96)
            return
        if opcode == 0x4d:
            self.auto_flip = True
            return
        if opcode == 0x4e:
            self.auto_flip = False
            return
        if opcode == 0x4f:                      # DEBUG
            pop()
            return
        if opcode == 0x5e:                      # SDB
            self.delta_base = pop()
            return
        if opcode == 0x5f:                      # SDS
            self.delta_shift = pop()
            return
        if opcode == 0x85:                      # SCANCTRL
            word = pop() & 0xffff
            threshold = word & 0xff
            if threshold == 0xff:
                self.scan_control = True
            elif threshold == 0:
                self.scan_control = False
            else:
                # Only the ppem rules can fire here: the cells are never
                # rotated and never stretched.
                if word & 0x0100 and self.ppem <= threshold:
                    self.scan_control = True
                if word & 0x0800 and self.ppem > threshold:
                    self.scan_control = False
            return
        if opcode == 0x8d:                      # SCANTYPE
            self.scan_type = pop()
            return
        if opcode == 0x8e:                      # INSTCTRL
            selector, value = pop(), pop()
            del value
            del selector
            return
        if opcode == 0x88:                      # GETINFO
            selector = pop()
            result = 0
            if selector & 1:
                result |= 3                     # scaler version, the Win9x family
            if selector & 32:
                result |= 0                     # not a grayscale rasterizer
            push(result)
            return
        if opcode == 0x7e:                      # SANGW, obsolete
            pop()
            return
        if opcode == 0x7f:                      # AA, obsolete
            pop()
            return
        # --- point measurement
        if opcode in (0x46, 0x47):              # GC
            index = pop()
            zone = self.zone(2)
            if opcode == 0x46:
                push(self.project(*zone.cur[index]))
            else:
                push(self.dual_project(*zone.org[index]))
            return
        if opcode == 0x48:                      # SCFS
            value, index = pop(), pop()
            zone = self.zone(2)
            current = self.project(*zone.cur[index])
            self.move(zone, index, value - current)
            if self.zp[2] == 0:
                zone.org[index] = list(zone.cur[index])
            return
        if opcode in (0x49, 0x4a):              # MD
            second, first = pop(), pop()
            zone0, zone1 = self.zone(0), self.zone(1)
            if opcode == 0x49:
                push(self.project(zone0.cur[first][0] - zone1.cur[second][0],
                                  zone0.cur[first][1] - zone1.cur[second][1]))
            else:
                push(self.dual_project(*self.unscaled_delta(zone0, first, zone1, second)))
            return
        if opcode == 0x29:                      # UTP
            index = pop()
            zone = self.zone(0)
            mask = 3
            if self.fv[0] != 0:
                mask &= ~1
            if self.fv[1] != 0:
                mask &= ~2
            zone.touch[index] &= mask
            return
        # --- moving points
        if opcode in (0x2e, 0x2f):              # MDAP
            index = pop()
            zone = self.zone(0)
            distance = 0
            if opcode == 0x2f:
                current = self.project(*zone.cur[index])
                distance = self.round_value(current) - current
            self.move(zone, index, distance)
            self.rp[0] = index
            self.rp[1] = index
            return
        if opcode in (0x3e, 0x3f):              # MIAP
            number, index = pop(), pop()
            zone = self.zone(0)
            value = self.cvt[number] if 0 <= number < len(self.cvt) else 0
            if self.zp[0] == 0:
                # The twilight zone takes the control value as the position.
                zone.org[index] = [mul_div(value, self.fv[0], ONE14),
                                   mul_div(value, self.fv[1], ONE14)]
                zone.cur[index] = list(zone.org[index])
            current = self.project(*zone.cur[index])
            if opcode == 0x3f:
                if abs(value - current) > self.control_value_cut_in:
                    value = current
                value = self.round_value(value)
            self.move(zone, index, value - current)
            self.rp[0] = index
            self.rp[1] = index
            return
        if 0xc0 <= opcode <= 0xdf:              # MDRP
            return self.mdrp(opcode)
        if 0xe0 <= opcode <= 0xff:              # MIRP
            return self.mirp(opcode)
        if opcode in (0x3a, 0x3b):              # MSIRP
            distance, index = pop(), pop()
            zone = self.zone(1)
            reference = self.zone(0)
            if self.zp[1] == 0:
                zone.org[index] = list(reference.org[self.rp[0]])
                self.move_original(zone, index, distance)
                zone.cur[index] = list(zone.org[index])
            current = self.project(zone.cur[index][0] - reference.cur[self.rp[0]][0],
                                   zone.cur[index][1] - reference.cur[self.rp[0]][1])
            self.move(zone, index, distance - current)
            self.rp[1] = self.rp[0]
            self.rp[2] = index
            if opcode == 0x3b:
                self.rp[0] = index
            return
        if opcode == 0x3c:                      # ALIGNRP
            reference = self.zone(0)
            base = self.project(*reference.cur[self.rp[0]])
            zone = self.zone(1)
            for _ in range(self.loop):
                index = pop()
                current = self.project(*zone.cur[index])
                self.move(zone, index, base - current)
            self.loop = 1
            return
        if opcode == 0x27:                      # ALIGNPTS
            second, first = pop(), pop()
            zone0, zone1 = self.zone(0), self.zone(1)
            a = self.project(*zone1.cur[second])
            b = self.project(*zone0.cur[first])
            distance = (b - a) // 2
            self.move(zone1, second, distance)
            self.move(zone0, first, -distance)
            return
        if opcode in (0x32, 0x33):              # SHP
            source = self.zone(1) if opcode == 0x32 else self.zone(0)
            reference = self.rp[2] if opcode == 0x32 else self.rp[1]
            delta = self.shift_delta(source, reference)
            zone = self.zone(2)
            for _ in range(self.loop):
                index = pop()
                self.shift_point(zone, index, delta)
            self.loop = 1
            return
        if opcode in (0x34, 0x35):              # SHC
            contour = pop()
            source = self.zone(1) if opcode == 0x34 else self.zone(0)
            reference = self.rp[2] if opcode == 0x34 else self.rp[1]
            delta = self.shift_delta(source, reference)
            zone = self.zone(2)
            first = 0 if contour == 0 else zone.ends[contour - 1] + 1
            last = zone.ends[contour] if contour < len(zone.ends) else zone.count - 1
            skip = reference if source is zone else -1
            for index in range(first, last + 1):
                if index != skip:
                    self.shift_point(zone, index, delta)
            return
        if opcode in (0x36, 0x37):              # SHZ
            which = pop()
            source = self.zone(1) if opcode == 0x36 else self.zone(0)
            reference = self.rp[2] if opcode == 0x36 else self.rp[1]
            delta = self.shift_delta(source, reference)
            zone = self.zones[which]
            last = zone.count - 1
            if which == 1:
                last = self.point_count + 3
            for index in range(0, last + 1):
                if not (source is zone and index == reference):
                    self.shift_point(zone, index, delta)
            return
        if opcode == 0x38:                      # SHPIX
            amount = pop()
            zone = self.zone(2)
            for _ in range(self.loop):
                index = pop()
                delta = (mul_div(amount, self.fv[0], ONE14),
                         mul_div(amount, self.fv[1], ONE14))
                zone.cur[index][0] = s32(zone.cur[index][0] + delta[0])
                zone.cur[index][1] = s32(zone.cur[index][1] + delta[1])
                if self.fv[0]:
                    zone.touch[index] |= 1
                if self.fv[1]:
                    zone.touch[index] |= 2
            self.loop = 1
            return
        if opcode == 0x39:                      # IP
            return self.interpolate()
        if opcode in (0x5d, 0x71, 0x72):        # DELTAP1..3
            return self.deltap(opcode)
        if opcode in (0x73, 0x74, 0x75):        # DELTAC1..3
            return self.deltac(opcode)
        if opcode in (0x30, 0x31):              # IUP
            return self.iup(opcode & 1)
        if opcode in (0x80, 0x81, 0x82):        # FLIPPT, FLIPRGON, FLIPRGOFF
            return self.flip(opcode)
        raise Failure('unsupported opcode 0x%02x in %s' % (opcode, self.which))

    # -- helpers for the larger instructions --------------------------------

    def flip(self, opcode):
        zone = self.glyph
        if opcode == 0x80:
            for _ in range(self.loop):
                index = self.pop()
                zone.on[index] = not zone.on[index]
            self.loop = 1
            return
        high, low = self.pop(), self.pop()
        for index in range(low, high + 1):
            if 0 <= index < zone.count:
                zone.on[index] = opcode == 0x81

    def normalize(self, x, y):
        if x == 0 and y == 0:
            return [ONE14, 0]
        length = (x * x + y * y) ** 0.5
        return [int(round(x * ONE14 / length)), int(round(y * ONE14 / length))]

    def set_vector_to_line(self, opcode):
        second, first = self.pop(), self.pop()
        zone2, zone1 = self.zone(2), self.zone(1)
        if opcode in (0x86, 0x87):              # SDPVTL: also the dual vector
            ax = zone1.org[second][0] - zone2.org[first][0]
            ay = zone1.org[second][1] - zone2.org[first][1]
            if opcode & 1:
                ax, ay = -ay, ax
            self.dv = self.normalize(ax, ay)
            bx = zone1.cur[second][0] - zone2.cur[first][0]
            by = zone1.cur[second][1] - zone2.cur[first][1]
            if opcode & 1:
                bx, by = -by, bx
            self.pv = self.normalize(bx, by)
            self.update_projection()
            return
        dx = zone1.cur[second][0] - zone2.cur[first][0]
        dy = zone1.cur[second][1] - zone2.cur[first][1]
        if opcode & 1:
            dx, dy = -dy, dx
        vector = self.normalize(dx, dy)
        if opcode in (0x06, 0x07):
            self.pv = vector
            self.dv = list(vector)
        else:
            self.fv = vector
        self.update_projection()

    def isect(self):
        b1, b0, a1, a0, point = (self.pop(), self.pop(), self.pop(), self.pop(), self.pop())
        zone2, zone1, zone0 = self.zone(2), self.zone(1), self.zone(0)
        pa = zone1.cur[a0]
        pb = zone1.cur[a1]
        pc = zone2.cur[b0]
        pd = zone2.cur[b1]
        dax, day = pb[0] - pa[0], pb[1] - pa[1]
        dbx, dby = pd[0] - pc[0], pd[1] - pc[1]
        discriminant = mul_div(dax, -dby, 64) + mul_div(day, dbx, 64)
        target = zone0.cur[point]
        if abs(discriminant) >= 0x40:
            dx, dy = pc[0] - pa[0], pc[1] - pa[1]
            value = mul_div(dx, -dby, 64) + mul_div(dy, dbx, 64)
            scale = mul_div(value, 0x10000, discriminant)
            target[0] = s32(pa[0] + mul_fix(dax, scale))
            target[1] = s32(pa[1] + mul_fix(day, scale))
        else:
            target[0] = (pa[0] + pb[0] + pc[0] + pd[0]) // 4
            target[1] = (pa[1] + pb[1] + pc[1] + pd[1]) // 4
        zone0.touch[point] |= 3

    def shift_delta(self, source, reference):
        distance = self.project(source.cur[reference][0] - source.org[reference][0],
                                source.cur[reference][1] - source.org[reference][1])
        return (mul_div(distance, self.fv[0], ONE14), mul_div(distance, self.fv[1], ONE14))

    def shift_point(self, zone, index, delta):
        if self.fv[0]:
            zone.cur[index][0] = s32(zone.cur[index][0] + delta[0])
            zone.touch[index] |= 1
        if self.fv[1]:
            zone.cur[index][1] = s32(zone.cur[index][1] + delta[1])
            zone.touch[index] |= 2

    def interpolate(self):
        zone = self.zone(2)
        base0, base1 = self.zone(0), self.zone(1)
        old_range = self.dual_project(*self.unscaled_delta(base1, self.rp[2], base0, self.rp[1]))
        current_range = self.project(base1.cur[self.rp[2]][0] - base0.cur[self.rp[1]][0],
                                     base1.cur[self.rp[2]][1] - base0.cur[self.rp[1]][1])
        for _ in range(self.loop):
            index = self.pop()
            original = self.dual_project(*self.unscaled_delta(zone, index, base0, self.rp[1]))
            current = self.project(zone.cur[index][0] - base0.cur[self.rp[1]][0],
                                   zone.cur[index][1] - base0.cur[self.rp[1]][1])
            if original == 0:
                new = 0
            elif old_range:
                new = mul_div(original, current_range, old_range)
            else:
                new = current
            self.move(zone, index, new - current)
        self.loop = 1

    def unscaled_delta(self, zone_a, index_a, zone_b, index_b):
        """The distance between two points, scaled once from the font units.

        The reference interpreter scales the difference rather than the two
        coordinates, which keeps one rounding step out of every measurement.
        """
        if zone_a is self.twilight or zone_b is self.twilight:
            return (zone_a.org[index_a][0] - zone_b.org[index_b][0],
                    zone_a.org[index_a][1] - zone_b.org[index_b][1])
        return (mul_fix((zone_a.orus[index_a][0] - zone_b.orus[index_b][0]) * 64, self.scale),
                mul_fix((zone_a.orus[index_a][1] - zone_b.orus[index_b][1]) * 64, self.scale))

    def iup(self, axis):
        zone = self.glyph
        first = 0
        for last in zone.ends:
            self._iup_contour(zone, first, last, 0 if axis else 1)
            first = last + 1

    def deltap(self, opcode):
        base = {0x5d: 0, 0x71: 16, 0x72: 32}[opcode]
        count = self.pop()
        zone = self.zone(0)
        for _ in range(count):
            index = self.pop()
            argument = self.pop()
            ppem = ((argument >> 4) & 0x0f) + self.delta_base + base
            if ppem != self.ppem:
                continue
            magnitude = (argument & 0x0f) - 8
            if magnitude >= 0:
                magnitude += 1
            amount = magnitude * (1 << 6) // (1 << self.delta_shift)
            if 0 <= index < zone.count:
                self.move(zone, index, amount)

    def deltac(self, opcode):
        base = {0x73: 0, 0x74: 16, 0x75: 32}[opcode]
        count = self.pop()
        for _ in range(count):
            index = self.pop()
            argument = self.pop()
            ppem = ((argument >> 4) & 0x0f) + self.delta_base + base
            if ppem != self.ppem:
                continue
            magnitude = (argument & 0x0f) - 8
            if magnitude >= 0:
                magnitude += 1
            amount = magnitude * (1 << 6) // (1 << self.delta_shift)
            if 0 <= index < len(self.cvt):
                self.cvt[index] = s32(self.cvt[index] + amount)

    def mdrp(self, opcode):
        index = self.pop()
        zone = self.zone(1)
        reference = self.zone(0)
        original = self.dual_project(*self.unscaled_delta(zone, index, reference, self.rp[0]))
        if abs(original - self.single_width_value) < self.single_width_cut_in:
            original = self.single_width_value if original >= 0 else -self.single_width_value
        distance = self.round_value(original) if opcode & 4 else original
        if opcode & 8:                          # keep a minimum distance
            if original >= 0:
                distance = max(distance, self.minimum_distance)
            else:
                distance = min(distance, -self.minimum_distance)
        current = self.project(zone.cur[index][0] - reference.cur[self.rp[0]][0],
                               zone.cur[index][1] - reference.cur[self.rp[0]][1])
        self.move(zone, index, distance - current)
        self.rp[1] = self.rp[0]
        self.rp[2] = index
        if opcode & 16:
            self.rp[0] = index

    def mirp(self, opcode):
        number = self.pop()
        index = self.pop()
        zone = self.zone(1)
        reference = self.zone(0)
        value = self.cvt[number] if 0 <= number < len(self.cvt) else 0
        if abs(value - self.single_width_value) < self.single_width_cut_in:
            value = self.single_width_value if value >= 0 else -self.single_width_value
        if self.zp[1] == 0:
            zone.org[index] = [reference.org[self.rp[0]][0] + mul_div(value, self.fv[0], ONE14),
                               reference.org[self.rp[0]][1] + mul_div(value, self.fv[1], ONE14)]
            zone.cur[index] = list(zone.org[index])
        original = self.dual_project(*self.unscaled_delta(zone, index, reference, self.rp[0]))
        current = self.project(zone.cur[index][0] - reference.cur[self.rp[0]][0],
                               zone.cur[index][1] - reference.cur[self.rp[0]][1])
        if self.auto_flip and (original < 0) != (value < 0):
            value = -value
        if opcode & 4:
            if self.zp[1] == self.zp[0] and abs(value - original) > self.control_value_cut_in:
                value = original
            distance = self.round_value(value)
        else:
            distance = value
        if opcode & 8:
            if original >= 0:
                distance = max(distance, self.minimum_distance)
            else:
                distance = min(distance, -self.minimum_distance)
        self.move(zone, index, distance - current)
        self.rp[1] = self.rp[0]
        self.rp[2] = index
        if opcode & 16:
            self.rp[0] = index


# ---------------------------------------------------------------------------
# scan conversion
# ---------------------------------------------------------------------------

def flatten(zone, point_count, steps=8):
    """Expand the hinted quadratic outline into polygons, in 26.6 pixels."""
    contours = []
    first = 0
    for last in zone.ends:
        if last >= point_count:
            break
        raw = [(tuple(zone.cur[i]), zone.on[i]) for i in range(first, last + 1)]
        first = last + 1
        if not raw:
            continue
        contours.append(_flatten_contour(raw, steps))
    return contours


def _flatten_contour(raw, steps):
    count = len(raw)
    if not any(on for _point, on in raw):
        middle = (((raw[0][0][0] + raw[-1][0][0]) // 2, (raw[0][0][1] + raw[-1][0][1]) // 2), True)
        raw = [middle] + raw
        count = len(raw)
    start = next(i for i in range(count) if raw[i][1])
    order = [raw[(start + i) % count] for i in range(count)] + [raw[start]]
    points = [order[0][0]]
    current = order[0][0]
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
            following = ((point[0] + following[0]) // 2, (point[1] + following[1]) // 2)
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


def _crossings(edges, sample):
    """Crossings of one sampling line with the outline, as (position, winding)."""
    out = []
    for x0, y0, x1, y1 in edges:
        if y0 == y1:
            continue
        if (y0 <= sample < y1) or (y1 <= sample < y0):
            t = (sample - y0) / (y1 - y0)
            out.append((x0 + t * (x1 - x0), 1 if y1 > y0 else -1))
    out.sort()
    return out


def _spans(crossings):
    """Interior intervals under the non-zero winding rule."""
    spans = []
    winding = 0
    start = None
    for position, direction in crossings:
        previous = winding
        winding += direction
        if previous == 0 and winding != 0:
            start = position
        elif previous != 0 and winding == 0 and start is not None:
            spans.append((start, position))
            start = None
    return spans


def rasterize(contours, width, height, baseline, dropout=True, smart=False, stubs=False):
    """Monochrome scan conversion with the classic TrueType dropout control.

    Coordinates are 26.6 pixels with y up from the baseline; the result is one
    bit per pixel with y down from the top of the cell.
    """
    bitmap = [0] * height
    horizontal = []
    vertical = []
    for contour in contours:
        for index in range(len(contour)):
            ax, ay = contour[index]
            bx, by = contour[(index + 1) % len(contour)]
            # To device space: x right, y down from the cell top.
            horizontal.append((ax, baseline - ay, bx, baseline - by))
            vertical.append((baseline - ay, ax, baseline - by, bx))

    def plot(column, row):
        if 0 <= column < width and 0 <= row < height:
            bitmap[row] |= 1 << column

    # The main sweep: one sampling line through the centre of every pixel row.
    # A pixel belongs to the glyph when its centre lies inside the outline or
    # on it, the rule the classic scan converter states. A centre on the right
    # or lower edge counts as well; without that the right stroke of the
    # close cross lost a column and the cross stood lopsided.
    for row in range(height):
        sample = row * 64 + 32
        for left, right in _spans(_crossings(horizontal, sample)):
            first = math.ceil((left - 32) / 64.0)
            last = math.floor((right - 32) / 64.0)
            if first > last:
                if dropout:
                    _dropout(plot, left, right, row, smart, False)
                continue
            for column in range(first, last + 1):
                plot(column, row)
    if not dropout:
        return bitmap
    # The cross sweep: the same test along columns, which is where a horizontal
    # hair line would otherwise disappear.
    for column in range(width):
        sample = column * 64 + 32
        for top, bottom in _spans(_crossings(vertical, sample)):
            first = math.ceil((top - 32) / 64.0)
            last = math.floor((bottom - 32) / 64.0)
            if first > last:
                _dropout(plot, top, bottom, column, smart, True)
    return bitmap


def _dropout(plot, low, high, other, smart, transposed):
    """One pixel for a span that no pixel centre fell into.

    Simple dropout control takes the pixel the span starts in; smart dropout
    control takes the pixel nearest the middle of the span.
    """
    position = (low + high) / 2.0 if smart else low
    cell = math.floor(position / 64.0)
    if transposed:
        plot(other, cell)
    else:
        plot(cell, other)


# ---------------------------------------------------------------------------
# cells
# ---------------------------------------------------------------------------

class HintedFont:
    """Hinted Marlett cells, one interpreter per ppem."""

    def __init__(self, data):
        self.font = Sfnt(data)
        self.interpreters = {}

    def interpreter(self, ppem):
        if ppem not in self.interpreters:
            self.interpreters[ppem] = Interpreter(self.font, ppem)
        return self.interpreters[ppem]

    def glyph(self, index, ppem):
        interpreter = self.interpreter(ppem)
        zone = interpreter.hint(index)
        count = interpreter.point_count
        advance = zone.cur[count + 1][0] - zone.cur[count][0]
        return zone, count, advance, interpreter

    def cell(self, code, size):
        """(rows of pixel bits, width, advance) for one character at one ppem."""
        if size <= 0 or size > 64:
            return None
        index = self.font.cmap.get(ord(code))
        if index is None:
            return None
        zone, count, advance, interpreter = self.glyph(index, size)
        # The em is usWinAscent + usWinDescent, so the cell height is the ppem
        # and the baseline sits on the bottom edge of the cell.
        baseline = mul_fix(self.font.win_ascent * 64, interpreter.scale)
        baseline = pix_round(baseline)
        contours = flatten(zone, count)
        # The first horizontal phantom point is the hinted pen origin.
        # Marlett moves it to -64 at 10 ppem for maximize/restore. Keep the
        # outline relative to that origin, or its left stem gets clipped.
        origin = zone.cur[count][0]
        contours = [[(x - origin, y) for x, y in contour] for contour in contours]
        dropout, smart, stubs = self.dropout_rules(interpreter)
        rows = rasterize(contours, size, size, baseline, dropout, smart, stubs)
        return rows, size, max(1, (advance + 32) >> 6)

    @staticmethod
    def dropout_rules(interpreter):
        """What SCANCTRL and SCANTYPE asked for, in the spec's own terms.

        SCANTYPE 0 is simple dropout control including stubs, 1 the same
        without them, 4 and 5 the smart variants; every other value turns
        dropout control off entirely.
        """
        enabled = bool(interpreter.scan_control)
        kind = interpreter.scan_type
        if kind not in (0, 1, 4, 5):
            return False, False, False
        return enabled, kind in (4, 5), kind in (0, 4)


def cells(path, codes, sizes):
    font = HintedFont(Path(path).read_bytes())
    out = {}
    for size in sizes:
        for code in codes:
            rendered = font.cell(code, size)
            if rendered:
                out[(size, code)] = rendered
    return font, out


# ---------------------------------------------------------------------------
# verification against the font's own hinted tables
# ---------------------------------------------------------------------------

def verify(path, sizes=range(8, 25)):
    """Compare hinted advances and vertical extremes against hdmx and VDMX."""
    font = HintedFont(Path(path).read_bytes())
    hdmx = font.font.hdmx()
    vdmx = font.font.vdmx()
    report = {'hdmx': {'checked': 0, 'mismatch': []},
              'vdmx': {'checked': 0, 'mismatch': [],
                       'note': 'this font records yMax = ppem and yMin = 0 for every '
                               'ppem, so VDMX cannot discriminate a hinted result here'},
              'ltsh': {'checked': 0, 'mismatch': []}}
    ltsh = font.font.ltsh()
    for ppem in sorted(set(sizes) | set(hdmx)):
        if ppem not in hdmx:
            continue
        widths = hdmx[ppem]
        extremes = []
        for glyph in range(font.font.glyph_count):
            try:
                zone, count, advance, interpreter = font.glyph(glyph, ppem)
            except NotImplementedError:
                continue
            rounded = (advance + 32) >> 6
            report['hdmx']['checked'] += 1
            if rounded != widths[glyph]:
                report['hdmx']['mismatch'].append((ppem, glyph, rounded, widths[glyph]))
            if count:
                ys = [zone.cur[i][1] for i in range(count)]
                extremes.append((max(ys), min(ys)))
            if ltsh and glyph < len(ltsh) and ppem >= ltsh[glyph]:
                linear = mul_fix(font.font.advance(glyph)[0] * 64, interpreter.scale)
                report['ltsh']['checked'] += 1
                if abs(((linear + 32) >> 6) - rounded) > 1:
                    report['ltsh']['mismatch'].append((ppem, glyph, rounded, (linear + 32) >> 6))
        if extremes and ppem in vdmx:
            ymax = max(value for value, _ in extremes)
            ymin = min(value for _, value in extremes)
            expected = vdmx[ppem]
            got = (-(-ymax // 64), ymin // 64)
            report['vdmx']['checked'] += 1
            if got != expected:
                report['vdmx']['mismatch'].append((ppem, got, expected))
    return report


if __name__ == '__main__':
    target = sys.argv[1] if len(sys.argv) > 1 else None
    if not target:
        raise SystemExit('usage: truetype_hint.py FONT.TTF [size ...]')
    if len(sys.argv) > 2:
        hinted = HintedFont(Path(target).read_bytes())
        for size in (int(value) for value in sys.argv[2:]):
            for code in '012rs345ahop':
                cell = hinted.cell(code, size)
                if not cell:
                    continue
                rows, width, advance = cell
                print('%r size %d advance %d' % (code, size, advance))
                for bits in rows:
                    print('   ' + ''.join('#' if (bits >> c) & 1 else '.' for c in range(width)))
    else:
        import json
        print(json.dumps(verify(target), indent=2)[:4000])
