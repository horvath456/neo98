#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build and run the native window-region functional test under ASan/UBSan."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[1]
SOURCES=['gdi','dibeng','dib_text','win98_rect','user','user_draw','text_edit','win98_dialog','win98_parameters']
def test(source=None,out=None):
 source=Path(source or ROOT/'src').resolve();out=Path(out or ROOT/'build/window-regions').resolve()
 out.mkdir(parents=True,exist_ok=True)
 environment=os.environ.copy();environment['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
 binary=out/'native-asan'
 command=['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
          '-I'+str(source),'-I'+str(ROOT/'src'),str(ROOT/'tools/test_native_window_region.cpp')]
 command+=[str((source if (source/(n+'.cpp')).exists() else ROOT/'src')/(n+'.cpp'))for n in SOURCES]
 command+=['-o',str(binary)]
 for stage,cmd in (('build',command),('run',[str(binary)])):
  result=subprocess.run(cmd,capture_output=True,text=True,env=environment)
  (out/f'native-{stage}.log').write_text(result.stdout+result.stderr)
  if result.returncode or stage=='run':print(result.stdout+result.stderr,end='',flush=True)
  result.check_returncode()
 checks=[l for l in (out/'native-run.log').read_text().splitlines()if l.endswith('PASS')or l.endswith('FAIL')]
 report={'status':'PASS','ASan_UBSan':'PASS','checks':len(checks)-1,
  'window':{'origin':[120,90],'size':[121,101]},
  'region':'per-scanline ellipse assembled with the original boolean core',
  'source_sha256':{n:hashlib.sha256((source/(n+'.cpp')).read_bytes()).hexdigest()for n in ('user','gdi')}}
 (out/'native-results.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path);p.add_argument('--out',type=Path)
 a=p.parse_args();print(json.dumps(test(a.source,a.out),indent=2))
