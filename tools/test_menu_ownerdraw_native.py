#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Owner-draw menu items in an unchanged x86 guest on the native USER runtime."""
from pathlib import Path
import argparse,ast,hashlib,json,shutil,struct,subprocess,sys,zlib
from menu_ownerdraw_components import ROOT,OUT,IMPORTS

def _engine_link_extra():
    """The FEXCore provider for this runner when the host links it
    (tools/native_app.py, engine_link_extra), else nothing."""
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parent))
    import native_app as _native_app
    return [str(p) for p in _native_app.engine_link_extra()]

sys.path.insert(0,str(ROOT));import neo98

def test(source_root=ROOT):
 OUT.mkdir(exist_ok=True);env=neo98.tool_environment();libs=[]
 dlltool=shutil.which('llvm-dlltool',path=env['PATH']);assert dlltool
 for dll,functions in IMPORTS.items():
  definition=OUT/(dll+'.def');lib=OUT/(dll+'.lib')
  definition.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(f'{n}@{b}\n'for n,b in functions.items()))
  subprocess.run([dlltool,'-m','i386','-k','-d',str(definition),'-l',str(lib)],env=env,check=True);libs.append(str(lib))
 obj=OUT/'fixture.obj';exe=OUT/'MENUODRW.EXE'
 subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin',
                 '-fno-stack-protector','-DMENU_OWNERDRAW_STANDALONE','-c',
                 str(ROOT/'tools/menu_ownerdraw_fixture.c'),'-o',str(obj)],env=env,check=True)
 subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:console','/timestamp:0','/nodefaultlib',
                 '/machine:x86','/out:'+str(exe),str(obj),*libs],env=env,check=True)
 source_root=Path(source_root).resolve();source=ROOT/'tools/test_menu_ownerdraw_native.cpp'
 if source_root!=ROOT:
  (source_root/'tools').mkdir(exist_ok=True);shutil.copyfile(source,source_root/'tools'/source.name)
  source=source_root/'tools'/source.name
 tree=ast.parse((ROOT/'tools/native_app.py').read_text())
 sources=next(ast.literal_eval(n.value)for n in ast.walk(tree)
              if isinstance(n,ast.Assign)and any(isinstance(t,ast.Name)and t.id=='sources'for t in n.targets))
 if (source_root/'native-sources.json').exists():sources=json.loads((source_root/'native-sources.json').read_text())
 sources=[s for s in sources if s!='user']
 package=Path(subprocess.check_output([str(neo98.analysis_python()),'-c','import unicorn;print(unicorn.__file__)'],text=True).strip()).parent
 library=package/'lib'/('libunicorn.2.dylib'if sys.platform=='darwin'else'libunicorn.so.2')
 binary=OUT/'test-menu-ownerdraw-native'
 compiled=subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',str(source),
                          str(source_root/('src/fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'src/fiber_aarch64.S')),
                          *[str(source_root/'src'/f'{s}.cpp')for s in sources],*_engine_link_extra(), str(library),
                          '-Wl,-rpath,'+str(library.parent),'-o',str(binary)],
                         env=env,capture_output=True,text=True)
 (OUT/'native-build.log').write_text(compiled.stdout+compiled.stderr)
 if compiled.returncode:print(compiled.stdout+compiled.stderr)
 compiled.check_returncode()
 run=subprocess.run([str(binary),str(exe),str(ROOT/'build/generated/notepad-fixedsys.fnt'),str(OUT/'ownerdraw.ppm')],
                    env=env,capture_output=True,text=True)
 (OUT/'native.log').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode()
 ppm=(OUT/'ownerdraw.ppm').read_bytes();pixels=ppm.split(b'\n',3)[3]
 def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
 png=(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',800,600,8,2,0,0,0))
      +chunk(b'IDAT',zlib.compress(b''.join(b'\0'+pixels[y*2400:(y+1)*2400]for y in range(600))))+chunk(b'IEND',b''))
 (OUT/'ownerdraw.ppm.png').write_bytes(png)
 report={'status':'PASS','asan_ubsan':'PASS','guest_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
         'checks':['WM_MEASUREITEM with the original MEASUREITEMSTRUCT fields',
                   'WM_DRAWITEM rectangle matches GetMenuItemRect of the popup window and paints the surface',
                   'MF_MENUBARBREAK starts a second column at the first row',
                   'bitmap item size from the bitmap, separator width zero, check column reserved',
                   'SetMenuItemBitmaps, GetMenuCheckMarkDimensions and mouse command delivery']}
 (OUT/'native-results.json').write_text(json.dumps(report,indent=2)+'\n');return report

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=ROOT);a=p.parse_args();test(a.source_root)
