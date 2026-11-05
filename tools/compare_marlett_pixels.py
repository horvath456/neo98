#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Compare complete Marlett pixel matrices, checking log and font identity."""
import argparse
import json
import re
from pathlib import Path
from wine_conformance import digest, save
from marlett_glyphs import CELLS


def fnv(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def read_run(folder, sizes):
    manifest = json.loads((folder / 'manifest.json').read_text())
    result = json.loads((folder / 'results.json').read_text())
    group = result['groups']['marlett_pixels']
    if group['status'] != 'PASS':
        raise ValueError(f'{folder}: incomplete or failed glyph probe')
    binary = next(g['sha256'] for g in manifest['groups'] if g['name'] == 'marlett_pixels')
    cases, fonts, identity = {}, {}, None
    for process in group['processes']:
        log = folder / process['log']
        if digest(log) != process['sha256']:
            raise ValueError(f'{log}: changed log')
        for line in log.read_text().splitlines():
            if ': MARLETT_FILE ' in line:
                match = re.fullmatch(r'.+: MARLETT_FILE bytes=(\d+) fnv=([0-9a-f]{8})', line)
                if not match or identity is not None:
                    raise ValueError('invalid or duplicate font identity')
                identity = (int(match[1]), int(match[2], 16))
            elif ': MARLETT_FONT ' in line:
                match = re.fullmatch(r'.+: MARLETT_FONT size=(\d+) face=(.+) height=(\d+) ascent=(\d+) descent=(\d+)', line)
                if not match or match[2].lower() != 'marlett' or int(match[1]) in fonts:
                    raise ValueError('invalid, substituted or duplicate font')
                fonts[int(match[1])] = list(map(int, match.group(3, 4, 5)))
            elif ': MARLETT_PIXELS ' in line:
                match = re.fullmatch(r'.+: MARLETT_PIXELS size=(\d+) code=(.) rows=((?:[0-9a-f]{8} ){32})', line)
                if not match:
                    raise ValueError('malformed glyph pixels')
                key = (int(match[1]), match[2])
                if key in cases:
                    raise ValueError('duplicate glyph')
                cases[key] = [int(x, 16) for x in match[3].split()]
    if set(fonts) != set(sizes) or set(cases) != {(n, c) for n in sizes for c in CELLS}:
        raise ValueError('incomplete glyph matrix')
    if identity is None:
        raise ValueError('font identity missing')
    return binary, identity, cases, fonts, result


def compare(reference, sizes, candidate=None, font=None):
    binary, identity, expected, metrics, _ = read_run(reference, sizes)
    report = {'scope': 'selected font bytes, complete glyph matrix and ordered pixel rows',
              'reference_results_sha256': digest(reference / 'results.json'),
              'binary_sha256': binary, 'font_bytes_fnv': list(identity)}
    if candidate:
        other, actual_identity, actual, actual_metrics, result = read_run(candidate, sizes)
        if binary != other or identity != actual_identity:
            raise ValueError('different EXE or font bytes')
        report.update(candidate_results_sha256=digest(candidate / 'results.json'),
                      candidate_host_sha256=result.get('host_sha256'),
                      metrics={str(size): {'status': 'PASS' if metrics[size] == actual_metrics[size] else 'FAIL',
                                          'reference': metrics[size], 'candidate': actual_metrics[size]}
                               for size in sizes})
    else:
        from truetype_hint import HintedFont
        data = font.read_bytes()
        if identity != (len(data), fnv(data)):
            raise ValueError('renderer font differs from the native selected font')
        renderer = HintedFont(data)
        actual = {}
        for size, code in expected:
            rows = renderer.cell(code, size)[0]
            actual[size, code] = rows + [0] * (32 - len(rows))
        report.update(renderer_sha256=digest(Path(__file__).with_name('truetype_hint.py')),
                      font_sha256=digest(font))
    checks = {f'{size}/{code}': {'status': 'PASS' if actual[size, code] == rows else 'FAIL',
                               'reference': rows, 'candidate': actual[size, code]}
              for (size, code), rows in expected.items()}
    all_checks = list(checks.values()) + list(report.get('metrics', {}).values())
    report.update(checks=checks, status='PASS' if all(c['status'] == 'PASS' for c in all_checks) else 'FAIL')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--candidate', type=Path)
    mode.add_argument('--font', type=Path)
    parser.add_argument('--sizes', default=','.join(map(str, range(1, 33))))
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    report = compare(args.reference, list(map(int, args.sizes.split(','))), args.candidate, args.font)
    save(args.report, report)
    passed = sum(c['status'] == 'PASS' for c in report['checks'].values())
    print(f'{report["status"]}: {passed}/{len(report["checks"])} glyph images')
    raise SystemExit(report['status'] != 'PASS')
