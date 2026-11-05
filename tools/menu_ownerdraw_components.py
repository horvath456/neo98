# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build and test owner-draw, bitmap and column menu items with original USER measurement."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/menu-ownerdraw'
IMPORTS={
 'KERNEL32':{'ExitProcess':4,'GetModuleHandleA':4,'GetCurrentThreadId':0,
             'CreateThread':24,'WaitForSingleObject':8,'CloseHandle':4,'Sleep':4},
 'USER32':{'CreatePopupMenu':0,'AppendMenuA':16,'DestroyMenu':4,'TrackPopupMenu':28,'EndMenu':0,
           'RegisterClassA':4,'CreateWindowExA':48,'DefWindowProcA':16,'DestroyWindow':4,
           'GetMenuItemRect':16,'MenuItemFromPoint':16,'WindowFromDC':4,'SetMenuItemBitmaps':20,'CheckMenuItem':12,
           'GetMenuCheckMarkDimensions':0,'SetMenuItemInfoA':16,'SetMenuDefaultItem':12,'FillRect':12,
           'PeekMessageA':20,'TranslateMessage':4,'DispatchMessageA':4},
 'GDI32':{'CreateBitmap':20,'DeleteObject':4,'PatBlt':24,'SetBkColor':8},
}

def test():
 import json,subprocess,sys
 sys.path.insert(0,str(ROOT));import neo98
 reports={};OUT.mkdir(exist_ok=True)
 for script,report in [('test_menu_ownerdraw','ownerdraw-results.json'),
                       ('test_menu_ownerdraw_native','native-results.json')]:
  run=subprocess.run([str(neo98.analysis_python()),str(ROOT/'tools'/f'{script}.py')],
                     env=neo98.tool_environment(),capture_output=True,text=True)
  (OUT/f'{script}.log').write_text(run.stdout+run.stderr)
  if run.returncode:print(run.stdout+run.stderr)
  run.check_returncode();reports[script]=json.loads((OUT/report).read_text());print(script+': PASS',flush=True)
 return reports
