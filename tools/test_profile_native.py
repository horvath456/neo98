#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Exercise the profile family through real x86 calls and shared native Files."""
from pathlib import Path
import argparse,ast,json,shutil,subprocess,sys,hashlib

def _engine_link_extra():
    """The FEXCore provider for this runner when the host links it
    (tools/native_app.py, engine_link_extra), else nothing."""
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parent))
    import native_app as _native_app
    return [str(p) for p in _native_app.engine_link_extra()]

ROOT=Path(__file__).resolve().parents[4] if 'runtime' in str(Path(__file__).parent) else Path(__file__).resolve().parents[1];OUT=ROOT/'build/profile-family';sys.path.insert(0,str(ROOT));import neo98
IMPORTS={'KERNEL32': {'GetModuleHandleA':4,'GetProcAddress':8,'ExitProcess': 4, 'GetLastError': 0, 'SetLastError': 4, 'GetProfileIntA': 12, 'GetPrivateProfileIntA': 16, 'GetProfileStringA': 20, 'GetPrivateProfileStringA': 24, 'GetProfileSectionA': 12, 'GetPrivateProfileSectionA': 16, 'GetPrivateProfileSectionNamesA': 12, 'WriteProfileStringA': 12, 'WritePrivateProfileStringA': 16, 'WriteProfileSectionA': 8, 'WritePrivateProfileSectionA': 12, 'GetPrivateProfileStructA': 20, 'WritePrivateProfileStructA': 20, 'GetPrivateProfileStringW': 24, 'CreateFileA': 28, 'ReadFile': 20, 'CloseHandle': 4}}
def test(source_root=ROOT):
 source_root=Path(source_root).resolve();OUT.mkdir(exist_ok=True);env=neo98.tool_environment();libs=[];dlltool=shutil.which('llvm-dlltool',path=env['PATH']);assert dlltool
 for dll,names in IMPORTS.items():
  definition=OUT/(dll+'.def');lib=OUT/(dll+'.lib');definition.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(f'{n}@{b}\n'for n,b in names.items()));subprocess.run([dlltool,'-m','i386','-k','-d',str(definition),'-l',str(lib)],env=env,check=True);libs.append(str(lib))
 folder=Path(__file__).parent;obj=OUT/'fixture.obj';exe=OUT/'PROFILE.EXE';subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-c',str(folder/'profile_fixture.c'),'-o',str(obj)],env=env,check=True);subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:console','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),str(obj),*libs],env=env,check=True)
 tree=ast.parse((ROOT/'tools/native_app.py').read_text());sources=next(ast.literal_eval(n.value)for n in ast.walk(tree)if isinstance(n,ast.Assign)and any(isinstance(t,ast.Name)and t.id=='sources'for t in n.targets));sources += [s for s in ['x86_profile','profile_files'] if s not in sources];package=Path(subprocess.check_output([str(neo98.analysis_python()),'-c','import unicorn;print(unicorn.__file__)'],text=True).strip()).parent;library=package/'lib'/('libunicorn.2.dylib'if sys.platform=='darwin'else'libunicorn.so.2');binary=OUT/'test-profile-native'
 build=subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',str(folder/'test_profile_native.cpp'),str(source_root/('src/fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'src/fiber_aarch64.S')),*[str(source_root/'src'/f'{s}.cpp')for s in sources],*_engine_link_extra(), str(library),'-Wl,-rpath,'+str(library.parent),'-o',str(binary)],env=env,capture_output=True,text=True);(OUT/'fixture-build.log').write_text(build.stdout+build.stderr);build.check_returncode();run=subprocess.run([str(binary),str(exe),str(ROOT/'build/generated/notepad-fixedsys.fnt')],env=env,capture_output=True,text=True);(OUT/'fixture-native.log').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode();report={'status':'PASS','sanitizers':['address','undefined'],'guest_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'checks':['13 ANSI profile exports via x86 stdcall','shared Files/cache persistence across two Application instances','case-folded lookups and quote trimming','32-bit integer overflow and caller default','key and section enumeration with truncation','struct encoding and partial-output checksum failure','key/section deletion with retained comments','default WIN.INI and private relative path routing','13 original W gates plus Open/CloseProfileUserMapping error120','selector-sized mutation rejection without buffer changes','public high-WORD size rejection error87']};(OUT/'fixture-results.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=ROOT);a=p.parse_args();test(a.source_root)
