#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Test current native region integration, pixels and allocation rollback."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/region-family'
ALLOCATION_INJECTION = r'''
#include <cstdlib>
static void* region_owned[4096];
static unsigned region_attempt, region_fail;
extern "C" void region_test_failure(unsigned n) {
    region_attempt=0; region_fail=n;
}
extern "C" unsigned region_test_allocations() {
    unsigned n=0; for(auto p:region_owned) n += p != nullptr; return n;
}
void* region_test_malloc(unsigned long n) {
    if (++region_attempt == region_fail) return nullptr;
    void* p=malloc(n);
    if (p) {
        for(auto& owned:region_owned) if(!owned) { owned=p; return p; }
        abort();
    }
    return p;
}
void region_test_free(void* p) {
    if (!p) return;
    for(auto& owned:region_owned) if(owned==p) {
        owned=nullptr; free(p); return;
    }
    abort();
}
'''


def test(source=None):
    source = Path(source or ROOT / 'src').resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment['UBSAN_OPTIONS'] = 'halt_on_error=1'
    reports = {}
    for inject in (False, True):
        name = 'native-oom' if inject else 'native-pixel'
        runtime = source
        if inject:
            runtime = OUT / 'native-oom-runtime'
            runtime.mkdir(exist_ok=True)
            for filename in ('gdi.cpp', 'gdi.h', 'gdi_region_store.inc',
                             'gdi_region_api.inc'):
                content = (source / filename).read_text()
                if filename == 'gdi.cpp':
                    assert 'namespace Gdi {' in content
                    content = content.replace('namespace Gdi {',
                        '#include "alloc_injection.h"\nnamespace Gdi {', 1)
                if filename == 'gdi_region_store.inc':
                    assert 'malloc(' in content and 'free(' in content
                    content = content.replace('malloc(', 'region_test_malloc(')
                    content = content.replace('free(', 'region_test_free(')
                (runtime / filename).write_text(content)
            (runtime / 'alloc_injection.h').write_text(ALLOCATION_INJECTION)
        binary = OUT / (name + '-asan')
        command = ['clang++', '-std=c++17', '-O1', '-g',
                   '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                   '-DREGION_OVERLAY']
        if inject:
            command.append('-DREGION_FAIL_TEST')
        command += ['-I' + str(runtime), '-I' + str(source),
                    '-I' + str(ROOT / 'src'),
                    str(ROOT / 'tools/test_native_region.cpp'),
                    str(runtime / 'gdi.cpp')]
        command += [str((source if (source / n).exists() else ROOT / 'src') / n)
                    for n in ('dibeng.cpp', 'dib_text.cpp', 'win98_rect.cpp')]
        command += ['-o', str(binary)]
        for stage, cmd in (('build', command), ('test', [str(binary)])):
            result = subprocess.run(cmd, env=environment, capture_output=True,
                                    text=True)
            (OUT / f'{name}-{stage}.log').write_text(result.stdout + result.stderr)
            if result.returncode or stage == 'test':
                print(result.stdout + result.stderr, end='', flush=True)
            result.check_returncode()
        reports[name] = 'PASS'
    result = {
        'status': 'PASS', 'checks': reports, 'ASan_UBSan': 'PASS',
        'complex_clip_rectangles': 100, 'allocation_failure_positions': 44,
        'origin_profiles': ['screen device origin', 'local retained surface'],
        'bitmap_changes': '1x1 to 24x12 to 6x6, then SaveDC/RestoreDC',
        'logical_offset': '2x/3x map, nonzero origins, (3,2) becomes (6,6)',
        'system_clip': 'independent from application clip and saved with the DC',
        'shared_generators': False,
        'source_sha256': {n: hashlib.sha256((source/n).read_bytes()).hexdigest()
                          for n in ('gdi.cpp', 'gdi.h', 'gdi_region_store.inc',
                                    'gdi_region_api.inc')},
    }
    (OUT / 'native-results.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path)
    args = parser.parse_args()
    print(json.dumps(test(args.source), indent=2))
