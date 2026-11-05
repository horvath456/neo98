#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
from pathlib import Path
ROOT=next(p for p in Path(__file__).resolve().parents if(p/'neo98.py').is_file());OUT=ROOT/'build/font-query'
IMPORTS={'KERNEL32':{'ExitProcess':4,'GetLastError':0,'SetLastError':4},'GDI32':{'CreateCompatibleDC':4,'DeleteDC':4,'SelectObject':8,'GetStockObject':4,'SaveDC':4,'RestoreDC':8,'CreateFontIndirectA':4,'DeleteObject':4,'GetTextFaceA':12,'GetCharWidthA':16,'GetTextFaceW':12,'GetTextMetricsA':8,'GetTextMetricsW':8,'EnumFontsA':16,'EnumFontFamiliesA':16,'EnumFontFamiliesExA':20,'EnumFontsW':16,'EnumFontFamiliesW':16,'EnumFontFamiliesExW':20}}
import json,subprocess,sys
sys.path.insert(0,str(ROOT));import neo98
def test(source=None):
 source=Path(source).resolve()if source else ROOT/'src';here=Path(__file__).resolve().parent;OUT.mkdir(exist_ok=True);env=neo98.tool_environment();env['NEO98_BRIDGE_SOURCE']=str(source);env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
 def run(name,args):
  p=subprocess.run(list(map(str,args)),env=env,capture_output=True,text=True);(OUT/(name+'.log')).write_text(p.stdout+p.stderr)
  if p.returncode:print(p.stdout+p.stderr)
  p.check_returncode();return p.stdout
 base=['clang++','-std=c++17','-O1','-g','-I'+str(source)]
 run('core-compile',base+['-dynamiclib',here/'font_query_bridge.cpp','-o',OUT/'core.dylib'])
 for name in ('query','family','width'):run(name+'-original',[neo98.analysis_python(),here/('test_font_'+name+'.py')])
 run('replay-compile',base+['-fsanitize=address,undefined',here/'test_font_query_replay.cpp','-o',OUT/'replay-asan'])
 sanitizer=run('replay',[OUT/'replay-asan',OUT/'face-cases.bin',OUT/'enum-cases.bin',OUT/'family-cases.bin',OUT/'width-cases.bin'])
 run('metadata-compile',base+['-fsanitize=address,undefined',here/'test_font_metadata.cpp',*[source/(name+'.cpp')for name in ('dib_text','dibeng','win98_rect')],'-o',OUT/'metadata-asan'])
 metadata=run('metadata',[OUT/'metadata-asan',ROOT/'build/generated/notepad-fixedsys.fnt'])
 report={name:json.loads((OUT/(name+'-results.json')).read_text())for name in ('core','family','width')};report['original_cases']=sum(r['cases']for r in report.values());report['sanitizers']=sanitizer.strip();report['metadata_sanitizers']=metadata.strip();(OUT/'component-results.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return report
if __name__=='__main__':test(sys.argv[1]if len(sys.argv)>1 else None)
