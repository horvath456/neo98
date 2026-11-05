#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Compile the Linux rasterizer's GLSL into an include with SPIR-V words."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def generate(output=None, compiler=None):
    output = Path(output or ROOT / 'build/generated/vulkan_shaders.h')
    compiler = compiler or shutil.which('glslangValidator')
    if not compiler:
        local = ROOT / 'build/toolchain/usr/bin/glslangValidator'
        compiler = str(local) if local.is_file() else None
    if not compiler:
        raise RuntimeError('glslangValidator is missing. Install glslang-tools to build Vulkan support.')
    sections = ['// Generated from tools/shaders/linux_basic.* by build_vulkan_shaders.py.\n#pragma once\n']
    with tempfile.TemporaryDirectory() as directory:
        for stage in ('vert', 'clip_vert', 'frag', 'glide_vert', 'glide_frag', 'w_frag', 'flat_vert', 'flat_frag', 'flat_w_frag', 'present_vert', 'present_frag'):
            binary = Path(directory) / (stage + '.spv')
            name = ('linux_present.' + stage[8:] if stage.startswith('present_') else
                    'linux_glide.' + stage[6:] if stage.startswith('glide_')
                    else 'linux_clip.vert' if stage == 'clip_vert'
                    else 'linux_basic.' + stage.rsplit('_', 1)[-1])
            defines = ['-DNEO98_WBUFFER=1'] if stage in ('w_frag', 'flat_w_frag') else []
            if stage in ('frag', 'w_frag', 'flat_frag', 'flat_w_frag'):
                defines += ['-DNEO98_SPECIALIZE_FIXED=1']
            if stage.startswith('flat_'):
                defines += ['-DNEO98_FLAT=1']
            subprocess.run([compiler, '-V', '--target-env', 'vulkan1.0', *defines,
                            str(ROOT / 'tools/shaders' / name), '-o', str(binary)], check=True,
                           capture_output=True)
            data = binary.read_bytes()
            words = struct.unpack('<' + 'I' * (len(data) // 4), data)
            sections.append('static const unsigned Vulkan_' + stage + '[] = {\n' +
                            '\n'.join(','.join(f'0x{w:08x}' for w in words[i:i + 8]) + ','
                                      for i in range(0, len(words), 8)) + '\n};\n')
    for symbol, name in (('frag', 'linux_basic.frag'), ('program_vert', 'linux_program.vert.glsl'),
                         ('program_helpers', 'linux_program_helpers.glsl'),
                         ('program_frag_main', 'linux_program_frag_main.glsl'),
                         ('program_vert_main', 'linux_program_vert_main.glsl')):
        source = (ROOT / 'tools/shaders' / name).read_text()
        sections.append(f'static const char Vulkan_{symbol}_source[] = R"NEO98({source})NEO98";\n')
    output.parent.mkdir(parents=True, exist_ok=True)
    text = '\n'.join(sections)
    if not output.exists() or output.read_text() != text:
        output.write_text(text)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--compiler')
    args = parser.parse_args()
    generate(args.output, args.compiler)
