# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Own PE resources and entry points for the USER image-resource family."""
from pathlib import Path
import struct,subprocess,shutil,sys,json
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/image-resource'
IMPORTS={
 'KERNEL32':{'ExitProcess':4,'GetModuleHandleA':4,'LoadLibraryA':4,'FreeLibrary':4,'GetLastError':0,'SetLastError':4,'CreateFileA':28,'WriteFile':20,'CloseHandle':4,'DeleteFileA':4},
 'USER32':{'LoadImageA':24,'LoadIconA':8,'LoadCursorA':8,'LoadBitmapA':8,'LoadCursorFromFileA':4,'LookupIconIdFromDirectory':8,'LookupIconIdFromDirectoryEx':20,'CreateIconFromResource':16,'CreateIconFromResourceEx':28,'DestroyIcon':4,'DestroyCursor':4,'DrawIconEx':36,'LoadBitmapW':8,'LoadCursorW':8,'LoadIconW':8,'LoadImageW':24,'LoadCursorFromFileW':4},
 'GDI32':{'CreateCompatibleDC':4,'CreateBitmap':20,'SelectObject':8,'DeleteDC':4,'DeleteObject':4,'GetPixel':12,'GetObjectA':12}}
def own_images():
 icon=struct.pack('<IiiHH6I',40,2,4,1,32,0,16,0,0,0,0)+struct.pack('<4I',*([0x00ff00]*4))+b'\0'*8
 cur=struct.pack('<IiiHH6I',40,2,4,1,1,0,8,0,0,0,0)+b'\0\0\0\0\xff\xff\xff\0'+b'\xc0\0\0\0'*2+b'\0'*8
 ico=struct.pack('<3H4B2H2I',0,1,1,2,2,0,0,1,32,len(icon),22)+icon
 cursor=struct.pack('<3H4B2H2I',0,2,1,2,2,2,0,1,1,len(cur),22)+cur
 bitmap=struct.pack('<IiiHH6I',40,2,2,1,32,0,16,0,0,0,0)+struct.pack('<4I',*([0x00ff00]*4))
 bmp=struct.pack('<2sIHHI',b'BM',14+len(bitmap),0,0,54)+bitmap
 group=struct.pack('<3H4B2HIH',0,1,1,2,2,0,0,1,32,len(icon),0x8001)
 return dict(icon=icon,ico=ico,cur=cursor,bmp=bmp,group=group,bitmap=bitmap)
def generate():
 OUT.mkdir(parents=True,exist_ok=True)
 # The fixture files have fixed names, so two tests generating at the
 # same time would otherwise write the same header twice over.
 sys.path.insert(0,str(ROOT/'tools'));from native_build import locked
 with locked(OUT/'fixture.lock'):return _generate()
def _generate():
 images=own_images()
 for name in ('ico','cur','bmp'):(OUT/f'fixture.{name}').write_bytes(images[name])
 text=['/* Own synthetic resources; no Microsoft bytes. */']
 for name,data in images.items():text.append(f'static const unsigned char image_{name}[]={{'+','.join(str(x)for x in data)+'};')
 (OUT/'resource-icon.bin').write_bytes(images['icon'])
 (OUT/'resource-cursor.bin').write_bytes(struct.pack('<HH',1,1)+images['cur'][22:])
 (OUT/'resource-bitmap.bin').write_bytes(images['bitmap'])
 (OUT/'resource-icon-group.bin').write_bytes(struct.pack('<3H4B2HIH',0,1,1,2,2,0,0,1,32,len(images['icon']),8301))
 (OUT/'resource-cursor-group.bin').write_bytes(struct.pack('<3H4HIH',0,2,1,2,4,1,1,len(images['cur'])-18,8302))
 rc=OUT/'fixture.rc';rc.write_text('8301 3 "resource-icon.bin"\n8302 1 "resource-cursor.bin"\n101 14 "resource-icon-group.bin"\nNAMED_ICON 14 "resource-icon-group.bin"\n201 12 "resource-cursor-group.bin"\nNAMED_CURSOR 12 "resource-cursor-group.bin"\n301 2 "resource-bitmap.bin"\nNAMED_BITMAP 2 "resource-bitmap.bin"\n')
 sys.path.insert(0,str(ROOT));import neo98;env=neo98.tool_environment()
 subprocess.run(['llvm-rc','/fo','fixture.res','fixture.rc'],cwd=OUT,env=env,check=True)
 subprocess.run(['lld-link','/dll','/noentry','/base:0x58000000','/timestamp:0','/machine:x86','/out:'+str(OUT/'IMAGERES.DLL'),str(OUT/'fixture.res')],env=env,check=True)
 dll=(OUT/'IMAGERES.DLL').read_bytes()
 text.append('static const unsigned char image_dll[]={'+','.join(str(x)for x in dll)+'};')
 (OUT/'fixture-images.h').write_text('\n'.join(text)+'\n')
 return images
def test():
 sys.path.insert(0,str(ROOT));import neo98
 reports={}
 for suffix,report in [('directory','directory-results.json'),('probe','probe-results.json'),('public','public-results.json'),('', 'resource-results.json'),('native','native-results.json')]:
  name='test_image_resource'+('_'+suffix if suffix else '')
  run=subprocess.run([str(neo98.analysis_python()),str(ROOT/'tools'/f'{name}.py')],capture_output=True,text=True,env=neo98.tool_environment());(OUT/f'{name}.log').write_text(run.stdout+run.stderr)
  if run.returncode:print(run.stdout+run.stderr)
  run.check_returncode();reports[name]=json.loads((OUT/report).read_text());print(name+': PASS',flush=True)
 return reports
if __name__=='__main__':generate()
