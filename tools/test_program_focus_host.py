#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""original games, guest-side launch during exclusive mode. Use serial.py."""
import json, shutil, struct, sys
from pathlib import Path
sys.path[:0]=[str(Path(__file__).resolve().parent),str(Path(__file__).resolve().parents[1])]
from native_app import ROOT
from ui_clipboard_test_support import fixture
from test_device_manager_host import host

from external_paths import external_path
def main():
 from test_support import require_file
 from external_paths import external_path
 require_file(external_path('NEO98_COLLECTION') / 'GAMES/ALTEPC/Spiele/Moorhuhn21')
 out=ROOT/'build/program-focus-host';out.mkdir(parents=True,exist_ok=True)
 games=external_path('NEO98_COLLECTION')
 imports={'KERNEL32':{'ExitProcess':4,'Sleep':4,'CreateProcessA':40,'CloseHandle':4,'CreateFileA':28,'WriteFile':20},
          'USER32':{'GetForegroundWindow':0,'GetWindowThreadProcessId':8,'GetWindowLongA':8,'GetWindowRect':8,'FindWindowA':8}}
 exe=fixture(out/'fixture',ROOT/'tools/program_focus_fixture.c',imports)
 def prepare(run):
  shutil.copy(exe,run/'c/FOCUS.EXE')
  shutil.copytree(games/'GAMES/ALTEPC/Spiele/Moorhuhn21',run/'c/MH2')
 code,text,run=host((ROOT/'build/native-app/neo98-host').resolve(),out,'handoff','C:\\FOCUS.EXE','C:\\FOCUS.EXE',
  ['18:tops=2','19:shot={out}/before.ppm','25:tops=2','34:tops=2','35:shot={out}/after.ppm','40:menu=quit'],
  65,ROOT/'build/win98-c',ROOT/'build/win98-c.registry',drives=[f'D={games}'],prepare=prepare,keep=True,
  environment={'NEO98_HOST_SCREEN':'1024x768'})
 result={'code':code}
 try:
  for name in ('FOCUS1.BIN','FOCUS2.BIN'):
   b=(run/'c'/name).read_bytes();(out/name).write_bytes(b);result[name]=struct.unpack('<14I',b)
  (out/'results.json').write_text(json.dumps(result,indent=2)+'\n')
  assert code==0 and 'CRASHREPORT' not in text
  assert result['FOCUS1.BIN'][6]==result['FOCUS1.BIN'][3],result
  assert result['FOCUS2.BIN'][6]==result['FOCUS2.BIN'][4],result
  assert result['FOCUS2.BIN'][7] and result['FOCUS2.BIN'][8]&0x20000000,result
  assert (run/'before.ppm').is_file() and (run/'after.ppm').is_file()
  print(json.dumps(result,indent=2))
 finally:shutil.rmtree(run/'c')
if __name__=='__main__':main()
