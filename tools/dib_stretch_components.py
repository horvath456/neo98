#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Replay original Win98 stretch bodies and test the native GDI provider."""
import json
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/dib-stretch'
sys.path.insert(0, str(ROOT))
import neo98
IMPORTS = {
 'KERNEL32': {'ExitProcess':4,'GetLastError':0,'SetLastError':4},
 'GDI32': {'StretchBlt':44,'GetStretchBltMode':4,'SetStretchBltMode':8,
           'CreateCompatibleBitmap':12,'CreateDiscardableBitmap':12,
           'CreateCompatibleDC':4,'CreateDIBSection':24,'DeleteObject':4,'DeleteDC':4,
           'SelectObject':8,'GetObjectA':12,'GetPixel':12,'SaveDC':4,'RestoreDC':8,
           'StretchDIBits':52},
}
def test(source=None):
 source = Path(source).resolve() if source else ROOT/'src'
 OUT.mkdir(parents=True, exist_ok=True)
 env=neo98.tool_environment();env['NEO98_BRIDGE_SOURCE']=str(source);env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
 def run(name, command):
  result=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True)
  (OUT/(name+'.log')).write_text(result.stdout+result.stderr)
  if result.returncode:print(result.stdout+result.stderr)
  result.check_returncode();return result.stdout.strip()
 base=['clang++','-std=c++17','-O1','-g','-I'+str(source),'-I'+str(ROOT/'src')]
 shared=['-dynamiclib']if sys.platform=='darwin'else['-shared','-fPIC']
 sanitize=['-fsanitize=address,undefined','-fno-omit-frame-pointer'];reports={}
 for name in ('axis','raster','objects'):
  bridge=ROOT/'tools'/('dib_stretch_bridge.cpp'if name=='axis'else'dib_stretch_'+name+'_bridge.cpp')
  extra=[ROOT/'src/dibeng.cpp']if name=='raster'else[]
  run(name+'-compile',base+shared+[bridge]+extra+['-o',OUT/(name+'.dylib')])
  run(name+'-original',[neo98.analysis_python(),ROOT/'tools'/('test_dib_stretch_'+name+'.py')])
  reports[name]=json.loads((OUT/(name+'-results.json')).read_text())
  run(name+'-asan-compile',base+sanitize+['-DDIB_STRETCH_'+name.upper()+'_STANDALONE',bridge]+extra+['-o',OUT/(name+'-asan')])
  reports[name]['sanitizers']=run(name+'-asan',[OUT/(name+'-asan'),OUT/(name+'-cases.bin')])
  print('Stretch '+name+': PASS',flush=True)
 run('native-compile',base+sanitize+[ROOT/'tools/test_dib_stretch_native.cpp',ROOT/'src/dibeng.cpp',ROOT/'src/dib_text.cpp',ROOT/'src/win98_rect.cpp','-o',OUT/'native-asan'])
 reports['native']={'result':run('native',[OUT/'native-asan']),'source':str(source),'sanitizers':['address','undefined']}
 (OUT/'component-results.json').write_text(json.dumps(reports,indent=2)+'\n')
 print('Stretch native: PASS',flush=True);return reports
if __name__=='__main__':test(sys.argv[1]if len(sys.argv)>1 else None)
