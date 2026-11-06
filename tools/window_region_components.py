#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Window-region original comparisons, sanitizer replay and native functional test."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
P=Path(__file__).resolve();ROOT=next((p for p in P.parents if(p/'neo98.py').exists()and(p/'build/gdi-audit/USER.EXE').exists()),None)or __import__('sys').exit('Run setup first: build/gdi-audit/USER.EXE is copied from the Windows 98 installation');sys.path.insert(0,str(ROOT));import neo98
def test(source_root,out):
 source_root=Path(source_root).resolve();out=Path(out).resolve();out.mkdir(parents=True,exist_ok=True)
 env=neo98.tool_environment();env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
 def run(name,args):
  p=subprocess.run(list(map(str,args)),env=env,capture_output=True,text=True)
  (out/(name+'.log')).write_text(p.stdout+p.stderr)
  if p.returncode:print(p.stdout+p.stderr)
  p.check_returncode();return p.stdout.strip()
 run('bridge-build',['clang++','-std=c++17','-O1','-dynamiclib',source_root/'tools/window_region_bridge.cpp',
                     '-o',out/'window-region.dylib'])
 for tool in('window_region_audit','test_window_region_original','test_window_region_wrappers'):
  run(tool,[neo98.analysis_python(),source_root/'tools'/(tool+'.py'),'--out',out])
 run('replay-build',['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                     source_root/'tools/test_window_region_replay.cpp','-o',out/'replay'])
 result={name:json.loads((out/(name+'-results.json')).read_text())for name in('audit','original','wrapper')}
 result['sanitizers']=run('replay',[out/'replay',out/'region-cases.bin'])
 result['replay_sha256']=hashlib.sha256((out/'replay').read_bytes()).hexdigest()
 sys.path.insert(0,str(source_root/'tools'));import test_native_window_region
 result['native']=test_native_window_region.test(source_root/'src',out)
 (out/'component-results.json').write_text(json.dumps(result,indent=2)+'\n')
 print(result['sanitizers']);return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=P.parents[1])
 p.add_argument('--out',type=Path,default=ROOT/'build/window-regions');a=p.parse_args();test(a.source_root,a.out)
