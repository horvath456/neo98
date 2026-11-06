#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Original message-metadata comparisons and sanitizer replay, with explicit output directory."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
P=Path(__file__).resolve();ROOT=next((p for p in P.parents if(p/'neo98.py').exists()and(p/'build/gdi-audit/USER.EXE').exists()),None)or __import__('sys').exit('Run setup first: build/gdi-audit/USER.EXE is copied from the Windows 98 installation');sys.path.insert(0,str(ROOT));import neo98
IMPORTS={'KERNEL32':{'ExitProcess':4,'Sleep':4,'GetTickCount':0,'GetCurrentThreadId':0,'CreateThread':24,'WaitForSingleObject':8,'CloseHandle':4},'USER32':{'RegisterClassA':4,'CreateWindowExA':48,'DefWindowProcA':16,'DestroyWindow':4,'ShowWindow':8,'SetFocus':4,'SetWindowTextA':8,'GetMessageTime':0,'GetMessagePos':0,'GetMessageExtraInfo':0,'SetMessageExtraInfo':4,'PeekMessageA':20,'GetMessageA':16,'PostMessageA':16,'PostQuitMessage':4,'SendMessageA':16,'TranslateMessage':4,'DispatchMessageA':4,'SetWindowsHookExA':16,'UnhookWindowsHookEx':4,'CallNextHookEx':16,'SetTimer':16,'KillTimer':8}}
def test(source_root,out):
 source_root=Path(source_root).resolve();out=Path(out).resolve();out.mkdir(parents=True,exist_ok=True);env=neo98.tool_environment();env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
 def run(name,args):
  p=subprocess.run(list(map(str,args)),env=env,capture_output=True,text=True);(out/(name+'.log')).write_text(p.stdout+p.stderr)
  if p.returncode:print(p.stdout+p.stderr)
  p.check_returncode();return p.stdout.strip()
 run('bridge-build',['clang++','-std=c++17','-O1','-dynamiclib',source_root/'tools/message_metadata_bridge.cpp','-o',out/'metadata.dylib'])
 for tool in('message_metadata_audit','test_message_metadata_original','test_message_metadata_wrappers'):run(tool,[neo98.analysis_python(),source_root/'tools'/(tool+'.py'),'--out',out])
 run('replay-build',['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',source_root/'tools/test_message_metadata_replay.cpp','-o',out/'replay'])
 result={name:json.loads((out/(name+'-results.json')).read_text())for name in('original','wrapper')};result['sanitizers']=run('replay',[out/'replay',out/'metadata-cases.bin']);result['replay_sha256']=hashlib.sha256((out/'replay').read_bytes()).hexdigest();(out/'component-results.json').write_text(json.dumps(result,indent=2)+'\n');print(result['sanitizers']);return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=P.parents[1]);p.add_argument('--out',type=Path,default=ROOT/'build/message-metadata');a=p.parse_args();test(a.source_root,a.out)
