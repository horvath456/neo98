#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Metal batches, stencil, surface access and texture versions.

Run under a shared serial test lock. --baseline accepts the pre-fix host_metal.mm
and counts its passes without changing its rendering. Screenshots are PPM.
"""
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: Metal exists on macOS only')  # macOS only; skipped elsewhere
        _sys.exit(77)
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/metal-passes')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'tools/host_metal.mm'
    if args.baseline:
        text = args.baseline.read_text()
        text = text.replace('class MetalBackend final', 'unsigned long long baseline_passes = 0;\nclass MetalBackend final')
        text = text.replace('        encoder = [command_buffer renderCommandEncoderWithDescriptor:pass];',
                            '        ++baseline_passes;\n        encoder = [command_buffer renderCommandEncoderWithDescriptor:pass];')
        text = text.replace('namespace HostMetal\n{',
                            'namespace HostMetal\n{\nunsigned long long RenderPasses() { return baseline_passes; }', 1)
        source = args.output / 'baseline.mm'
        source.write_text(text)
    binary = args.output / 'test-metal-passes'
    env = dict(neo98.tool_environment(), MTL_DEBUG_LAYER='1', NEO98_METAL_STATS='1')
    subprocess.run(['clang++', '-std=c++17', '-O1', '-fsanitize=address,undefined', '-fobjc-arc',
                    '-I', str(ROOT / 'tools'), str(ROOT / 'tools/test_metal_passes.cpp'),
                    '-x', 'objective-c++', str(source), '-x', 'c++', str(ROOT / 'src/d3d_shader_msl.cpp'),
                    '-framework', 'Metal', '-framework', 'Foundation', '-o', str(binary)], env=env, check=True)
    subprocess.run([str(binary), 'baseline' if args.baseline else 'merged', str(args.output / 'stencil.ppm')], env=env, check=True)


if __name__ == '__main__':
    main()
