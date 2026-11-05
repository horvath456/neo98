# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build/test the original USER shell-window registration package."""
from pathlib import Path
import json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/shell-registration'
IMPORTS={'KERNEL32':{'ExitProcess':4,'GetModuleHandleA':4},'USER32':{'RegisterClassA':4,'CreateWindowExA':48,'DestroyWindow':4,'DefWindowProcA':16,'SetShellWindow':4,'GetShellWindow':0,'GetWindow':8,'IsWindow':4,'SetWindowPos':28,'SetWindowLongA':12}}
def test():
 sys.path.insert(0,str(ROOT));import neo98
 reports={}
 for suffix,path in [('', 'results.json'),('_native','native-results.json')]:
  name='test_shell_registration'+suffix;run=subprocess.run([str(neo98.analysis_python()),str(ROOT/'tools'/f'{name}.py')],env=neo98.tool_environment(),capture_output=True,text=True);(OUT/f'{name}.log').write_text(run.stdout+run.stderr)
  if run.returncode:print(run.stdout+run.stderr)
  run.check_returncode();reports[name]=json.loads((OUT/path).read_text());print(name+': PASS',flush=True)
 return reports
