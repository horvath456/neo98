#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Original GDI pixel comparisons and sanitizer replay, with explicit output directory."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
P=Path(__file__).resolve();ROOT=next((p for p in P.parents if(p/'neo98.py').exists()and(p/'build/gdi-audit/USER.EXE').exists()),None)or __import__('sys').exit('Run setup first: build/gdi-audit/USER.EXE is copied from the Windows 98 installation');sys.path.insert(0,str(ROOT));import neo98
IMPORTS={'KERNEL32':{'ExitProcess':4,'GetLastError':0,'SetLastError':4},'GDI32':{'CreateCompatibleDC':4,'CreateICA':16,'DeleteDC':4,'DeleteObject':4,'SelectObject':8,'CreateDIBSection':24,'SetPixel':16,'SetPixelV':16,'GetPixel':12,'GetNearestColor':8,'SetROP2':8,'SaveDC':4,'RestoreDC':8,'IntersectClipRect':20,'SetWindowOrgEx':16,'SetViewportOrgEx':16,'MoveToEx':16,'GetCurrentPositionEx':8}}
def test(source_root,out):
 source_root=Path(source_root).resolve();out=Path(out).resolve();out.mkdir(parents=True,exist_ok=True);env=neo98.tool_environment();env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
 def run(name,args):
  p=subprocess.run(list(map(str,args)),env=env,capture_output=True,text=True);(out/(name+'.log')).write_text(p.stdout+p.stderr)
  if p.returncode:print(p.stdout+p.stderr)
  p.check_returncode();return p.stdout.strip()
 for kind in('core','driver'):
  sources=[source_root/'tools'/('gdi_pixel_'+kind+'_bridge.cpp')]+([source_root/'src/dibeng.cpp']if kind=='driver'else[])
  run(kind+'-build',['clang++','-std=c++17','-O1','-dynamiclib',*sources,'-o',out/(kind+'.dylib')])
 for tool in('gdi_pixel_audit','test_gdi_pixel_core','test_gdi_pixel_driver','test_gdi_pixel_wrappers'):run(tool,[neo98.analysis_python(),source_root/'tools'/(tool+'.py'),'--out',out])
 return replay(source_root,out,run)
def replay(source_root,out,run):
 reports={name:json.loads((out/(name+'-results.json')).read_text())for name in('core','driver','wrapper')};reports['sanitizers']={}
 for kind in('core','driver'):
  extra=['-DGDI_PIXEL_DRIVER_REPLAY',source_root/'src/dibeng.cpp']if kind=='driver'else[]
  binary=out/(kind+'-replay');run(kind+'-replay-build',['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',source_root/'tools/test_gdi_pixel_replay.cpp',*extra,'-o',binary]);reports['sanitizers'][kind]={'result':run(kind+'-replay',[binary,out/(kind+'-cases.bin')]),'sha256':hashlib.sha256(binary.read_bytes()).hexdigest()}
 (out/'component-results.json').write_text(json.dumps(reports,indent=2)+'\n');print(reports['sanitizers']);return reports
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=P.parents[1]);p.add_argument('--out',type=Path,default=ROOT/'build/gdi-pixel');a=p.parse_args();test(a.source_root,a.out)
