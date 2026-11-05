#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Compare the DIB family with original drivers and check native storage lifetime."""
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/dib-api'
sys.path.insert(0, str(ROOT))
import neo98

IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'GetLastError': 0, 'SetLastError': 4, 'VirtualQuery': 12},
    'GDI32': {
        'CreateDIBitmap': 24, 'CreateDIBSection': 24, 'GetDIBits': 28, 'SetDIBits': 28,
        'SetDIBitsToDevice': 48, 'StretchDIBits': 52, 'GetDIBColorTable': 16,
        'SetDIBColorTable': 16, 'CreateCompatibleDC': 4, 'SelectObject': 8,
        'DeleteObject': 4, 'DeleteDC': 4, 'GetPixel': 12, 'BitBlt': 36,
        'SaveDC': 4, 'RestoreDC': 8, 'GetObjectA': 12,
        'CreateBitmap': 20, 'SetTextColor': 8, 'SetBkColor': 8,
        'GetStockObject': 4, 'StretchBlt': 44, 'SetStretchBltMode': 8,
    },
}


def test(source=None):
    """Use current sources, or an isolated runtime overlay; generate no shared assets."""
    source = Path(source).resolve() if source else ROOT / 'src'
    OUT.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    env['NEO98_BRIDGE_SOURCE'] = str(source)  # the bridges the tests build (native_build.load_bridge)
    env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    python = str(neo98.analysis_python())
    reports = {}

    def run(name, command):
        result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env,
                                capture_output=True, text=True)
        (OUT / (name + '.log')).write_text(result.stdout + result.stderr)
        if result.returncode:
            print(result.stdout + result.stderr)
        result.check_returncode()
        return result.stdout.strip()

    base = ['clang++', '-std=c++17', '-O1', '-g', '-I' + str(source), '-I' + str(ROOT / 'src')]
    shared = ['-dynamiclib'] if sys.platform == 'darwin' else ['-shared', '-fPIC']
    sanitize = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    raster = [source / 'gdi.cpp', ROOT / 'src/dibeng.cpp', ROOT / 'src/dib_text.cpp',
              ROOT / 'src/win98_rect.cpp']
    cases = [
        ('layout', 'dib_api_bridge.cpp', 'DIB_API_STANDALONE'),
        ('public', 'dib_api_public_bridge.cpp', 'DIB_API_PUBLIC_STANDALONE'),
        ('color', 'dib_api_color_bridge.cpp', 'DIB_API_COLOR_STANDALONE'),
        ('transfer', 'dib_api_transfer_bridge.cpp', 'DIB_API_TRANSFER_STANDALONE'),
    ]
    for name, bridge, macro in cases:
        sources = [ROOT / 'tools' / bridge] + (raster if name == 'transfer' else [])
        run(name + '-compile', base + shared + sources + ['-o', OUT / (name + '.dylib')])
        run(name + '-original', [python, ROOT / 'tools' / ('test_dib_api_' + name + '.py')])
        reports[name] = json.loads((OUT / (name + '-results.json')).read_text())
        run(name + '-asan-compile', base + sanitize + ['-D' + macro] + sources +
            ['-o', OUT / (name + '-asan')])
        reports[name]['sanitizers'] = run(name + '-asan',
            [OUT / (name + '-asan'), OUT / (name + '-cases.bin')])
        print('DIB ' + name + ': PASS', flush=True)

    run('native-compile', base + sanitize + [ROOT / 'tools/test_dib_api_native.cpp'] +
        raster[1:] + ['-o', OUT / 'native-asan'])
    reports['native'] = {'result': run('native', [OUT / 'native-asan']),
                         'sanitizers': ['address', 'undefined'], 'source': str(source)}
    (OUT / 'native-results.json').write_text(json.dumps(reports['native'], indent=2) + '\n')
    (OUT / 'component-results.json').write_text(json.dumps(reports, indent=2) + '\n')
    print('DIB native: PASS', flush=True)
    return reports


if __name__ == '__main__':
    print(json.dumps(test(sys.argv[1] if len(sys.argv) > 1 else None), indent=2))
