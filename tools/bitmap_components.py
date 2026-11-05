#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Original GDI DDB storage, bitmap API and pattern-brush comparisons."""
import json
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98


def test():
    out = ROOT / 'build/bitmap-core'
    out.mkdir(parents=True, exist_ok=True)
    reports = {}
    for suffix, define, corpus, report in (
        ('', 'BITMAP_STANDALONE', 'cases.bin', 'original-results.json'),
        ('_entry', 'BITMAP_ENTRY_STANDALONE', 'entry-cases.bin', 'entry-results.json'),
        ('_query', 'BITMAP_QUERY_STANDALONE', 'query-cases.bin', 'query-results.json'),
    ):
        stem = 'bitmap' + suffix
        source = ROOT / 'tools' / (stem + '_bridge.cpp')
        library = out / (stem + '.dylib')
        compiler = ['clang++', '-std=c++17', '-O1', '-Wall', '-Wextra', str(source)]
        subprocess.run(compiler + ['-dynamiclib', '-o', str(library)], check=True)
        subprocess.run([str(neo98.analysis_python()), str(ROOT/'tools'/('test_'+stem+'.py')),
                        str(library), str(out/report)], check=True)
        binary = out / (stem + '-asan')
        subprocess.run(compiler + ['-g', '-fsanitize=address,undefined', '-D'+define,
                                  '-o', str(binary)], check=True)
        subprocess.run([str(binary), str(out/corpus)], check=True)
        reports[stem] = json.loads((out/report).read_text())
        reports[stem]['sanitizers'] = 'ASan/UBSan PASS'
    for stem, directory in (('bitmap_thunk', 'bitmap-thunk'), ('pattern_brush', 'pattern-brush')):
        subprocess.run([str(neo98.analysis_python()), str(ROOT/'tools'/('test_'+stem+'.py'))], check=True)
        reports[stem] = json.loads((ROOT/'build'/directory/'results.json').read_text())
    return reports


if __name__ == '__main__':
    print(json.dumps(test(), indent=2))
