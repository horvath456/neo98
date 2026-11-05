#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""arrows, track and thumb of the fault dialog's read-only EDIT."""
import argparse
import json
from pathlib import Path
import shutil
import sys
sys.path[:0]=[str(Path(__file__).resolve().parent),str(Path(__file__).resolve().parents[1])]
from PIL import Image,ImageChops
import win98_c
from native_app import ROOT
from test_device_manager_host import host
from test_fault_box_host import build_fixture

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--host',type=Path,default=ROOT/'build/native-app/neo98-host')
    parser.add_argument('--out',type=Path,default=ROOT/'build/fault-scroll-host')
    args=parser.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    exe=build_fixture(out/'fixture');shutil.copyfile(exe,exe.with_name('FAULTI.EXE'))
    steps=['0:wait=#32770|Faulti','0:keydown=12','0:key=44','0:keyup=12','0:idle=500']
    def shot(name):steps.extend(['0:move=700,550','0:idle=300',f'0:shot={{out}}/{name}.ppm'])
    shot('top')
    steps+=['0:click=579,486'];shot('arrow')
    steps+=['0:click=579,372'];shot('up')
    steps+=['0:press=579,388','0:idle=200','0:move=579,468','0:idle=200','0:release=579,468'];shot('thumb')
    steps+=['0:press=579,468','0:idle=200','0:move=579,388','0:idle=200','0:release=579,388'];shot('thumbup')
    steps+=['0:click=579,455'];shot('page')
    steps+=['0:key=0d','0:idle=300','0:menu=quit']
    code,log,run=host(args.host.resolve(),out,'scroll',r'D:\FAULTI.EXE','FAULTI.EXE',steps,30,
        win98_c.use_pristine(),drives=['D='+str(exe.parent)],environment={'NEO98_HOST_SCREEN':'800x600'})
    frames={}
    for name in ('top','arrow','up','thumb','thumbup','page'):
        file=run/(name+'.ppm')
        if file.exists():
            frame=Image.open(file).convert('RGB');frame.save(file.with_suffix('.png'))
            frames[name]=frame.crop((213,364,570,494))
    checks={'host ended':code==0 and 'FAULT BOX closed' in log,
            'details and every screenshot':len(frames)==6 and 'timed out' not in log}
    if len(frames)==6:
        changed=lambda a,b:ImageChops.difference(frames[a],frames[b]).getbbox() is not None
        checks.update({'down arrow scrolls':changed('top','arrow'),
                       'up arrow returns to start':not changed('top','up'),
                       'thumb drag scrolls':changed('top','thumb'),
                       'thumb drag returns to start':not changed('top','thumbup'),
                       'track click scrolls':changed('top','page')})
    (out/'results.json').write_text(json.dumps({'checks':checks,'log':str(run/'log.txt')},indent=2)+'\n')
    print(json.dumps(checks));return 0 if all(checks.values()) else 1

if __name__=='__main__':raise SystemExit(main())
