#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build the shell registry adapter and x86 fixture in an isolated copy, without generators."""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unicorn

def _engine_link_extra():
    """The FEXCore provider for this runner when the host links it
    (tools/native_app.py, engine_link_extra), else nothing."""
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parent))
    import native_app as _native_app
    return [str(p) for p in _native_app.engine_link_extra()]

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/shell-registry/native'
sys.path.insert(0,str(ROOT))
import neo98
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--patch',type=Path);args=parser.parse_args()
    environment=neo98.tool_environment()
    os.environ['PATH']=environment['PATH']
    dlltool=shutil.which('llvm-dlltool',path=environment['PATH'])
    if not dlltool:raise RuntimeError('Missing llvm-dlltool on the configured tool PATH')
    OUT.mkdir(parents=True,exist_ok=True);directory=OUT/'runtime'
    if directory.exists():shutil.rmtree(directory)
    directory.mkdir();shutil.copytree(ROOT/'src',directory/'src',ignore=shutil.ignore_patterns('*.o','*.d','*.img','*.elf','*.map','*.lst'))
    (directory/'tools').mkdir();shutil.copy2(ROOT/'tools/native_app.py',directory/'tools/native_app.py')
    for name in ('build','third_party'):(directory/name).symlink_to(ROOT/name,target_is_directory=True)
    if args.patch:
        subprocess.run(['git','init','-q'],cwd=directory,check=True)
        subprocess.run(['git','apply','-p2',str(args.patch.resolve())],cwd=directory,check=True)
    for f in ('test_shell_registry_native.cpp','shell_registry_fixture.c','fixture_native_modules.h'):shutil.copy2(ROOT/'tools'/f,directory/'tools'/f)
    from shell_registry_components import KERNEL_IMPORTS, SHELL_IMPORTS, ADVAPI_IMPORTS
    adapter=directory/'src/x86_shell_registry.cpp';original=adapter.read_text()
    libraries=[]
    for module,imports in [('KERNEL32',KERNEL_IMPORTS),('SHLWAPI',SHELL_IMPORTS),('ADVAPI32',ADVAPI_IMPORTS)]:
        definition=OUT/(module+'.def');definition.write_text('LIBRARY '+module+'.DLL\nEXPORTS\n'+''.join(f'{name}@{stack}\n'for name,stack in imports.items()))
        library=OUT/(module+'.lib');libraries.append(str(library))
        subprocess.run([dlltool,'-m','i386','-k','-d',str(definition),'-l',str(library)],check=True)
    subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-DSHELL_REGISTRY_STANDALONE','-c',str(ROOT/'tools/shell_registry_fixture.c'),'-o',str(OUT/'shell-registry.obj')],check=True)
    subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x86','/out:'+str(OUT/'SHELLREG.EXE'),str(OUT/'shell-registry.obj'),*libraries],check=True)
    tree=ast.parse((directory/'tools/native_app.py').read_text())
    assignment=next(n for n in ast.walk(tree) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='sources' for t in n.targets))
    sources=[s for s in dict.fromkeys(ast.literal_eval(assignment.value)) if s!='x86_shell_registry']
    pkg=Path(unicorn.__file__).resolve().parent;lib=pkg/'lib'/('libunicorn.2.dylib' if sys.platform=='darwin' else 'libunicorn.so.2')
    command=['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(directory/'tools/test_shell_registry_native.cpp'),str(directory/'src/fiber_aarch64.S'),*[str(directory/'src'/f'{name}.cpp') for name in sources],*_engine_link_extra(), str(lib),'-Wl,-rpath,'+str(lib.parent),'-o',str(OUT/'test-shell-registry-native')]
    run=subprocess.run(command,capture_output=True,text=True);(OUT/'build.log').write_text(run.stdout+run.stderr)
    if run.returncode:print(run.stdout+run.stderr)
    run.check_returncode()
    env=os.environ.copy();env['UBSAN_OPTIONS']='halt_on_error=1'
    run=subprocess.run([str(OUT/'test-shell-registry-native'),str(OUT/'SHELLREG.EXE'),str(ROOT/'build/generated/notepad-fixedsys.fnt')],capture_output=True,text=True,env=env)
    (OUT/'test.log').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode()
    (OUT/'results.json').write_text(json.dumps({'asan_ubsan':'pass','native_checks':['CU/LM views and caller defaults','actual RAM metadata/enumeration','process expansion','160 HUSKEY close/reuse cycles','128-handle exhaustion rollback','guest heap allocation failure','host allocation failure','preallocation expansion span limit'],'guest_fixture':'shell_registry_fixture.c','guest_sha256':hashlib.sha256((OUT/'SHELLREG.EXE').read_bytes()).hexdigest(),'adapter_sha256':hashlib.sha256(original.encode()).hexdigest(),'shared_generators':False},indent=2)+'\n')
if __name__=='__main__':main()
