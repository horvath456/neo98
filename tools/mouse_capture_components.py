#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Original mouse-capture comparisons and sanitizer replay, with explicit output directory."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
P=Path(__file__).resolve();ROOT=next(p for p in P.parents if(p/'neo98.py').exists()and(p/'build/gdi-audit/USER.EXE').exists());sys.path.insert(0,str(ROOT));import neo98
IMPORTS={'KERNEL32':{'ExitProcess':4,'Sleep':4,'GetCurrentThreadId':0},'USER32':{'RegisterClassA':4,'CreateWindowExA':48,'DefWindowProcA':16,'DestroyWindow':4,'IsWindow':4,'ShowWindow':8,'SetCapture':4,'GetCapture':0,'ReleaseCapture':0,'SendMessageA':16,'SetWindowTextA':8,'GetMessageA':16,'TranslateMessage':4,'DispatchMessageA':4,'PostMessageA':16,'CreatePopupMenu':0,'AppendMenuA':16,'TrackPopupMenu':28,'EndMenu':0,'DestroyMenu':4}}
def test(source_root,out):
 source_root=Path(source_root).resolve();out=Path(out).resolve();out.mkdir(parents=True,exist_ok=True);env=neo98.tool_environment();env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
 def run(name,args):
  p=subprocess.run(list(map(str,args)),env=env,capture_output=True,text=True);(out/(name+'.log')).write_text(p.stdout+p.stderr)
  if p.returncode:print(p.stdout+p.stderr)
  p.check_returncode();return p.stdout.strip()
 run('bridge-build',['clang++','-std=c++17','-O1','-dynamiclib',source_root/'tools/mouse_capture_bridge.cpp','-o',out/'capture.dylib'])
 for tool in('mouse_capture_audit','test_mouse_capture_original','test_mouse_capture_wrappers'):run(tool,[neo98.analysis_python(),source_root/'tools'/(tool+'.py'),'--out',out])
 run('replay-build',['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',source_root/'tools/test_mouse_capture_replay.cpp','-o',out/'replay'])
 result={name:json.loads((out/(name+'-results.json')).read_text())for name in('original','wrapper')};result['sanitizers']=run('replay',[out/'replay',out/'capture-cases.bin']);result['replay_sha256']=hashlib.sha256((out/'replay').read_bytes()).hexdigest();(out/'component-results.json').write_text(json.dumps(result,indent=2)+'\n');print(result['sanitizers']);return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=P.parents[1]);p.add_argument('--out',type=Path,default=ROOT/'build/mouse-capture');a=p.parse_args();test(a.source_root,a.out)
