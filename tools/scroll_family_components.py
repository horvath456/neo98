#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/scroll-family'
IMPORTS={'KERNEL32':{'ExitProcess':4},'USER32':{'RegisterClassA':4,'CreateWindowExA':48,'DefWindowProcA':16,'DestroyWindow':4,'IsWindow':4,'ShowWindow':8,'GetClientRect':8,'GetWindowLongA':8,'SetScrollInfo':16,'GetScrollInfo':12,'SetScrollPos':16,'GetScrollPos':8,'SetScrollRange':20,'GetScrollRange':16,'ShowScrollBar':12,'EnableScrollBar':12}}

def test(source=None):
 import sys,subprocess,json
 sys.path.insert(0,str(ROOT));import neo98
 source=Path(source).resolve()if source else ROOT/'src';OUT.mkdir(exist_ok=True);env=neo98.tool_environment();env['NEO98_BRIDGE_SOURCE']=str(source);env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
 def run(name,args):
  p=subprocess.run(list(map(str,args)),cwd=ROOT,env=env,capture_output=True,text=True);(OUT/(name+'.log')).write_text(p.stdout+p.stderr)
  if p.returncode:print(p.stdout+p.stderr)
  p.check_returncode();return p.stdout.strip()
 shared=['-dynamiclib']if sys.platform=='darwin'else['-shared','-fPIC']
 run('original-build',['clang++','-std=c++17','-O1',*shared,'-I'+str(source),ROOT/'tools/scroll_family_bridge.cpp','-o',OUT/'scroll.dylib'])
 for name in ('state','owner','wrappers','draw'):run(name+'-original',[neo98.analysis_python(),ROOT/('tools/test_scroll_family_'+name+'.py')])
 run('replay-build',['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(source),ROOT/'tools/test_scroll_family_replay.cpp','-o',OUT/'replay-asan'])
 result={name:json.loads((OUT/(name+'-results.json')).read_text())for name in('state','owner','wrapper','draw')};result['sanitizers']=run('replay',[OUT/'replay-asan',OUT]);(OUT/'component-results.json').write_text(json.dumps(result,indent=2)+'\n');print(result['sanitizers']);return result
if __name__=='__main__':
 import sys
 test(sys.argv[1]if len(sys.argv)>1 else None)
