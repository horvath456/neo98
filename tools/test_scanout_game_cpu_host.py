#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""getrusage for the original Animal Crossing workload. Use serial.py."""
import argparse
import hashlib,json,os,re,resource,shutil,subprocess,sys,time
from test_support import require_file
from pathlib import Path
sys.path[:0]=[str(Path(__file__).resolve().parent),str(Path(__file__).resolve().parents[1])]
import neo98
from native_app import ROOT

def main():
 p=argparse.ArgumentParser();p.add_argument('--host',type=Path,default=ROOT/'build/native-app/neo98-host');p.add_argument('--name',default='current');a=p.parse_args()
 source=Path(os.environ.get('NEO98_AC_HOME', ROOT / 'build/external/set-NEO98_AC_HOME')) / 'build/win32'
 require_file(source/'ACWIN.EXE');require_file(source/'ROM/MAIN.DOL')
 out=ROOT/'build/scanout-game-cpu-host'/a.name;out.mkdir(parents=True,exist_ok=True)
 private=out/'c'
 if private.exists():shutil.rmtree(private)
 subprocess.run(['cp','-cR',str((ROOT/'build/win98-c').resolve()),str(private)],check=True)
 subprocess.run(['cp','-cR',str(source),str(private/'AC')],check=True)
 (private/'AC/settings.ini').write_text('[Audio]\nmaster_volume = 0\n')
 try:
  if subprocess.run(['pgrep','-x','neo98-host'],capture_output=True).returncode==0:__import__('test_support').skip('exclusive host access is unavailable: another neo98-host is active')
  env=dict(neo98.tool_environment(),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NEO98_CPU='fex',NEO98_HOST_STATS='1',NEO98_HOST_INPUT=f'28:shot={out}/frame.ppm;30:menu=quit',NEO98_HOST_DEADLINE='50',NEO98_SETTINGS_DIR=str(out/'settings'),NEO98_LOG_DIR=str(out/'logs'))
  with (out/'host.log').open('w') as log:
   before=resource.getrusage(resource.RUSAGE_CHILDREN);started=time.monotonic()
   child=subprocess.Popen([a.host.resolve(),'-',ROOT/'build/generated/notepad-fixedsys.fnt','--exe','C:\\AC\\ACWIN.EXE','--command','ACWIN.EXE --profile --log','--system-tree',private,'--drive-c',private,'--no-registry-file','--quiet'],env=env,stdout=log,stderr=subprocess.STDOUT)
   try:code=child.wait(timeout=70)
   except subprocess.TimeoutExpired:child.terminate();child.wait(timeout=20);raise
   elapsed=time.monotonic()-started;after=resource.getrusage(resource.RUSAGE_CHILDREN)
  text=(out/'host.log').read_text(errors='replace');assert code==0 and 'menu-script quit ok' in text,(code,text[-2000:])
  assert 'CRASHREPORT' not in text and (out/'frame.ppm').is_file()
  for name in ('ACWIN.LOG','ACWIN.ERR'):
   if (private/'AC'/name).exists():shutil.copyfile(private/'AC'/name,out/name)
  cpu=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime
  result={'host_sha256':hashlib.sha256(a.host.read_bytes()).hexdigest(),'game_sha256':hashlib.sha256((source/'ACWIN.EXE').read_bytes()).hexdigest(),'cpu_seconds':cpu,'wall_seconds':elapsed,'cores':cpu/elapsed}
  stats=re.findall(r'NEO98 HOST stats t=([\d.]+) presents=(\d+) sound=(\d+) audible=(\d+) cpu=([\d.]+)',text)
  if stats:result['host_stats']=dict(zip(('seconds','presents','sound_frames','audible_frames','cpu_seconds'),stats[-1]))
  (out/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
 finally:shutil.rmtree(private)
if __name__=='__main__':main()
