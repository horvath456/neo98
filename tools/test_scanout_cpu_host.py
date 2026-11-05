#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""measure only the host child's CPU time with getrusage.

Run with serial.py. --host selects a before/after binary. The guest writes
600 primary frames with Sleep pacing; concurrent compilers cannot add to
this child's CPU time. test_user_scanout separately verifies direct writes.
"""
import argparse, hashlib, json, os, resource, shutil, subprocess, sys, time
from pathlib import Path
sys.path[:0]=[str(Path(__file__).resolve().parent),str(Path(__file__).resolve().parents[1])]
import neo98
from native_app import ROOT
from ui_clipboard_test_support import fixture

def main():
 p=argparse.ArgumentParser();p.add_argument('--host',type=Path,default=ROOT/'build/native-app/neo98-host');p.add_argument('--name',default='after');a=p.parse_args()
 out=ROOT/'build/scanout-cpu-host';out.mkdir(parents=True,exist_ok=True)
 names={'KERNEL32':{'ExitProcess':4,'Sleep':4,'GetTickCount':0},'USER32':{'RegisterClassA':4,'CreateWindowExA':48,'DefWindowProcA':16,'ShowCursor':4},'DDRAW':{'DirectDrawCreate':12}}
 exe=fixture(out/'fixture',ROOT/'tools/scanout_cpu_fixture.c',names)
 private=out/'c'
 if private.exists():shutil.rmtree(private)
 subprocess.run(['cp','-cR',str((ROOT/'build/win98-c').resolve()),str(private)],check=True)
 shutil.copyfile(exe,private/'SCANCPU.EXE')
 try:
  if subprocess.run(['pgrep','-x','neo98-host'],capture_output=True).returncode==0:__import__('test_support').skip('exclusive host access is unavailable: another neo98-host is active')
  samples=[]
  for n in range(3):
   log=out/f'{a.name}-{n}.log'
   env=dict(neo98.tool_environment(),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NEO98_HOST_DEADLINE='45',NEO98_CPU='fex',NEO98_SETTINGS_DIR=str(out/'settings'),NEO98_LOG_DIR=str(out/'logs'))
   with log.open('w') as stream:
    before=resource.getrusage(resource.RUSAGE_CHILDREN);started=time.monotonic()
    proc=subprocess.Popen([a.host,'-',ROOT/'build/generated/notepad-fixedsys.fnt','--exe','C:\\SCANCPU.EXE','--system-tree',private,'--drive-c',private,'--no-registry-file','--quiet'],stdout=stream,stderr=subprocess.STDOUT,env=env)
    try:code=proc.wait(timeout=60)
    except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=20);raise
    elapsed=time.monotonic()-started;after=resource.getrusage(resource.RUSAGE_CHILDREN)
   text=log.read_text(errors='replace');assert code==0 and 'NEO98 HOST exited code=0' in text,(code,text[-2000:])
   cpu=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime
   samples.append(dict(cpu_seconds=cpu,wall_seconds=elapsed,cores=cpu/elapsed))
  result=dict(host_sha256=hashlib.sha256(a.host.read_bytes()).hexdigest(),frames=600,samples=samples)
  (out/f'{a.name}.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
 finally:shutil.rmtree(private)
if __name__=='__main__':main()
