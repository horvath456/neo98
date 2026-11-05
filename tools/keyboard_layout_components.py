#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
from pathlib import Path
ROOT=next(p for p in Path(__file__).resolve().parents if(p/'neo98.py').is_file());OUT=ROOT/'build/keyboard-layout'
IMPORTS={'KERNEL32':{'ExitProcess':4,'GetLastError':0,'SetLastError':4,'GetCurrentThreadId':0,'CreateThread':24,'WaitForSingleObject':8,'CloseHandle':4},'USER32':{'GetKeyboardLayout':4,'GetKeyboardLayoutList':8,'GetKeyboardLayoutNameA':4,'GetKeyboardLayoutNameW':4,'LoadKeyboardLayoutW':8,'LoadKeyboardLayoutA':8,'UnloadKeyboardLayout':4,'ActivateKeyboardLayout':8,'RegisterClassA':4,'CreateWindowExA':48,'DestroyWindow':4,'EnableWindow':8,'DefWindowProcA':16}}

import json,subprocess,sys
sys.path.insert(0,str(ROOT));import neo98
def test(source=None):
 source=Path(source).resolve()if source else ROOT/'src';here=Path(__file__).resolve().parent;OUT.mkdir(exist_ok=True);env=neo98.tool_environment();env['NEO98_BRIDGE_SOURCE']=str(source);env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
 def run(name,args):
  p=subprocess.run(list(map(str,args)),env=env,capture_output=True,text=True);(OUT/(name+'.log')).write_text(p.stdout+p.stderr)
  if p.returncode:print(p.stdout+p.stderr)
  p.check_returncode();return p.stdout
 base=['clang++','-std=c++17','-O1','-g','-I'+str(source)]
 run('core-compile',base+['-dynamiclib',here/'keyboard_layout_loader_bridge.cpp','-o',OUT/'core.dylib'])
 for name in ('','_loader','_public'):run('original'+name,[neo98.analysis_python(),here/('test_keyboard_layout'+name+'.py')])
 run('replay-compile',base+['-fsanitize=address,undefined',here/'test_keyboard_layout_replay.cpp','-o',OUT/'replay-asan'])
 sanitizer=run('replay',[OUT/'replay-asan',OUT/'core-cases.bin',OUT/'loader-cases.bin'])
 report={name:json.loads((OUT/(name+'-results.json')).read_text())for name in ('core','loader','public')};report['original_cases']=sum(r['cases']for r in report.values());report['sanitizers']=sanitizer.strip();(OUT/'component-results.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return report
if __name__=='__main__':test(sys.argv[1]if len(sys.argv)>1 else None)
