# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build and test native popup tracking with original USER algorithms."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/menu-tracking'
IMPORTS={'KERNEL32':{'ExitProcess':4,'GetModuleHandleA':4},'USER32':{'CreatePopupMenu':0,'AppendMenuA':16,'DestroyMenu':4,'TrackPopupMenu':28,'TrackPopupMenuEx':24,'EndMenu':0,'RegisterClassA':4,'CreateWindowExA':48,'DefWindowProcA':16,'DestroyWindow':4,'SetWindowTextA':8,'GetMenuItemRect':16,'MenuItemFromPoint':16}}

IMPORTS['USER32'].update({'GetMenuBarInfo':16,'FindWindowA':8})

def test():
 import json,subprocess,sys
 sys.path.insert(0,str(ROOT));import neo98
 reports={};OUT.mkdir(exist_ok=True)
 for script,report in [('test_menu_geometry','geometry-results.json'),('test_menu_placement','placement-results.json'),('test_menu_tracking','tracking-results.json'),('test_menu_bar_info','bar-info-results.json'),('test_menu_tracking_native','native-results.json'),('test_menu_bar_provider','bar-provider-results.json')]:
  run=subprocess.run([str(neo98.analysis_python()),str(ROOT/'tools'/f'{script}.py')],env=neo98.tool_environment(),capture_output=True,text=True);(OUT/f'{script}.log').write_text(run.stdout+run.stderr)
  if run.returncode:print(run.stdout+run.stderr)
  run.check_returncode();reports[script]=json.loads((OUT/report).read_text());print(script+': PASS',flush=True)
 return reports
