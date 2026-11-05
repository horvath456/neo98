#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Compare individual Wine ok_sequence invocations, including empty sequences."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re

from wine_conformance import CHECK, digest, save

MARKER = re.compile(r'^NEO98_SEQUENCE_(BEGIN|END) ([^:]+:\d+) (.*)$')


def parse_sequences(text):
    cases, occurrences, active = [], Counter(), None
    for line in text.splitlines():
        marker = MARKER.fullmatch(line)
        if marker:
            kind, site, context = marker.groups()
            if kind == 'BEGIN':
                if active is not None:
                    raise ValueError('Nested sequence marker')
                occurrences[site, context] += 1
                active = dict(site=site, context=context, occurrence=occurrences[site, context], counts={})
            else:
                if active is None or (site, context) != (active['site'], active['context']):
                    raise ValueError('Unmatched sequence end')
                active['status'] = 'FAIL' if any(active['counts'].get(x) for x in ('FAIL', 'TODO_FAIL')) else 'PASS'
                cases.append(active)
                active = None
        elif line.startswith('NEO98_SEQUENCE_'):
            raise ValueError('Malformed sequence marker')
        elif active is not None:
            check = CHECK.fullmatch(line)
            if check:
                filename, number, status = check.groups()
                if filename+':'+number != active['site']:
                    raise ValueError('Foreign assertion inside sequence')
                active['counts'][status] = active['counts'].get(status, 0) + 1
    if active is not None:
        raise ValueError('Unfinished sequence')
    if not cases:
        raise ValueError('No sequence markers')
    return cases


def compare_cases(reference, candidate):
    key = lambda case: (case['site'], case['context'], case['occurrence'])
    actual = {key(case): case for case in candidate}
    if len(actual) != len(candidate) or len({key(case) for case in reference}) != len(reference):
        raise ValueError('Duplicate sequence identity')
    checks = []
    for expected in reference:
        found = actual.pop(key(expected), None)
        if found is None:
            status = 'MISSING'
        elif expected['status'] == 'PASS':
            status = found['status']
        else:
            status = 'IMPROVEMENT' if found['status'] == 'PASS' else 'REFERENCE_FAIL'
        checks.append(dict(status=status, reference=expected, candidate=found))
    return dict(status='FAIL' if any(x['status'] in ('FAIL', 'MISSING') for x in checks) else 'PASS',
                checks=checks, additional_sequences=list(actual.values()))


def read_sequences(folder):
    manifest = json.loads((folder/'manifest.json').read_text())
    result = json.loads((folder/'results.json').read_text())
    group = result['groups']['user32_msg']
    if not result.get('suite_done') or group['status'] not in ('PASS', 'FAIL'):
        raise ValueError('Incomplete message run')
    # This test is one process. Separate repeated processes need explicit
    # process identities rather than silently resetting occurrence numbers.
    if len(group['processes']) != 1:
        raise ValueError('Expected one message-test process')
    process = group['processes'][0]
    log = folder/process['log']
    if digest(log) != process['sha256']:
        raise ValueError('Changed message log')
    return parse_sequences(log.read_text()), manifest, result


def compare(reference, candidate):
    expected, rm, rr = read_sequences(reference)
    actual, cm, cr = read_sequences(candidate)
    binary = lambda m: next(g['sha256'] for g in m['groups'] if g['name'] == 'user32_msg')
    if binary(rm) != binary(cm) or not rm.get('supervisor_sha256') or any(
            rm.get(k) != cm.get(k) for k in ('supervisor_sha256', 'fixtures', 'assets')):
        raise ValueError('Different test binaries, supervisor or fixtures')
    if rr.get('runtime') != '86box-win98':
        raise ValueError('Original Win98 reference required')
    report = compare_cases(expected, actual)
    report.update(schema=1, binary_sha256=binary(rm), candidate_host_sha256=cr.get('host_sha256'),
                  reference_results_sha256=digest(reference/'results.json'),
                  candidate_results_sha256=digest(candidate/'results.json'),
                  scope='Individual message sequence invocations; optional messages may change assertion counts. Other assertions remain in the raw source-site comparison.')
    return report


def qualify_contract(baseline, reference, review_path=None):
    """Use invocation outcomes for sequences, exact counts for other assertions."""
    cases, manifest, result = read_sequences(reference)
    if result.get('runtime') != '86box-win98' or digest(reference/'results.json') != baseline['reference_results_sha256']:
        raise ValueError('Mismatched original message reference')
    if set(baseline['groups']) != {'user32_msg'}:
        raise ValueError('Message contract requires only user32_msg')
    group = baseline['groups']['user32_msg']
    sites = sorted({case['site'] for case in cases})
    for site in sites:
        group['required'].pop(site, None)
        group['advisory'].pop(site, None)
    baseline.update(comparison='message_sequences', assertion_contract=True,
                    sequence_sites=sites, sequence_invocations=len(cases))
    if review_path is not None:
        review = json.loads(review_path.read_text())
        if (review['group'] != 'user32_msg' or review['binary_sha256'] != group['sha256'] or
                review['reference_results_sha256'] != baseline['reference_results_sha256'] or
                not review.get('reason') or digest(Path(review['probe'])) != review['probe_sha256'] or
                group['required'].get(review['site']) != review['reference']):
            raise ValueError('Native defect review does not match this reference and probe')
        del group['required'][review['site']]
        baseline['reviewed_native_defects'] = {'review_sha256': digest(review_path), 'review': review}
    return baseline


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('reference', 'candidate', 'report'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    report = compare(args.reference, args.candidate)
    save(args.report, report)
    print(report['status'], dict(Counter(x['status'] for x in report['checks'])))
    raise SystemExit(0 if report['status'] == 'PASS' else 1)
