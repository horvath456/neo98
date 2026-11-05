#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Compare complete ordered probe traces from the same native test EXE."""
import argparse
import json
from pathlib import Path
from wine_conformance import digest, save


def read_run(folder,group,prefix,count):
    result=json.loads((folder/'results.json').read_text())
    manifest=json.loads((folder/'manifest.json').read_text())
    entry=result['groups'][group]
    if entry['status']!='PASS':raise ValueError('incomplete or failed probe')
    binary=next(g['sha256'] for g in manifest['groups'] if g['name']==group)
    traces=[]
    for process in entry['processes']:
        log=folder/process['log']
        if digest(log)!=process['sha256']:raise ValueError('changed probe log')
        for line in log.read_text().splitlines():
            _,separator,message=line.partition(': ')
            if separator and message.startswith(prefix):traces.append(message)
    if len(traces)!=count or count<=0:raise ValueError('incomplete probe traces')
    return binary,traces,result


def compare(reference,candidate,group,prefix,count,reviewed=None):
    binary,expected,_=read_run(reference,group,prefix,count)
    other,actual,result=read_run(candidate,group,prefix,count)
    if other!=binary:raise ValueError('different probe EXE')
    expected_manifest=json.loads((reference/'manifest.json').read_text())
    actual_manifest=json.loads((candidate/'manifest.json').read_text())
    if not expected_manifest.get('supervisor_sha256') or any(
            expected_manifest.get(key)!=actual_manifest.get(key)
            for key in ('supervisor_sha256','fixtures','assets')):
        raise ValueError('different or missing probe supervisor/fixtures')
    allowances={}
    if reviewed is not None:
        if (reviewed['group'],reviewed['prefix'],reviewed['binary_sha256'])!=(group,prefix,binary):
            raise ValueError('review belongs to another probe')
        for entry in reviewed['differences']:
            key=(entry['reference'],entry['candidate'])
            if key in allowances or key[0]==key[1] or not entry['reason'].strip():
                raise ValueError('invalid reviewed difference')
            allowances[key]=entry['reason']
    checks=[];used=set()
    for a,b in zip(expected,actual):
        check={'status':'PASS' if a==b else 'FAIL','reference':a,'candidate':b}
        if (a,b) in allowances:
            check.update(status='REVIEWED_DIFFERENCE',reason=allowances[a,b]);used.add((a,b))
        checks.append(check)
    if used!=set(allowances):raise ValueError('stale reviewed difference')
    status='FAIL' if any(c['status']=='FAIL' for c in checks) else 'PASS_WITH_REVIEWED_DIFFERENCES' if used else 'PASS'
    return {'status':status,
            'group':group,'prefix':prefix,'expected_count':count,'binary_sha256':binary,
            'candidate_host_sha256':result.get('host_sha256'),
            'supervisor_sha256':expected_manifest['supervisor_sha256'],
            'reference_results_sha256':digest(reference/'results.json'),
            'candidate_results_sha256':digest(candidate/'results.json'),
            'checks':checks}


def check_trace_contracts(baseline, reference, candidate):
    reports = []
    for spec in baseline.get('trace_checks', []):
        try:
            if spec['group'] not in baseline['groups']:
                raise ValueError('Trace group is outside the qualified contract')
            report = compare(reference, candidate, spec['group'], spec['prefix'], spec['count'])
        except (ValueError, KeyError, OSError) as error:
            report = {'status': 'CHECK_ERROR', 'error': str(error)}
        reports.append(report)
    return reports


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for arg in ('reference','candidate','report'):p.add_argument('--'+arg,type=Path,required=True)
    for arg in ('group','prefix'):p.add_argument('--'+arg,required=True)
    p.add_argument('--count',type=int,required=True)
    p.add_argument('--reviewed-differences',type=Path);a=p.parse_args()
    reviewed=json.loads(a.reviewed_differences.read_text()) if a.reviewed_differences else None
    report=compare(a.reference,a.candidate,a.group,a.prefix,a.count,reviewed)
    if a.reviewed_differences:report['review_sha256']=digest(a.reviewed_differences)
    save(a.report,report)
    print(report['status'],sum(c['status']=='PASS' for c in report['checks']),'/',a.count)
    raise SystemExit(report['status']=='FAIL')
