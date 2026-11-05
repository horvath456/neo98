#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Inventory current external EXEs and their recorded Win98/Neo98 attempts.

This measures coverage, not conformance: a completed failing test counts as
an execution. Qualification reports remain the source for compatibility.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def result_files(root):
    excluded = {'source', 'vm', 'c', 'bin', 'include', 'build-logs',
                'translation-units', 'build-tools', 'system-provenance'}
    for folder, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in excluded and
                   not d.startswith('session-') and not d.endswith('-reports')]
        if 'invalid-harness.json' in files:
            dirs[:] = []
            continue
        if 'results.json' in files and 'manifest.json' in files:
            yield Path(folder)/'results.json'


def inventory(catalog_root, result_root, seed):
    groups, sources = {}, {}
    for source in ('wine', 'reactos', 'sdl12'):
        path = catalog_root/source/'catalog.json'
        catalog = json.loads(path.read_text())
        sources[source] = {'pin': catalog['source_pin'], 'catalog_sha256': digest(path)}
        for group in catalog['groups']:
            groups[group['name']] = {k: group[k] for k in
                ('name', 'build_status', 'sha256', 'assessment', 'source', 'source_sha256') if k in group}
            groups[group['name']].update(source_family=source, reference=[], neo98=[])

    def add(name, sha, kind, status, evidence, counts=None, suite_done=None):
        group = groups.get(name)
        if not group or group['build_status'] != 'BUILT' or group.get('sha256') != sha:
            return
        entry = {'status': status, 'evidence': evidence}
        if counts is not None: entry['counts'] = counts
        if suite_done is not None: entry['suite_done'] = suite_done
        group[kind].append(entry)

    if seed:
        historical = json.loads(seed.read_text())
        for name, runs in historical['runs'].items():
            for run in runs:
                for kind, field in [('reference', 'reference'), ('neo98', 'candidate')]:
                    add(name, run.get('sha256'), kind, run[field], str(seed))
    for path in result_files(result_root):
        result = json.loads(path.read_text())
        kind = {'86box-win98': 'reference', 'neo98': 'neo98'}.get(result.get('runtime'))
        if not kind: continue
        manifest = json.loads((path.parent/'manifest.json').read_text())
        for group in manifest['groups']:
            run = result.get('groups', {}).get(group['name'])
            if run:
                add(group['name'], group['sha256'], kind, run['status'], str(path),
                    run.get('counts'), result.get('suite_done'))
    unbuilt, unattempted, missing_arm = [], [], []
    for name, group in groups.items():
        if group['build_status'] != 'BUILT':
            unbuilt.append(name)
            continue
        if not any(r['status'] != 'NOT_RUN' for r in group['reference']):
            unattempted.append(name)
        completed = any(r['status'] in ('PASS', 'FAIL') for r in group['reference'])
        attempted_arm = any(r['status'] not in ('NOT_RUN', 'SKIP') for r in group['neo98'])
        if completed and not attempted_arm: missing_arm.append(name)
    return {'schema': 1, 'scope': 'Execution coverage for current catalog hashes, not compatibility approval',
            'sources': sources, 'seed_sha256': digest(seed) if seed else None,
            'totals': {'catalog': len(groups), 'built': len(groups)-len(unbuilt),
                       'build_errors': len(unbuilt), 'without_reference_attempt': len(unattempted),
                       'completed_reference_without_neo98_attempt': len(missing_arm)},
            'unbuilt': sorted(unbuilt), 'without_reference_attempt': sorted(unattempted),
            'completed_reference_without_neo98_attempt': sorted(missing_arm), 'groups': groups}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog-root', type=Path, default=Path('build/conformance-campaign'))
    parser.add_argument('--result-root', type=Path, default=Path('build'))
    parser.add_argument('--seed', type=Path, default=Path('docs/benchmarks/conformance-campaign-2026-10-21.json'))
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    report = inventory(args.catalog_root, args.result_root, args.seed)
    args.out.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report['totals'], indent=2))
