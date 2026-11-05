#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Compare calendar outputs; last-error is checked only on API failure."""
import argparse
import json
from pathlib import Path
import re
from wine_conformance import digest, save

LOCALES=(0x409,0xc07,0x411,0x404,0x412,0x41e,0xffff)
DATES=('1868/9/7','1868/9/8','1912/7/29','1912/7/30','1926/12/24',
       '1926/12/25','1989/1/7','1989/1/8','2019/4/4','2019/5/1')


def read_run(folder):
    result=json.loads((folder/'results.json').read_text())
    manifest=json.loads((folder/'manifest.json').read_text())
    group=result['groups']['calendar_contract']
    if group['status']!='PASS':raise ValueError('incomplete or failed calendar probe')
    binary=next(g['sha256'] for g in manifest['groups'] if g['name']=='calendar_contract')
    cases={}
    for process in group['processes']:
        log=folder/process['log']
        if digest(log)!=process['sha256']:raise ValueError('changed calendar log')
        for line in log.read_text().splitlines():
            if ': CALENDAR_' not in line:continue
            match=re.fullmatch(r'.+: (CALENDAR_(?:NUMBER|STRING|DATE) .+?) n=(\d+) error=([0-9a-f]{8}) (.+)',line)
            if not match or match[1] in cases:raise ValueError('malformed or duplicate calendar trace')
            count=int(match[2]);value={'return':count,'output':match[4]}
            if not count:value['error']=int(match[3],16)
            cases[match[1]]=value
    expected={f'CALENDAR_{mode} locale={locale:04x} calendar={calendar}'
              for mode in ('NUMBER','STRING') for locale in LOCALES for calendar in range(14)}
    expected|={f'CALENDAR_DATE locale={locale:04x} date={date} alt={alt}'
               for locale in LOCALES for date in DATES for alt in (0,1)}
    if set(cases)!=expected:raise ValueError('incomplete calendar matrix')
    return binary,cases,result


def compare(reference,candidate):
    binary,expected,_=read_run(reference);other,actual,result=read_run(candidate)
    if binary!=other:raise ValueError('different calendar EXE')
    checks={key:{'status':'PASS' if value==actual[key] else 'FAIL',
                 'reference':value,'candidate':actual[key]} for key,value in expected.items()}
    return {'status':'PASS' if all(v['status']=='PASS' for v in checks.values()) else 'FAIL',
            'scope':'All return values and outputs; last-error only on failure. Successful calls may change last-error.',
            'binary_sha256':binary,'candidate_host_sha256':result.get('host_sha256'),
            'reference_results_sha256':digest(reference/'results.json'),
            'candidate_results_sha256':digest(candidate/'results.json'),'checks':checks}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for arg in ('reference','candidate','report'):parser.add_argument('--'+arg,type=Path,required=True)
    args=parser.parse_args();report=compare(args.reference,args.candidate);save(args.report,report)
    print(report['status'],sum(c['status']=='PASS' for c in report['checks'].values()),'/',len(report['checks']))
    raise SystemExit(report['status']!='PASS')
