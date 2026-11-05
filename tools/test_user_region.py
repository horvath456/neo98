#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Verify USER clip providers, nested DC leases and new allocation failures."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
from test_native_region import ALLOCATION_INJECTION

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/region-family/user-native'
SOURCES = ['gdi', 'dibeng', 'dib_text', 'win98_rect', 'user', 'user_draw',
           'text_edit', 'win98_dialog', 'win98_parameters']


def test(user_source=None):
    OUT.mkdir(parents=True, exist_ok=True)
    user_source = Path(user_source or ROOT / 'src/user.cpp').resolve()
    environment = os.environ.copy()
    environment['UBSAN_OPTIONS'] = 'halt_on_error=1'
    reports = {}
    for inject in (False, True):
        name = 'oom' if inject else 'pixels'
        runtime = OUT / name
        runtime.mkdir(exist_ok=True)
        for source in SOURCES:
            shutil.copy2(ROOT / 'src' / (source + '.cpp'),
                         runtime / (source + '.cpp'))
        shutil.copy2(user_source, runtime / 'user.cpp')
        if inject:
            (runtime / 'alloc_injection.h').write_text(ALLOCATION_INJECTION)
            gdi = runtime / 'gdi.cpp'
            content = gdi.read_text()
            assert 'namespace Gdi {' in content
            gdi.write_text(content.replace('namespace Gdi {',
                '#include "alloc_injection.h"\nnamespace Gdi {', 1))
            content = (ROOT / 'src/gdi_region_store.inc').read_text()
            assert 'malloc(' in content and 'free(' in content
            content = content.replace('malloc(', 'region_test_malloc(')
            content = content.replace('free(', 'region_test_free(')
            (runtime / 'gdi_region_store.inc').write_text(content)
        binary = OUT / (name + '-asan')
        command = ['clang++', '-std=c++17', '-O1', '-g',
                   '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                   '-I' + str(ROOT / 'src')]
        if inject:
            command.append('-DUSER_CLIP_OOM')
        command += [str(ROOT / 'tools/test_user_region.cpp')]
        command += [str(runtime / (n + '.cpp')) for n in SOURCES]
        command += ['-o', str(binary)]
        try:
            for stage, cmd in (('build', command), ('run', [str(binary)])):
                result = subprocess.run(cmd, capture_output=True, text=True,
                                        env=environment)
                (OUT / f'{name}-{stage}.log').write_text(result.stdout + result.stderr)
                if result.returncode or stage == 'run':
                    print(result.stdout + result.stderr, end='', flush=True)
                result.check_returncode()
        finally:
            shutil.rmtree(str(binary) + '.dSYM', ignore_errors=True)
        reports[name] = 'PASS'
    result = {
        'status': 'PASS', 'ASan_UBSan': 'PASS', 'checks': reports,
        'owner_draw_pixels': 300, 'control_dimensions': [20, 15],
        'screen_pixels': 480000, 'nested_screen_dc_restoration': 'PASS',
        'getdc_beginpaint_application_clip_absent': 'PASS',
        'getdc_snapshot_allocation_failures': 2,
        'source_sha256': hashlib.sha256(user_source.read_bytes()).hexdigest(),
        'shared_generators': False,
    }
    (OUT / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--user-source', type=Path)
    args = parser.parse_args()
    print(json.dumps(test(args.user_source), indent=2))
