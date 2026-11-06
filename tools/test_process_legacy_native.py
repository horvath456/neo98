#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Exercise original legacy adapters through real parent and child processes."""
from pathlib import Path
import argparse,ast,hashlib,json,shutil,subprocess,sys

def _engine_link_extra():
    """The FEXCore provider for this runner when the host links it
    (tools/native_app.py, engine_link_extra), else nothing."""
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parent))
    import native_app as _native_app
    return [str(p) for p in _native_app.engine_link_extra()]

SOURCE=Path(__file__).resolve().parents[1];ROOT=Path(__file__).resolve().parents[4] if 'runtime' in str(Path(__file__).parent) else SOURCE;OUT=ROOT/'build/process-legacy'
IMPORTS={'KERNEL32': {'WinExec': 8, 'LoadModule': 8, 'GetStartupInfoA': 4, 'GetLastError': 0, 'SetLastError': 4, 'ExitProcess': 4, 'GetCommandLineA': 0, 'GetModuleHandleA': 4, 'GetEnvironmentVariableA': 12, 'SetEnvironmentVariableA': 8, 'GetCurrentDirectoryA': 8, 'SetCurrentDirectoryA': 4, 'Sleep': 4, 'CreateFileA': 28, 'WriteFile': 20, 'ReadFile': 20, 'CloseHandle': 4, 'GetFileAttributesA': 4}, 'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16, 'GetMessageA': 16, 'DispatchMessageA': 4, 'FindWindowA': 8, 'PostMessageA': 16}}
def test(source_root=SOURCE):
 sys.path.insert(0,str(ROOT));import neo98
 source_root=Path(source_root).resolve();OUT.mkdir(exist_ok=True);env=neo98.tool_environment();libs=[];dlltool=shutil.which('llvm-dlltool',path=env['PATH']);assert dlltool
 for dll,names in IMPORTS.items():
  d=OUT/(dll+'.def');lib=OUT/(dll+'.lib');d.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(n+'@'+str(b)+(' @'+n[1:]+' NONAME' if n.startswith('O')and n[1:].isdigit() else '')+'\n'for n,b in names.items()));subprocess.run([dlltool,'-m','i386','-k','-d',str(d),'-l',str(lib)],env=env,check=True);libs.append(str(lib))
 folder=source_root/'tools';obj=OUT/'fixture.obj';exe=OUT/'LEGACY.EXE';subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-c',str(folder/'process_legacy_fixture.c'),'-o',str(obj)],env=env,check=True);subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:console','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),str(obj),*libs],env=env,check=True)
 tree=ast.parse((source_root/'tools/native_app.py').read_text());sources=next(ast.literal_eval(n.value)for n in ast.walk(tree)if isinstance(n,ast.Assign)and any(isinstance(t,ast.Name)and t.id=='sources'for t in n.targets));package=Path(subprocess.check_output([str(neo98.analysis_python()),'-c','import unicorn;print(unicorn.__file__)'],text=True).strip()).parent;library=package/'lib'/('libunicorn.2.dylib'if sys.platform=='darwin'else'libunicorn.so.2');binary=OUT/'test-process-legacy-native'
 command=['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',str(folder/'test_process_legacy_native.cpp'),str(source_root/('src/fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'src/fiber_aarch64.S')),*[str(source_root/'src'/f'{s}.cpp')for s in sources],*_engine_link_extra(), str(library),'-Wl,-rpath,'+str(library.parent),'-o',str(binary)]
 inputs=[source_root/('src/fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'src/fiber_aarch64.S'),folder/'test_process_legacy_native.cpp',folder/'process_legacy_fixture.c',source_root/'tools/native_app.py',*list((source_root/'src').glob('*.h')),*list((source_root/'src').glob('*.inc')),*[source_root/'src'/f'{s}.cpp'for s in sources],*list((source_root/'build/generated').glob('*.h')),source_root/'build/crt-locale/crt_locale_data.h',source_root/'build/image-resource/system_assets.h',*list((source_root/'third_party/unicorn/include').rglob('*.h'))]
 def hashes():return {str(p.relative_to(source_root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(set(inputs))}
 assert all(not p.is_symlink()for p in inputs);before=hashes();(OUT/'snapshot-before.json').write_text(json.dumps(before,indent=2)+'\n')
 build=subprocess.run(command,env=env,capture_output=True,text=True);(OUT/'fixture-build.log').write_text(build.stdout+build.stderr);build.check_returncode();after=hashes();assert before==after;(OUT/'snapshot-after.json').write_text(json.dumps(after,indent=2)+'\n')
 run=subprocess.run([str(binary),str(exe),str(ROOT/'build/generated/notepad-fixedsys.fnt')],env=env,capture_output=True,text=True);(OUT/'fixture-native.log').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode();report={'status':'PASS','sanitizers':['address','undefined'],'guest_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'source_inputs':len(before),'source_hashes_unchanged':True,'checks':['real WinExec child reaches a blocked GetMessage before parent resumes','original legacy failure mapping and native invalid spans','LoadModule creates children and closes both real handles','copied command, environment and show data','zero-length legacy tail uses executable command fallback','GetStartupInfoA returns actual process startup record','all children and closed legacy handle records cleaned up']};(OUT/'fixture-results.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=SOURCE);test(p.parse_args().source_root)
