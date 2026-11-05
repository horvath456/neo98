#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Original GDI pen and DIBENG line tests with compact sanitizer replays."""
from pathlib import Path
import json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/pen-line';sys.path.insert(0,str(ROOT));import neo98
IMPORTS={'KERNEL32':{'ExitProcess':4,'GetLastError':0,'SetLastError':4},'GDI32':{
 'CreatePen':12,'CreatePenIndirect':4,'ExtCreatePen':20,'GetObjectA':12,'SelectObject':8,'DeleteObject':4,
 'MoveToEx':16,'GetCurrentPositionEx':8,'LineTo':12,'Polyline':12,'SetROP2':8,'GetROP2':4,
 'CreateCompatibleDC':4,'CreateDIBSection':24,'DeleteDC':4,'SaveDC':4,'RestoreDC':8,'GetPixel':12,
 'GetStockObject':4,'SetBkMode':8,'SetBkColor':8,'IntersectClipRect':20,'SetViewportOrgEx':16}}
def test(source=None):
 source=Path(source).resolve()if source else ROOT/'src';OUT.mkdir(exist_ok=True);env=neo98.tool_environment();env['NEO98_BRIDGE_SOURCE']=str(source);env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
 def run(name,command):
  p=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True);(OUT/(name+'.log')).write_text(p.stdout+p.stderr)
  if p.returncode:print(p.stdout+p.stderr)
  p.check_returncode();return p.stdout.strip()
 base=['clang++','-std=c++17','-O1','-g','-I'+str(source),'-I'+str(ROOT/'src')];shared=['-dynamiclib']if sys.platform=='darwin'else['-shared','-fPIC'];sanitize=['-fsanitize=address,undefined','-fno-omit-frame-pointer'];reports={}
 for family in ('objects','state','lifetime','query','raster'):
  bridge=ROOT/'tools'/('pen_line_'+family+'_bridge.cpp');run(family+'-compile',base+shared+[bridge,'-o',OUT/(family+'.dylib')]);run(family+'-original',[neo98.analysis_python(),ROOT/'tools'/('test_pen_line_'+family+'.py')]);reports[family]=json.loads((OUT/(family+'-results.json')).read_text());print('Pen '+family+': PASS',flush=True)
 bridges=[ROOT/'tools'/('pen_line_'+family+'_bridge.cpp')for family in ('objects','state','lifetime','query')]
 run('replay-compile',base+sanitize+[ROOT/'tools/test_pen_line_replay.cpp',*bridges,'-o',OUT/'replay-asan']);reports['sanitizers']=run('replay',[OUT/'replay-asan',*[OUT/(family+'-cases.bin')for family in ('objects','state','lifetime','query')]])
 run('raster-asan-compile',base+sanitize+['-DPEN_LINE_RASTER_STANDALONE',ROOT/'tools/pen_line_raster_bridge.cpp','-o',OUT/'raster-asan']);reports['raster']['sanitizers']=run('raster-asan',[OUT/'raster-asan',OUT/'raster-cases.bin'])
 run('native-compile',base+sanitize+[ROOT/'tools/test_pen_line_native.cpp',ROOT/'src/dibeng.cpp',ROOT/'src/dib_text.cpp',ROOT/'src/win98_rect.cpp','-o',OUT/'native-asan']);reports['native']={'result':run('native',[OUT/'native-asan']),'source':str(source),'sanitizers':['address','undefined']}
 (OUT/'component-results.json').write_text(json.dumps(reports,indent=2)+'\n');print('Pen native and sanitizers: PASS',flush=True);return reports
if __name__=='__main__':test(sys.argv[1]if len(sys.argv)>1 else None)
