#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Compare OEM bitmap presence, format and pixel hashes from identical EXEs."""
import argparse
import json
import re
from pathlib import Path
from wine_conformance import digest, save


def read_run(folder):
    result = json.loads((folder / 'results.json').read_text())
    manifest = json.loads((folder / 'manifest.json').read_text())
    binary = next(g['sha256'] for g in manifest['groups'] if g['name'] == 'oem_bitmaps')
    group = result['groups']['oem_bitmaps']
    if group['status'] != 'PASS':
        raise ValueError(f'{folder}: bitmap probe did not complete successfully')
    cases = {}
    for process in group['processes']:
        log = folder / process['log']
        if digest(log) != process['sha256']:
            raise ValueError(f'{log}: captured log hash changed')
        for line in log.read_text().splitlines():
            if ': OEM id=' not in line:
                continue
            match = re.fullmatch(r'.+:\d+: OEM id=([0-9a-f]{4}) variant=(\d+) (.+)', line)
            if not match:
                raise ValueError(f'{log}: malformed bitmap trace')
            key = f'{match[1]}/{match[2]}'
            if key in cases:
                raise ValueError(f'{log}: duplicate bitmap case {key}')
            value = match[3]
            # Win98 LoadBitmap sometimes changes LastError despite returning
            # NULL. This comparison is explicitly about image results only.
            if value.startswith('absent error='):
                value = 'absent'
            elif not re.fullmatch(r'size=\d+,\d+ stride=\d+ planes=\d+ bpp=\d+ hash=[0-9a-f]{8}', value):
                raise ValueError(f'{log}: malformed bitmap result {key}')
            cases[key] = value
    expected = {f'{i:04x}/{v}' for i in range(0x7fde, 0x8000) for v in range(3)}
    if set(cases) != expected:
        raise ValueError(f'{folder}: missing or unexpected bitmap cases: {sorted(set(cases) ^ expected)}')
    return binary, result, cases


def compare(reference, candidate):
    ref_binary, ref_result, expected = read_run(reference)
    arm_binary, arm_result, actual = read_run(candidate)
    if ref_binary != arm_binary:
        raise ValueError('reference and candidate executed different bitmap EXEs')
    checks = {key: {'status': 'PASS' if actual[key] == value else 'FAIL',
                    'reference': value, 'candidate': actual[key]}
              for key, value in expected.items()}
    return {'status': 'PASS' if all(c['status'] == 'PASS' for c in checks.values()) else 'FAIL',
            'scope': 'presence, dimensions, format and ordered pixel hash; excludes LastError',
            'binary_sha256': ref_binary,
            'reference_results_sha256': digest(reference / 'results.json'),
            'candidate_results_sha256': digest(candidate / 'results.json'),
            'candidate_host_sha256': arm_result.get('host_sha256'), 'checks': checks}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    report = compare(args.reference, args.candidate)
    save(args.report, report)
    passed = sum(c['status'] == 'PASS' for c in report['checks'].values())
    print(f'{report["status"]}: {passed}/{len(report["checks"])} OEM bitmap variants')
    raise SystemExit(report['status'] != 'PASS')
