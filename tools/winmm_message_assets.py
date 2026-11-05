#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The MMSYSTEM string table, the source of every WINMM error text.

mciGetErrorStringA, waveOutGetErrorTextA  and
midiOutGetErrorTextA  probe their buffer and then thunk into the
16-bit MMSYSTEM.DLL, which answers out of its own string resources. The
identifier of a string is the error number itself, so the table below is the
whole contract: an error with a string has a text, an error without one has
none.

The blocks in the file also name the four error spaces MMSYSTEM keeps apart:
0 to 20 MMSYSERR, 32 to 35 WAVERR, 64 to 71 MIDIERR and 256 upwards MCIERR.

Writes build/winmm-message/winmm_messages.h and winmm-messages.json. Nothing
of the original is checked in; the header is a build product like the USER
image resources in tools/image_resource_assets.py.
"""
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/winmm-message'
DLL = ROOT / 'build/win98-c/WINDOWS/SYSTEM/MMSYSTEM.DLL'
SHA = 'be30ae3d04785a31c71acb4113ebf7d3875d20cb8409adb4c74f166720f89557'
RT_STRING = 6


def strings():
    raw = DLL.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != SHA:
        raise RuntimeError('MMSYSTEM.DLL changed: %s' % digest)
    u16 = lambda at: struct.unpack_from('<H', raw, at)[0]
    ne = struct.unpack_from('<I', raw, 60)[0]
    if raw[ne:ne + 2] != b'NE':
        raise RuntimeError('MMSYSTEM.DLL is not an NE module')
    table = ne + u16(ne + 36)
    shift = u16(table)
    at = table + 2
    out = {}
    while u16(at):
        kind, count = u16(at), u16(at + 2)
        at += 8
        for _ in range(count):
            offset, length, name = u16(at) << shift, u16(at + 2) << shift, u16(at + 6)
            at += 12
            if kind != (RT_STRING | 0x8000):
                continue
            block = (name & 0x7fff) - 1
            cursor, end = offset, offset + length
            for index in range(16):
                if cursor >= end:
                    break
                size = raw[cursor]
                text = raw[cursor + 1:cursor + 1 + size].decode('latin1')
                cursor += 1 + size
                if text:
                    out[block * 16 + index] = text
    return out


def generate():
    table = strings()
    OUT.mkdir(parents=True, exist_ok=True)
    lines = ['// Generated from the original MMSYSTEM.DLL string resources;',
             '// source SHA256 ' + SHA + '. Written by tools/winmm_message_assets.py.',
             '#pragma once',
             'struct OriginalMultimediaMessage {unsigned id; const char *text;};',
             'inline constexpr OriginalMultimediaMessage OriginalMultimediaMessages[]={']
    for key in sorted(table):
        lines.append('{%u,%s},' % (key, json.dumps(table[key])))
    lines.append('};')
    header = OUT / 'winmm_messages.h'
    text = '\n'.join(lines) + '\n'
    if not header.exists() or header.read_text() != text:
        header.write_text(text)
    (OUT / 'winmm-messages.json').write_text(json.dumps(
        {'sha256': SHA, 'strings': len(table), 'ids': sorted(table)}, indent=2) + '\n')
    return header


if __name__ == '__main__':
    print(json.dumps({'header': str(generate())}))
