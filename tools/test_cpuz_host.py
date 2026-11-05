#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""CPU-Z 1.57 on a private Windows clone: VIA probe, tabs and normal exit."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tools')]
import first_logon

def main():
    p=argparse.ArgumentParser()
    p.add_argument('folder',type=Path)
    p.add_argument('--host',type=Path,default=ROOT/'build/native-app/neo98-host')
    args=p.parse_args()
    assert (args.folder/'cpuz.exe').is_file()
    out=ROOT/'build/cpuz-host';out.mkdir(exist_ok=True)
    drive,reg=first_logon.clone(out/'session')
    program=out/'program';program.mkdir(exist_ok=True)
    for file in args.folder.iterdir():
        if file.is_file():shutil.copyfile(file,program/file.name)
    steps=['3:wait=#32770|CPU-Z ',f'8:shot={out}/cpu.ppm','9:click=325,142',
           f'10:shot={out}/mainboard.ppm','11:click=520,142',f'12:shot={out}/about.ppm',
           '14:click=559,478']
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NEO98_HOST_INPUT=';'.join(steps),
        NEO98_HOST_DEADLINE='35',NEO98_SETTINGS_DIR=str(out/'settings'),NEO98_LOG_DIR=str(out/'logs'))
    for key in ('NEO98_HOST_MENU','NEO98_TRACE_CALLS_FROM','NEO98_FEX_DEBUG','NEO98_HOST_ATTACHED'):
        env.pop(key,None)
    with (out/'host.log').open('w') as log:
        result=subprocess.run([str(args.host.resolve()),'-',str(ROOT/'build/generated/notepad-fixedsys.fnt'),
            '--system-tree',str(ROOT/'build/win98-c.setup'),'--drive-c',str(drive),'--registry',str(reg),
            '--drive',f'D={program}','--exe',r'D:\cpuz.exe','--quiet','--crash-report-dir',str(out/'reports')],
            env=env,stdout=log,stderr=subprocess.STDOUT,timeout=60)
    text=(out/'host.log').read_text()
    # CPU-Z 1.57 returns 1 after its normal OK button (dialog result IDOK).
    assert result.returncode==0 and 'NEO98 HOST exited code=1 ' in text,text[-3000:]
    assert 'EXCEPTION c000001d trap 6 at 004efba9' in text, 'VIA probe did not run'
    assert 'CRASHREPORT' not in text and 'timed out' not in text,text[-3000:]
    frames=[]
    for name in ('cpu','mainboard','about'):
        frame=Image.open(out/(name+'.ppm')).convert('RGB');frame.save(out/(name+'.png'));frames.append(frame.tobytes())
    assert len(set(frames))==3,'tabs did not change'
    (out/'results.json').write_text(json.dumps({'status':'PASS','host':str(args.host.resolve()),
        'checks':['VIA ALTINST handled by guest SEH','CPU, Mainboard and About tabs','CPU-Z normal exit, host alive']},indent=2)+'\n')
    print('CPU-Z HOST PASS: guest SEH, three tabs, normal exit')
if __name__=='__main__':main()
