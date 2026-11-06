#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""VER.2 GetFileResourceSize, VER.3 GetFileResource and VER.20
GetFileVersionInfoRaw for a 16-bit caller.

tools/ver_resource_win16_fixture.asm reads the version resource (type 16,
id 1) of C:\\WINDOWS\\SYSTEM\\VER.DLL (NE) and VERSION.DLL (PE) through the
three routines, asks for a missing file and a missing type, and takes the
three names through GetProcAddress the way SETUPX does. The
test finds the same resources in the two files itself and compares sizes,
file offsets, handles and bytes.

    build/analysis-venv/bin/python tools/test_ver_resource_win16_host.py [--no-build]
"""
from __future__ import annotations

import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win16_fixture  # noqa: E402
import win98_c  # noqa: E402

UNTOUCHED = 0xA5A5A5A5
BUFFER = 1024


def ne_resource(image, kind, name):
    """File offset and length of an integer resource of an NE file."""
    header = struct.unpack_from('<I', image, 0x3C)[0]
    table = header + struct.unpack_from('<H', image, header + 0x24)[0]
    shift = struct.unpack_from('<H', image, table)[0]
    at = table + 2
    while True:
        kind_at, count = struct.unpack_from('<HH', image, at)
        if not kind_at:
            return None
        if kind_at == 0x8000 | kind:
            for i in range(count):
                offset, length, _flags, ident = struct.unpack_from('<HHHH', image, at + 8 + i * 12)
                if ident == 0x8000 | name:
                    return offset << shift, length << shift
            return None
        at += 8 + count * 12


def pe_resource(path, kind, name):
    """The bytes of an integer resource of a PE file, first language."""
    import pefile
    pe = pefile.PE(str(path), fast_load=True)
    pe.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_RESOURCE']])
    for entry in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if entry.id != kind:
            continue
        for item in entry.directory.entries:
            if item.id == name:
                data = item.directory.entries[0].data.struct
                return pe.get_data(data.OffsetToData, data.Size)
    return None


class Reader:
    def __init__(self, data):
        self.data, self.at = data, 0

    def take(self, fmt):
        value = struct.unpack_from(fmt, self.data, self.at)
        self.at += struct.calcsize(fmt)
        return value if len(value) > 1 else value[0]

    def bytes(self, count):
        value = self.data[self.at:self.at + count]
        self.at += count
        return value


def copied(buffer, expected):
    """The expected bytes at the start, CCh behind them, nothing beyond."""
    return buffer[:len(expected)] == expected and set(buffer[len(expected):]) <= {0xCC}


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('ver-resource-win16')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(pristine.resolve()), str(drive)], check=True)
    win16_fixture.assemble(ROOT / 'tools/ver_resource_win16_fixture.asm', out / 'VERRES.EXE', 'VERRES',
                           neo98.tool_environment(), modules=('KERNEL', 'USER', 'VER'))
    shutil.copyfile(out / 'VERRES.EXE', drive / 'VERRES.EXE')

    ne_path = win98_c.path('C:\\WINDOWS\\SYSTEM\\VER.DLL', drive)
    pe_path = win98_c.path('C:\\WINDOWS\\SYSTEM\\VERSION.DLL', drive)
    ne_image = ne_path.read_bytes()
    ne_offset, ne_length = ne_resource(ne_image, 16, 1)
    ne_bytes = ne_image[ne_offset:ne_offset + ne_length]
    pe_bytes = pe_resource(pe_path, 16, 1)
    assert ne_length <= BUFFER and pe_bytes and len(pe_bytes) <= BUFFER, (ne_length, len(pe_bytes or b''))

    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='60',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\VERRES.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=180)
    written = win98_c.path('C:\\VERRES.BIN', drive)
    data = written.read_bytes() if written else b''
    shutil.rmtree(drive, ignore_errors=True)
    log_text = (out / 'log.txt').read_text(errors='replace')

    expected_size = 6 * 8 + 4 * (2 + BUFFER) + 2 + 3 * 4
    checks = {}
    if len(data) == expected_size:
        r = Reader(data)
        size, handle = r.take('<II')
        checks['NE: GetFileResourceSize answers the resource length'] = size == ne_length
        checks['NE: the handle is the file offset of the resource'] = handle == ne_offset
        ok, buffer = r.take('<H'), r.bytes(BUFFER)
        checks['NE: GetFileResource at that offset copies the 64 bytes asked'] = (
            ok == 1 and copied(buffer, ne_image[ne_offset:ne_offset + 64]))
        ok, buffer = r.take('<H'), r.bytes(BUFFER)
        checks['NE: GetFileResource with offset 0 finds it again, no more than its length'] = (
            ok == 1 and copied(buffer, ne_bytes))
        size, handle = r.take('<II')
        checks['PE: GetFileResourceSize answers the resource length'] = size == len(pe_bytes)
        checks['PE: the handle comes back as zero'] = handle == 0
        ok, buffer = r.take('<H'), r.bytes(BUFFER)
        checks['PE: GetFileResource copies the resource'] = ok == 1 and copied(buffer, pe_bytes)
        size, handle = r.take('<II')
        checks['missing file: zero, handle untouched'] = size == 0 and handle == UNTOUCHED
        size, handle = r.take('<II')
        checks['NE without the type: zero, handle untouched'] = size == 0 and handle == UNTOUCHED
        size, handle = r.take('<II')
        checks['PE without the type: zero, handle written as zero'] = size == 0 and handle == 0
        ok, buffer = r.take('<H'), r.bytes(BUFFER)
        checks['GetFileVersionInfoRaw: the stored Unicode block, unconverted'] = (
            ok == 1 and copied(buffer, pe_bytes))
        module = r.take('<H')
        pointers = [r.take('<I') for _ in range(3)]
        checks['GetProcAddress(VER, ...) finds all three names'] = bool(module) and all(pointers)
        size, handle = r.take('<II')
        checks['GetFileResourceSize through its GetProcAddress pointer'] = (
            size == ne_length and handle == ne_offset)
    else:
        checks[f'C:\\VERRES.BIN has {expected_size} bytes'] = False
    checks['no MISSING WIN16 EXPORT VER in the log'] = 'MISSING WIN16 EXPORT VER' not in log_text
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print('written:', len(data), 'bytes\n    log:', out / 'log.txt')
        return 1
    print('ver resource win16: passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
