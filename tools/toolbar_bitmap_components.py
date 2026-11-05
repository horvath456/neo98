#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
from pathlib import Path
import shutil,struct,subprocess,sys
ROOT=next(p for p in Path(__file__).resolve().parents if(p/'neo98.py').is_file());OUT=ROOT/'build/toolbar-bitmap';sys.path.insert(0,str(ROOT));import neo98
IMPORTS={'KERNEL32':{'ExitProcess':4,'GetModuleHandleA':4},'COMCTL32':{'InitCommonControls':0,'ImageList_GetImageCount':4,'ImageList_Destroy':4},'USER32':{'CreateWindowExA':48,'DestroyWindow':4,'SendMessageA':16,'GetWindowLongA':8,'LoadBitmapA':8,'GetDC':4,'UpdateWindow':4,'ReleaseDC':8,'ScreenToClient':8},'GDI32':{'DeleteObject':4,'GetPixel':12}}
def build_resource(env, output=None):
 output=Path(output) if output is not None else OUT
 output.mkdir(parents=True,exist_ok=True)
 # Two actual 16x15 24bpp cells: red then blue. No copyrighted asset in fixture.
 pixels=b''.join((b'\0\0\xff'*16+b'\xff\0\0'*16)for y in range(15));dib=struct.pack('<IiiHHIIiiII',40,32,15,1,24,0,len(pixels),0,0,0,0)+pixels;bitmap=struct.pack('<2sIHHI',b'BM',14+len(dib),0,0,54)+dib
 (output/'strip.bmp').write_bytes(bitmap);rc=output/'fixture.rc';rc.write_text('101 BITMAP "'+str(output/'strip.bmp')+'"\n');resource=output/'fixture.res';subprocess.run(['llvm-rc','/fo','fixture.res','fixture.rc'],cwd=output,env=env,check=True);return resource
