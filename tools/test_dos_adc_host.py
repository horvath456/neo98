#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""DOS ADC captures a synthetic microphone with dummy audio/video.

Run under serial.py. The COM fixture checks DMA pacing/IRQ, masking,
auto-init, D0/D4 pause, DAh stop, and emits the samples for signal checks.
"""
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['nasm'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)
import argparse,json,os,shutil,struct,subprocess,sys
from pathlib import Path
sys.path[:0]=[str(Path(__file__).resolve().parent),str(Path(__file__).resolve().parents[1])]
import neo98
from native_app import ROOT

def main():
 p=argparse.ArgumentParser();p.add_argument('--host',type=Path,default=ROOT/'build/native-app/neo98-host');p.add_argument('--source',choices=['sine','silence'],default='sine');a=p.parse_args()
 out=ROOT/'build/dos-adc-host'/a.source;out.mkdir(parents=True,exist_ok=True)
 env=dict(neo98.tool_environment(),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NEO98_TEST_MICROPHONE=a.source,NEO98_HOST_DEADLINE='40',NEO98_HOST_INPUT='15:menu=quit',NEO98_SETTINGS_DIR=str(out/'settings'),NEO98_LOG_DIR=str(out/'logs'))
 exe=out/'SBADC.COM'
 subprocess.run(['nasm','-f','bin',str(ROOT/'tools/dos_programs/sb_adc.asm'),'-o',str(exe)],env=env,check=True)
 private=out/'c'
 if private.exists():shutil.rmtree(private)
 subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R',str((__import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or __import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or ROOT / 'build/win98-c'))).resolve()),str(private)],check=True)
 shutil.copyfile(exe,private/'SBADC.COM')
 try:
  if subprocess.run(__import__('test_support').other_hosts_command(),capture_output=True).returncode==0:__import__('test_support').skip('exclusive host access is unavailable: another neo98-host is active')
  with (out/'host.log').open('w') as log:
   child=subprocess.Popen([a.host,'-',ROOT/'build/generated/notepad-fixedsys.fnt','--exe','C:\\SBADC.COM','--drive-c',private,'--system-tree',private,'--no-registry-file','--quiet'],env=env,stdout=log,stderr=subprocess.STDOUT)
   try:code=child.wait(timeout=60)
   except subprocess.TimeoutExpired:child.terminate();child.wait(timeout=20);raise
  text=(out/'host.log').read_text(errors='replace')
  assert code==0 and 'DOSVM finished code=0' in text,(code,text[-4000:])
  assert 'CRASHREPORT' not in text
  timing=struct.unpack('<2H',(private/'ADC.OUT').read_bytes());assert 5<=timing[0]<=20 and timing[1]>=6,timing
  result=dict(ticks=timing[0],auto_irqs=timing[1],source=a.source,samples={})
  for name,size in [('ADC20.RAW',256),('ADC24.RAW',4096),('ADC2C.RAW',4096)]:
   b=(private/name).read_bytes();(out/name).write_bytes(b);assert len(b)==size,(name,len(b))
   if a.source=='silence':assert set(b)=={128},(name,min(b),max(b))
   else:
    assert min(b)<60 and max(b)>195,(name,min(b),max(b))
    if name!='ADC20.RAW':
     signal=b[512:];crossings=sum(x<128<=y for x,y in zip(signal,signal[1:]));assert 100<crossings<200,(name,crossings)
   result['samples'][name]=dict(bytes=len(b),minimum=min(b),maximum=max(b))
  (out/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
 finally:shutil.rmtree(private)
if __name__=='__main__':main()
