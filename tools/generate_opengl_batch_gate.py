#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate the relocatable x86 helper bytes used by the OpenGL ICD."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    out = ROOT / 'build/opengl-batch'
    out.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    compiler = shutil.which('clang', path=env['PATH'])
    if not compiler:
        raise RuntimeError('clang is required to generate the OpenGL batch helper')
    obj = out / 'gate.obj'
    subprocess.run([compiler, '--target=i686-pc-windows-msvc', '-O2', '-ffreestanding',
                    '-fno-builtin', '-fno-stack-protector', '-c',
                    str(ROOT / 'tools/opengl_batch_gate.c'), '-o', str(obj)], env=env, check=True)
    data = obj.read_bytes()
    count = struct.unpack_from('<H', data, 2)[0]
    header = 20 + struct.unpack_from('<H', data, 16)[0]
    sections = []
    for i in range(count):
        at = header + i * 40
        if data[at:at + 8].rstrip(b'\0') != b'.text':
            continue
        size, offset = struct.unpack_from('<II', data, at + 16)
        assert struct.unpack_from('<H', data, at + 32)[0] == 0, 'Helper must have no relocations'
        sections.append(data[offset:offset + size])
    assert len(sections) == 1 and sections[0]
    code = sections[0]
    text = ('// SPDX-License-Identifier: GPL-2.0-or-later\n'
            '// Generated from tools/opengl_batch_gate.c by tools/generate_opengl_batch_gate.py.\n'
            '#pragma once\nnamespace Win98OpenGLBatch\n{\nconstexpr unsigned char Record[] = {\n')
    for i in range(0, len(code), 12):
        text += '    ' + ', '.join(f'0x{x:02x}' for x in code[i:i + 12]) + ',\n'
    text += '};\n}\n'
    path = ROOT / 'src/win98_opengl_batch.h'
    if args.check:
        assert path.read_text() == text, 'Regenerate src/win98_opengl_batch.h'
    else:
        path.write_text(text)
    print(f'OpenGL batch helper: {len(code)} bytes, no relocations')


if __name__ == '__main__':
    main()
