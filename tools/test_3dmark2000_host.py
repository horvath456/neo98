#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Original 3DMark2000: start benchmark, Escape, then close the project.

Copies the installed program, system DLLs and registry to a private session.
The source installation is read only. This is a startup test, not a full score run.
"""
import argparse
import hashlib
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
    p.add_argument('--source-drive',type=Path,default=ROOT/'build/win98-c')
    p.add_argument('--source-registry',type=Path,default=ROOT/'build/win98-c.registry')
    p.add_argument('--host',type=Path,default=ROOT/'build/native-app/neo98-host')
    p.add_argument('--close-only',action='store_true')
    args=p.parse_args()
    relative=Path('Program Files/MadOnion.com/3DMark2000')
    assert (args.source_drive/relative/'3DMark2000.exe').is_file()
    out=ROOT/('build/3dmark2000-close' if args.close_only else 'build/3dmark2000-host');out.mkdir(exist_ok=True)
    drive,reg=first_logon.clone(out/'session')
    target=drive/relative;target.parent.mkdir(parents=True,exist_ok=True)
    subprocess.run(['cp','-cR',str(args.source_drive/relative),str(target)],check=True)
    subprocess.run(['cp','-cR',str(args.source_drive/'WINDOWS/SYSTEM')+'/.',str(drive/'WINDOWS/SYSTEM')],check=True)
    shutil.copyfile(args.source_registry,reg)
    steps=['3:wait=#32770|3DMark2000','5:click=480,370',f'8:shot={out}/project.ppm',
           '9:keydown=12','9:key=73','9:keyup=12']
    if not args.close_only:
        steps=steps[:3]+['9:click=440,410',f'30:shot={out}/benchmark.ppm','38:key=1b','40:key=1b',
                        '42:keydown=12','42:key=73','42:keyup=12']
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NEO98_HOST_INPUT=';'.join(steps),
        NEO98_HOST_DEADLINE='65',NEO98_SETTINGS_DIR=str(out/'settings'),NEO98_LOG_DIR=str(out/'logs'))
    with (out/'host.log').open('w') as log:
        process=subprocess.Popen([str(args.host.resolve()),'-',str(ROOT/'build/generated/notepad-fixedsys.fnt'),
            '--system-tree',str(args.source_drive.resolve()),'--drive-c',str(drive),'--registry',str(reg),
            '--exe',r'C:\Program Files\MadOnion.com\3DMark2000\3DMark2000.exe','--quiet',
            '--crash-report-dir',str(out/'reports')],env=env,stdout=log,stderr=subprocess.STDOUT)
        try: code=process.wait(timeout=85)
        except subprocess.TimeoutExpired:
            process.terminate();process.wait(timeout=20);raise
    text=(out/'host.log').read_text()
    assert code==0 and 'NEO98 HOST exited code=0 ' in text,(code,text[-3000:])
    assert 'CRASHREPORT' not in text and 'timed out' not in text,text[-3000:]
    Image.open(out/'project.ppm').save(out/'project.png')
    if not args.close_only:
        assert 'DP2 render target unusable' not in text,'render target resolved outside its DirectDraw object'
        frame=Image.open(out/'benchmark.ppm').convert('RGB')
        frame.save(out/'benchmark.png')
        pixels=list(frame.crop((50,70,950,450)).getdata())
        gray=sum(abs(r-g)<4 and abs(g-b)<4 for r,g,b in pixels)
        assert len(set(pixels))>500 and gray<len(pixels)//5,'benchmark scene did not render'
    (out/'results.json').write_text(json.dumps({'status':'PASS','revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'host_sha256':hashlib.sha256(args.host.read_bytes()).hexdigest(),'checks':['create project','Alt-F4 destroys MFC views','normal exit'],
        'benchmark_started':not args.close_only,'full_benchmark_tested':False,'render_target_warnings':text.count('DP2 render target unusable')},indent=2)+'\n')
    print('3DMARK2000 PASS: project, optional benchmark/Escape, MFC view cleanup, normal exit')
if __name__=='__main__':main()
