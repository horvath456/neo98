#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""complete thread-start SEH and original Indeo open on FEX and Unicorn. Use serial.py."""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from ui_clipboard_test_support import fixture

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--host',type=Path,default=ROOT/'build/native-app/neo98-host')
    parser.add_argument('--baseline',action='store_true')
    args=parser.parse_args()
    out=ROOT/'build/indeo-thread';out.mkdir(exist_ok=True)
    exe=fixture(out/'fixture',ROOT/'tools/indeo_thread_fixture.c',{'KERNEL32':{
        'ExitProcess':4,'CreateThread':24,'WaitForSingleObject':8,'GetExitCodeThread':8,
        'CloseHandle':4,'IsBadReadPtr':8}, 'MSVFW32':{'ICOpen':12,'ICClose':4}})
    digest=hashlib.sha256(exe.read_bytes()).hexdigest();results=[]
    for cpu in ('fex','unicorn'):
        if shutil.disk_usage('/System/Volumes/Data').free < 8 * 1024**3:
            raise SystemExit('STOP: fewer than 8 GiB free before host run')
        with tempfile.TemporaryDirectory(prefix='private-c-',dir=out) as temporary:
            drive=Path(temporary)/'c'
            subprocess.run(['cp','-c','-R',str((ROOT/'build/win98-c').resolve()),str(drive)],check=True)
            assert not drive.is_symlink()
            path=out/(cpu+('-before' if args.baseline else '-after')+'.log')
            env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NEO98_CPU=cpu,
                NEO98_HOST_DEADLINE='8',NEO98_SETTINGS_DIR=str(out/'settings'),NEO98_LOG_DIR=str(out/'logs'))
            command=[str(args.host.resolve()),'-',str(ROOT/'build/generated/notepad-fixedsys.fnt'),
                '--exe',str(exe),'--quiet','--system-tree',str(drive),'--ram-drive','--no-registry-file']
            with path.open('w') as log:
                p=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
                try:code=p.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    p.terminate();p.wait(timeout=20);raise
            text=path.read_text(errors='replace');expected=62021 if args.baseline else 0
            assert code==0 and f'NEO98 HOST exited code={expected}' in text,(cpu,code,text[-2000:])
            assert hashlib.sha256(exe.read_bytes()).hexdigest()==digest
            results.append({'cpu':cpu,'exit_code':expected,'status':'EXPECTED_FAILURE' if args.baseline else 'PASS'})
            print(cpu,results[-1]['status'],flush=True)
    (out/('before.json' if args.baseline else 'results.json')).write_text(json.dumps({
        'host_sha256':hashlib.sha256(args.host.read_bytes()).hexdigest(),'exe_sha256':digest,'tests':results},indent=2)+'\n')

if __name__=='__main__':main()
