# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build and validate the Win98 menu object family."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/menu-family'
IMPORTS={'KERNEL32':{'ExitProcess':4},'USER32':{'CreateMenu':0,'CreatePopupMenu':0,'DestroyMenu':4,'IsMenu':4,'AppendMenuA':16,'InsertMenuA':20,'ModifyMenuA':20,'RemoveMenu':12,'DeleteMenu':12,'GetMenuItemCount':4,'GetMenuItemID':8,'GetSubMenu':8,'GetMenuState':12,'CheckMenuItem':12,'EnableMenuItem':12,'CheckMenuRadioItem':20,'SetMenuDefaultItem':12,'GetMenuDefaultItem':12,'GetMenuStringA':20,'GetMenuItemInfoA':16,'SetMenuItemInfoA':16,'InsertMenuItemA':16,'GetMenuInfo':8,'SetMenuInfo':8,'GetMenuContextHelpId':4,'SetMenuContextHelpId':8}}

IMPORTS['KERNEL32'].update({'GetLastError':0,'SetLastError':4})
IMPORTS['USER32'].update({'AppendMenuW':16,'GetMenuItemInfoW':16,'GetMenuStringW':20,'InsertMenuItemW':16,'InsertMenuW':20,'ModifyMenuW':20,'SetMenuItemInfoW':16})
def test():
 import json,subprocess,sys
 sys.path.insert(0,str(ROOT));import neo98
 reports={}
 for script,report in [('test_menu_family','results.json'),('test_menu_public','public-results.json'),('test_menu_details','details-results.json'),('test_menu_family_native','native-results.json')]:
  run=subprocess.run([str(neo98.analysis_python()),str(ROOT/'tools'/f'{script}.py')],env=neo98.tool_environment(),capture_output=True,text=True);(OUT/f'{script}.log').write_text(run.stdout+run.stderr)
  if run.returncode:print(run.stdout+run.stderr)
  run.check_returncode();reports[script]=json.loads((OUT/report).read_text());print(script+': PASS',flush=True)
 return reports
