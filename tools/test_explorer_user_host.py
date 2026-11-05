#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""My Computer using a private clone of the installed user's machine.

Run through a shared serial test lock. The fixture expects the reported 1024x768
32-bit desktop and saved maximized Explorer. C:, its registry journal and
app settings are cloned; the games collection is mounted read-only.
With --check-restore, also compare the restored menu/toolbar pixels.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import arm64_run
from native_app import ROOT
from PIL import Image


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


from external_paths import external_path
from test_support import require_file
def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=Path, default=ROOT/'build/native-app/neo98-host')
    p.add_argument('--source', type=Path, default=ROOT/'build/win98-c')
    p.add_argument('--registry', type=Path, default=ROOT/'build/win98-c.registry')
    p.add_argument('--settings', type=Path, default=Path.home()/'Library/Application Support/Neo98/settings.ini')
    p.add_argument('--drive-d', type=Path, default=external_path('NEO98_COLLECTION'))
    p.add_argument('--out', type=Path, default=ROOT/'build/explorer-user-host')
    p.add_argument('--check-restore', action='store_true')
    args = p.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    assert shutil.disk_usage(ROOT).free >= 8*1024**3, 'Need at least 8 GiB free'
    source, registry, settings = args.source.resolve(), args.registry.resolve(), args.settings.resolve()
    for path in (source, registry, settings, args.drive_d):
        require_file(path)
    inputs = [registry, settings, source/'WINDOWS/ShellIconCache']
    before = {str(f): digest(f) for f in inputs if f.exists()}
    drive = out/'c'
    assert not drive.exists(), 'Scratch C: already exists; choose a new --out'
    subprocess.run(['cp','-c','-R',str(source),str(drive)],check=True)
    shutil.copyfile(registry,out/'c.registry')
    (out/'settings').mkdir(exist_ok=True)
    text = settings.read_text()
    text = '\n'.join('machine = '+str(drive) if s.startswith('machine =') else s for s in text.splitlines())+'\n'
    (out/'settings/settings.ini').write_text(text)
    steps = ['0:wait=Shell_TrayWnd','0:idle=1000','0:dblclick=32,32',
             '0:wait=CabinetWClass','0:idle=1000','0:move=740,540','0:tops=2',
             '0:shot={out}/closed.ppm','0:click=1009,98','0:idle=500',
             '0:move=740,540','0:lists=1','0:shot={out}/open.ppm',
             '0:click=1009,118','0:idle=200','0:move=740,540','0:lists=1',
             '0:shot={out}/scroll-up.ppm','0:click=1009,325','0:idle=200',
             '0:move=740,540','0:lists=1','0:shot={out}/scroll-down.ppm','0:key=1b',
             '0:keydown=12','0:key=20','0:keyup=12','0:idle=100','0:key=52',
             '0:idle=1000','0:tops=2','0:shot={out}/restored.ppm','0:menu=quit']
    env = dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NEO98_HOST_DEADLINE='180',
               NEO98_SETTINGS_DIR=str(out/'settings'),NEO98_LOG_DIR=str(out/'logs'),
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}',str(out)))
    for key in ('NEO98_HOST_ATTACHED','NEO98_HOST_MENU'):
        env.pop(key,None)
    command = [str(args.host.resolve()),'--machine',str(drive),'--registry',str(out/'c.registry'),
               '--drive-readonly','D='+str(args.drive_d.resolve())]
    arm64_run.ensure_idle()
    start = time.monotonic()
    try:
        with (out/'log.txt').open('w') as log:
            child = subprocess.Popen(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL)
            try:
                code = child.wait(210)
            except subprocess.TimeoutExpired:
                arm64_run.end(child)
                raise RuntimeError('Explorer host exceeded deadline')
    finally:
        shutil.rmtree(drive)
    log = (out/'log.txt').read_text(errors='replace')
    checks = {'host completed':code==0 and 'FAULT BOX' not in log and 'timed out' not in log,
              'input files unchanged':before=={str(f):digest(f) for f in inputs if f.exists()}}
    frames = {}
    for name in ('closed','open','scroll-up','scroll-down','restored'):
        file = out/(name+'.ppm')
        if file.exists():
            frames[name] = Image.open(file).convert('RGB')
            frames[name].save(file.with_suffix('.png'))
    checks['all frames captured'] = len(frames)==5
    if len(frames)==5:
        def colors(name,box):
            return len(frames[name].crop(box).getcolors(65536) or [])
        checks['reported desktop size'] = frames['closed'].size==(1024,768)
        checks['selected My Computer image'] = colors('closed',(56,90,72,106))>=3
        checks['selected image while dropdown open'] = colors('open',(56,90,72,106))>=3
        counts = []
        for row in range(13):
            x = 77 if 1<=row<=8 else 67
            counts.append(colors('open',(x,110+row*16,x+16,126+row*16)))
        checks['every visible dropdown image'] = all(n>=3 for n in counts)
        checks['Desktop reached by up arrow'] = colors('scroll-up',(55,110,71,126))>=3
        checks['up arrow changes first row'] = frames['open'].crop((55,110,220,126)).tobytes()!=frames['scroll-up'].crop((55,110,220,126)).tobytes()
        checks['down arrow returns to original rows'] = frames['open'].crop((55,110,220,318)).tobytes()==frames['scroll-down'].crop((55,110,220,318)).tobytes()
        if args.check_restore:
            checks['restored menu text'] = frames['closed'].crop((19,25,225,39)).tobytes()==frames['restored'].crop((23,207,229,221)).tobytes()
            checks['restored toolbar contents'] = frames['closed'].crop((15,47,610,83)).tobytes()==frames['restored'].crop((19,229,614,265)).tobytes()
    result = {'checks':checks,'command':command,'steps':steps,'seconds':time.monotonic()-start,
              'host_sha256':digest(args.host.resolve()),'input_hashes':before}
    (out/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    for name,okay in checks.items():
        print(('PASS ' if okay else 'FAIL ')+name)
    return int(not all(checks.values()))

if __name__=='__main__':
    raise SystemExit(main())
