#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build the indirect dialog guest without rebuilding the runtime."""
import argparse,subprocess,sys
from pathlib import Path
ROOT=next(p for p in Path(__file__).resolve().parents if(p/'neo98.py').is_file());HERE=Path(__file__).resolve().parent;sys.path.insert(0,str(ROOT));import neo98
IMPORTS={'KERNEL32': {'GetModuleHandleA': 4, 'VirtualAlloc': 16, 'VirtualProtect': 16, 'VirtualFree': 12, 'GetLastError': 0, 'SetLastError': 4, 'ExitProcess': 4}, 'USER32': {'CreateDialogIndirectParamA': 20, 'DialogBoxIndirectParamA': 20, 'CreateDialogIndirectParamW': 20, 'DialogBoxIndirectParamW': 20, 'CreateWindowExA': 48, 'GetWindow': 8, 'GetDlgItem': 8, 'GetWindowLongA': 8, 'GetWindowTextA': 12, 'IsWindow': 4, 'IsWindowEnabled': 4, 'EnableWindow': 8, 'DestroyWindow': 4, 'EndDialog': 8, 'SetWindowLongA': 12, 'SendMessageA': 16, 'CallWindowProcA': 20, 'DefWindowProcA': 16, 'DefDlgProcA': 16, 'SetCapture': 4, 'ReleaseCapture': 0, 'GetDC': 4, 'ReleaseDC': 8,'FillRect':12,'GetSysColorBrush':4}}
IMPORTS['GDI32']={'CreateSolidBrush':4,'DeleteObject':4,'GetPixel':12,'GetBkColor':4,'GetTextColor':4}
def build(out):
 out.mkdir(parents=True,exist_ok=True);env=neo98.tool_environment();libs=[]
 def run(args):subprocess.run(list(map(str,args)),env=env,check=True)
 for dll,entries in IMPORTS.items():
  d=out/(dll+'.def');lib=out/(dll+'.lib');d.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(f'{n}@{b}\n'for n,b in entries.items()));run(['llvm-dlltool','-m','i386','-k','-d',d,'-l',lib]);libs.append(lib)
 obj=out/'fixture.obj';exe=out/'DLGSUB.EXE';run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-c',HERE/'dialog_subclass_fixture.c','-o',obj]);run(['lld-link','/entry:mainCRTStartup','/subsystem:console','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),obj,*libs]);return exe
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);print(build(p.parse_args().out.resolve()))
