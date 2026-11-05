#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""SYSDM's root-device profile action against the observed original Win98 behavior.

The original Cirrus adapter stays in the tree and the action box resets when
reopened. VPOWERD Flags records a pending system change; the parent sheet says
Close. It does not write CSCONFIGFLAG_DO_NOT_CREATE for this action.
"""
import json
import sys
from pathlib import Path
sys.path[:0] = [str(Path(__file__).resolve().parent), str(Path(__file__).resolve().parents[1])]
import win98_c
from native_app import ROOT, build_host
from test_device_manager_host import host, failures, Script, select, trees, dumps, COMMAND
from test_wheel_host import picture


def main():
    out = ROOT / 'build/hardware-profile-remove-host'
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    pristine = win98_c.use_pristine()
    script = Script().expand_all()
    select(script, 6)
    script.add('click=133,431',1).add('wait=|NEO98 Display Adapter Properties',1)
    script.add('move=500,550',.5).add('shot={out}/before.ppm',.5)
    script.add('button=Remove from this hardware profile',1).add('shot={out}/checked.ppm',.5)
    script.add('button=OK',2).add('tops=2',.5).add('trees=1',.5)
    select(script,6)
    script.add('click=133,431',1).add('wait=|NEO98 Display Adapter Properties',1)
    script.add('move=500,550',.5).add('shot={out}/reopened.ppm',.5).add('tops=2',.5)
    script.add('button=Cancel',1).add('button=Close',2).add('tops=2',.5).add('button=No',1)
    code,text,run = host(binary,out,'original','C:\\WINDOWS\\RUNDLL32.EXE',COMMAND,script.steps,
                         int(script.time)+25,pristine,win98_c.journal(pristine),
                         environment={'NEO98_REG_TRACE':'1'})
    checks = {}
    def check(name,yes):
        checks[name]=bool(yes);print(('ok  ' if yes else 'FAIL'),name,flush=True)
    shots={n:picture(run,n,(133,470,146,483)) for n in ['before','checked','reopened']
           if (run/(n+'.ppm')).exists()}
    check('the checkbox is checked and resets on reopening, as in original Win98',len(shots)==3
          and shots['before']!=shots['checked'] and shots['before']==shots['reopened'])
    found=trees(text)
    check('the root adapter stays in the tree, as in original Win98',found and any(
          row[3]=='NEO98 Display Adapter' for row in found[-1]['items']))
    states=[d for d in dumps(text) if isinstance(d,dict) and d['windows']]
    first=states[0]['windows'][0] if states else {}
    check('the parent sheet says Close and disables Cancel', first.get('title')=='System Properties'
          and 'Close' in first.get('texts',[])
          and 'style=58010000 process=1 props=0 class=Button title=Cancel' in text)
    check('the VPOWERD policy comes from the hive',
          'System\\CurrentControlSet\\Services\\VxD\\VPOWERD = 0' in text
          and '| Flags = 0' in text)
    check('no invented profile removal flag', 'Registry write name=CSConfigFlags' not in text)
    check('no fatal call or crash',code==0 and not failures(text) and 'CRASHREPORT' not in text)
    (out/'results.json').write_text(json.dumps(checks,indent=2)+'\n')
    print(f'hardware profile remove: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1

if __name__=='__main__':
    raise SystemExit(main())
