#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Original class algorithms through Application, USER and real x86 calls."""
from pathlib import Path
import argparse,ast,hashlib,json,re,shutil,subprocess,sys

def _engine_link_extra():
    """The FEXCore provider for this runner when the host links it
    (tools/native_app.py, engine_link_extra), else nothing."""
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parent))
    import native_app as _native_app
    return [str(p) for p in _native_app.engine_link_extra()]

ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/"build/toolbar-layout"
IMPORTS={"KERNEL32":{"ExitProcess":4,"GetModuleHandleA":4,"GetLastError":0},"USER32":{"RegisterClassA":4,"UnregisterClassA":8,"CreateWindowExA":48,"DestroyWindow":4,"DefWindowProcA":16,"SendMessageA":16,"GetWindowLongA":8},"COMCTL32":{"InitCommonControls":0},"GDI32":{"GetObjectA":12}}
sys.path.insert(0,str(ROOT));import neo98

def test(source_root=ROOT):
 OUT.mkdir(exist_ok=True);env=neo98.tool_environment();libs=[];dlltool=shutil.which('llvm-dlltool',path=env['PATH']);assert dlltool
 for dll,functions in IMPORTS.items():
  definition=OUT/(dll+'.def');lib=OUT/(dll+'.lib');definition.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(f'{n}@{b}\n'for n,b in functions.items()));subprocess.run([dlltool,'-m','i386','-k','-d',str(definition),'-l',str(lib)],env=env,check=True);libs.append(str(lib))
 obj=OUT/'fixture.obj';exe=OUT/'TOOLBAR-LAYOUT.EXE';subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-DTOOLBAR_LAYOUT_STANDALONE','-c',str(ROOT/'tools/toolbar_layout_fixture.c'),'-o',str(obj)],env=env,check=True);subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:console','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),str(obj),*libs],env=env,check=True)
 source_root=Path(source_root).resolve();source=ROOT/'tools/test_toolbar_layout_native.cpp'
 if source_root!=ROOT:
  (source_root/'tools').mkdir(exist_ok=True);shutil.copyfile(source,source_root/'tools'/source.name);source=source_root/'tools'/source.name
 tree=ast.parse((ROOT/'tools/native_app.py').read_text());sources=next(ast.literal_eval(n.value)for n in ast.walk(tree)if isinstance(n,ast.Assign)and any(isinstance(t,ast.Name)and t.id=='sources'for t in n.targets))
 if (source_root/'native-sources.json').exists():sources=json.loads((source_root/'native-sources.json').read_text())
 # Native runner links canonical USER as a separate translation unit.
 package=Path(subprocess.check_output([str(neo98.analysis_python()),'-c','import unicorn;print(unicorn.__file__)'],text=True).strip()).parent;library=package/'lib'/('libunicorn.2.dylib'if sys.platform=='darwin'else'libunicorn.so.2');binary=OUT/'test-toolbar-layout-native'
 compiled=subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',str(source),str(source_root/'src/fiber_aarch64.S'),*[str(source_root/'src'/f'{s}.cpp')for s in sources],*_engine_link_extra(), str(library),'-Wl,-rpath,'+str(library.parent),'-o',str(binary)],env=env,capture_output=True,text=True);(OUT/'native-build.log').write_text(compiled.stdout+compiled.stderr)
 if compiled.returncode:print(compiled.stdout+compiled.stderr)
 compiled.check_returncode();run=subprocess.run([str(binary),str(exe),str(ROOT/'build/generated/notepad-fixedsys.fnt')],env=env,capture_output=True,text=True);(OUT/'native.log').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode();report={'status':'PASS','asan_ubsan':'PASS','guest_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'runner_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'api_calls':int(re.search(r'GUEST PASS calls=(\d+)',run.stdout)[1]),'callbacks':int(re.search(r'callbacks=(\d+)',run.stdout)[1]),'checks':['original button dimensions and lazy item rectangles through x86 imports','wrapped separators and hidden-button geometry','real monochrome GDI backing growth, reuse and destruction','actual GDI object exhaustion, preserved failed resize and free/retry']};(OUT/'native-results.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=ROOT);a=p.parse_args();test(a.source_root)
