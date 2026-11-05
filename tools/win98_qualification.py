#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Rerun saved Win98 contracts against one frozen NEO98 host."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

from wine_conformance import ROOT, digest, save


def fixture_arguments(groups):
    arguments = []
    if set(groups) & {'user32_listbox', 'kernel32_drive', 'kernel32_path'}:
        arguments.append('--fixture-cdrom')
    if 'riched20_editor' in groups:
        arguments.extend(['--reference-dll', 'RICHED20.DLL'])
    return arguments


def contract_batches(directory, names=None):
    batches = {}
    selected = set(names or [])
    found = set()
    for path in sorted(directory.glob('*.json')):
        if selected and path.stem not in selected:
            continue
        value = json.loads(path.read_text())
        if 'reference_results_sha256' not in value:
            continue
        found.add(path.stem)
        batch = batches.setdefault(value['reference_results_sha256'], {'contracts': [], 'groups': set()})
        batch['contracts'].append(path)
        batch['groups'].update(value['groups'])
    if selected - found:
        raise ValueError('Unknown contracts: ' + ', '.join(sorted(selected - found)))
    if not batches:
        raise ValueError('No qualified contracts selected')
    return batches


def reference_index(root):
    result = {}
    paths = subprocess.check_output(['rg', '--files', '--no-ignore', str(root), '-g', 'results.json'], text=True)
    for filename in paths.splitlines():
        path = Path(filename)
        if not path.parent.name.startswith('reference'):
            continue
        bundle = path.parent.parent
        if not (bundle / 'manifest.json').is_file() or not (bundle / 'bin').is_dir():
            continue
        result.setdefault(digest(path), path.parent)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baselines', type=Path, default=ROOT / 'tools/wine_conformance/baselines')
    parser.add_argument('--reference-root', type=Path, default=ROOT / 'build')
    parser.add_argument('--names', help='Comma-separated baseline filenames without .json')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--host-provenance', type=Path, required=True)
    parser.add_argument('--win98-root', type=Path, required=True)
    args = parser.parse_args()
    batches = contract_batches(args.baselines, args.names.split(',') if args.names else None)
    references = reference_index(args.reference_root)
    provenance = json.loads(args.host_provenance.read_text())
    host_hash = digest(args.host)
    if provenance['host_sha256'] != host_hash:
        raise ValueError('Host does not match provenance')
    args.out.mkdir(parents=True, exist_ok=False)
    report = {'status': 'RUNNING', 'host_sha256': host_hash,
              'host_provenance_sha256': digest(args.host_provenance), 'contracts': {}}
    report_path = args.out / 'results.json'
    save(report_path, report)
    for reference_hash, batch in batches.items():
        reference = references.get(reference_hash)
        candidate = args.out / reference_hash[:16]
        if not reference:
            for baseline in batch['contracts']:
                report['contracts'][baseline.stem] = {'status': 'MISSING_REFERENCE',
                    'reference_results_sha256': reference_hash, 'baseline_sha256': digest(baseline)}
            save(report_path, report)
            print('MISSING_REFERENCE', ','.join(p.stem for p in batch['contracts']), flush=True)
            continue
        groups = sorted(batch['groups'])
        command = [sys.executable, '-u', str(ROOT / 'tools/wine_conformance.py'),
                   '--out', str(reference.parent), 'neo98', '--win98-root', str(args.win98_root),
                   '--results', str(candidate), '--reference-results', str(reference / 'results.json'),
                   '--reference-dlls', '--discard-sessions', '--names', ','.join(groups),
                   '--host', str(args.host), '--host-provenance', str(args.host_provenance)]
        command.extend(fixture_arguments(groups))
        print('RUN', ','.join(p.stem for p in batch['contracts']), flush=True)
        with (args.out / (reference_hash[:16] + '.log')).open('w') as log:
            run = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, cwd=ROOT)
            for baseline in batch['contracts']:
                output = args.out / (baseline.stem + '-qualification.json')
                entry = {'baseline_sha256': digest(baseline), 'reference_results_sha256': reference_hash,
                         'candidate': str(candidate), 'run_exit_code': run.returncode}
                if (candidate / 'results.json').is_file():
                    check = subprocess.run([sys.executable, str(ROOT / 'tools/wine_conformance.py'), 'check',
                        '--baseline', str(baseline), '--candidate', str(candidate), '--report', str(output),
                        '--reference', str(reference)],
                        stdout=log, stderr=subprocess.STDOUT, cwd=ROOT)
                    entry['check_exit_code'] = check.returncode
                    if output.is_file():
                        entry.update(status=json.loads(output.read_text())['status'],
                                     qualification=str(output), qualification_sha256=digest(output))
                        if run.returncode:
                            entry['status'] = 'RUN_ERROR'

                    else:
                        entry['status'] = 'CHECK_ERROR'
                else:
                    entry['status'] = 'RUN_ERROR'
                report['contracts'][baseline.stem] = entry
                save(report_path, report)
                print(entry['status'], baseline.stem, flush=True)
    report['status'] = 'PASS' if all(v['status'] == 'PASS' for v in report['contracts'].values()) else 'FAIL'
    save(report_path, report)
    return report['status'] != 'PASS'


if __name__ == '__main__':
    raise SystemExit(main())
