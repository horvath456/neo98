#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build clipboard integration in an isolated source copy; no shared generators."""
from pathlib import Path
import argparse,ast,json,os,re,shutil,subprocess,sys
import unicorn
from clipboard_components import IMPORTS

def _engine_link_extra():
    """The FEXCore provider for this runner when the host links it
    (tools/native_app.py, engine_link_extra), else nothing."""
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parent))
    import native_app as _native_app
    return [str(p) for p in _native_app.engine_link_extra()]

ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/clipboard-family/native'
def main():
    p=argparse.ArgumentParser();p.add_argument('--runtime',type=Path);p.add_argument('--patch',type=Path);a=p.parse_args();OUT.mkdir(parents=True,exist_ok=True)
    if a.runtime:runtime=a.runtime.resolve()
    else:
        runtime=OUT/'runtime'
        if runtime.exists():shutil.rmtree(runtime)
        runtime.mkdir();shutil.copytree(ROOT/'src',runtime/'src',ignore=shutil.ignore_patterns('*.o','*.d','*.img','*.elf','*.map','*.lst'))
        for n in ('build','third_party','sdk'):(runtime/n).symlink_to(ROOT/n,target_is_directory=True)
        if a.patch:
            subprocess.run(['git','init','-q'],cwd=runtime,check=True)
            strip=2 if 'b/neo98/src/' in a.patch.read_text() else 1
            subprocess.run(['git','apply',f'-p{strip}',str(a.patch.resolve())],cwd=runtime,check=True)
    (runtime/'tools').mkdir(exist_ok=True);shutil.copy2(ROOT/'tools/test_clipboard_native.cpp',runtime/'tools/test_clipboard_native.cpp')
    environment=os.environ.copy();environment['PATH']='/opt/homebrew/opt/llvm/bin:'+environment.get('PATH','');environment['UBSAN_OPTIONS']='halt_on_error=1'
    libs=[]
    for dll,items in IMPORTS.items():
        definition=OUT/(dll+'.def');definition.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+'\n'.join(f'{n}@{c}' for n,c in items.items())+'\n')
        lib=definition.with_suffix('.lib');libs.append(lib);subprocess.run(['llvm-dlltool','-m','i386','-k','-d',str(definition),'-l',str(lib)],env=environment,check=True)
    entry=OUT/'entry.c';entry.write_text('__declspec(dllimport) void __stdcall ExitProcess(unsigned); int run_clipboard_test(void);void mainCRTStartup(void){ExitProcess(run_clipboard_test());}\n')
    objs=[]
    for source in (entry,ROOT/'tools/clipboard_fixture.c'):
        obj=OUT/(source.stem+'.obj');objs.append(obj)
        subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-stack-protector','-c',str(source),'-o',str(obj)],env=environment,check=True)
    exe=OUT/'CLIPBOARD.EXE';subprocess.run(['lld-link','/machine:x86','/entry:mainCRTStartup','/subsystem:console,4.0','/nodefaultlib','/fixed','/safeseh:no','/timestamp:0',*[str(x)for x in objs+libs],'/out:'+str(exe)],env=environment,check=True)
    tree=ast.parse((ROOT/'tools/native_app.py').read_text());assignment=next(n for n in ast.walk(tree) if isinstance(n,ast.Assign)and any(isinstance(t,ast.Name)and t.id=='sources'for t in n.targets))
    sources=[s for s in dict.fromkeys(ast.literal_eval(assignment.value))if s!='x86_clipboard' and (runtime/'src'/f'{s}.cpp').exists()]
    package=Path(unicorn.__file__).resolve().parent;lib=package/'lib'/('libunicorn.2.dylib' if sys.platform=='darwin' else 'libunicorn.so.2');binary=OUT/'clipboard-native-asan'
    command=['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(runtime/'tools/test_clipboard_native.cpp'),str(runtime/('src/fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'src/fiber_aarch64.S')),*[str(runtime/'src'/f'{s}.cpp')for s in sources],*_engine_link_extra(), str(lib),'-Wl,-rpath,'+str(lib.parent),'-o',str(binary)]
    r=subprocess.run(command,env=environment,capture_output=True,text=True);(OUT/'build.log').write_text(r.stdout+r.stderr)
    if r.returncode:print(r.stdout+r.stderr)
    r.check_returncode();r=subprocess.run([str(binary),str(exe),str(ROOT/'build/generated/notepad-fixedsys.fnt')],env=environment,capture_output=True,text=True);(OUT/'test.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
    (OUT/'results.json').write_text(json.dumps({'status':'PASS','ASan_UBSan':'PASS','guest_fixture':'clipboard_fixture.c','applications':4,'shared_generators':False},indent=2)+'\n')
if __name__=='__main__':main()
