#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Verify original region bodies and current native GDI integration."""
from pathlib import Path
import argparse
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/region-family'
sys.path.insert(0, str(ROOT))
import neo98

# Stdcall byte counts for the independent COMMON guest fixture.
IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'GetLastError': 0, 'SetLastError': 4,
                 'GetModuleHandleA': 4},
    'GDI32': {
        'CreateRectRgn': 16, 'CreateRectRgnIndirect': 4, 'SetRectRgn': 20,
        'CombineRgn': 16, 'OffsetRgn': 12, 'GetRgnBox': 8, 'PtInRegion': 12,
        'RectInRegion': 8, 'EqualRgn': 8, 'GetRegionData': 12,
        'ExtCreateRegion': 12, 'SelectClipRgn': 8, 'ExtSelectClipRgn': 12,
        'GetClipRgn': 8, 'GetRandomRgn': 12, 'OffsetClipRgn': 12, 'DeleteObject': 4,
        'CreateCompatibleDC': 4, 'DeleteDC': 4, 'CreateBitmap': 20,
        'CreateSolidBrush': 4, 'SelectObject': 8, 'SaveDC': 4, 'RestoreDC': 8,
        'PatBlt': 24, 'GetPixel': 12,
    },
    'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16,
               'DestroyWindow': 4, 'GetDC': 4, 'ReleaseDC': 8, 'FillRect': 12,
               'BeginPaint': 8, 'EndPaint': 8, 'InvalidateRect': 12},
}


def test(source=None):
    """Default to current src; --source permits an isolated integration overlay."""
    OUT.mkdir(parents=True, exist_ok=True)
    environment = neo98.tool_environment()
    environment['UBSAN_OPTIONS'] = 'halt_on_error=1'
    reports = {}

    def run(name, command):
        result = subprocess.run(command, capture_output=True, text=True,
                                env=environment)
        (OUT / f'component-{name}.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            print(result.stdout + result.stderr)
        result.check_returncode()
        print(name + ': PASS', flush=True)

    for name, macro, report, corpus in (
        ('region', 'REGION_STANDALONE', 'original-results.json', 'cases.bin'),
        ('region_clip', 'REGION_CLIP_STANDALONE', 'clip-results.json',
         'clip-cases.bin'),
    ):
        library = OUT / ('region.dylib' if name == 'region' else 'region-clip.dylib')
        bridge = ROOT / 'tools' / (name + '_bridge.cpp')
        shared = ['-dynamiclib'] if sys.platform == 'darwin' else ['-shared', '-fPIC']
        run(name + '-build', ['clang++', '-std=c++17', '-O1', '-g', *shared,
                             str(bridge), '-o', str(library)])
        run(name + '-original', [str(neo98.analysis_python()),
                                 str(ROOT / 'tools' / ('test_' + name + '.py'))])
        binary = OUT / (name + '-asan')
        run(name + '-asan-build', ['clang++', '-std=c++17', '-O1', '-g',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            '-D' + macro, str(bridge), '-o', str(binary)])
        run(name + '-asan', [str(binary), str(OUT / corpus)])
        reports[name] = json.loads((OUT / report).read_text())
        reports[name]['ASan_UBSan'] = 'PASS'
    command = [str(neo98.analysis_python()), str(ROOT / 'tools/test_native_region.py')]
    if source:
        command += ['--source', str(Path(source).resolve())]
    run('region-native', command)
    reports['native'] = json.loads((OUT / 'native-results.json').read_text())
    run('region-user', [str(neo98.analysis_python()), str(ROOT / 'tools/test_user_region.py')])
    reports['user'] = json.loads((OUT / 'user-native/results.json').read_text())
    (OUT / 'component-results.json').write_text(json.dumps(reports, indent=2) + '\n')
    return reports


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path)
    args = parser.parse_args()
    print(json.dumps(test(args.source), indent=2))
