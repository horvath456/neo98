#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build the change-notification guest using the existing x86 toolchain."""
import argparse,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT));import neo98
IMPORTS={'KERNEL32':{'FindFirstChangeNotificationA':12,'FindFirstChangeNotificationW':12,'FindNextChangeNotification':4,'FindCloseChangeNotification':4,'WaitForSingleObject':8,'WaitForMultipleObjects':16,'CreateFileA':28,'CloseHandle':4,'WriteFile':20,'DeleteFileA':4,'CreateDirectoryA':8,'RemoveDirectoryA':4,'SetFileAttributesA':8,'GetLastError':0,'ExitProcess':4}}
def build(out):
 out.mkdir(parents=True,exist_ok=True);env=neo98.tool_environment();libs=[]
 def run(args):subprocess.run(list(map(str,args)),env=env,check=True)
 for dll,entries in IMPORTS.items():
  d=out/(dll+'.def');lib=out/(dll+'.lib');d.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(f'{n}@{b}\n'for n,b in entries.items()));run(['llvm-dlltool','-m','i386','-k','-d',d,'-l',lib]);libs.append(lib)
 obj=out/'fixture.obj';exe=out/'CHANGENO.EXE';run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-c',ROOT/'tools/change_notification_fixture.c','-o',obj]);run(['lld-link','/entry:mainCRTStartup','/subsystem:console','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),obj,*libs]);return exe
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);print(build(p.parse_args().out.resolve()))
