#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build the process-environment adapter and x86 fixture in an isolated copy, without generators."""
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
OUT=ROOT/'build/environment-family/native'
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
    for f in ('test_environment_native.cpp','environment_family_fixture.c'):shutil.copy2(ROOT/'tools'/f,directory/'tools'/f)
    from environment_components import KERNEL_IMPORTS
    imports={name:stack//4 for name,stack in KERNEL_IMPORTS.items()}
    adapter=directory/'src/x86_environment.cpp'
    original=adapter.read_text()
    opening=original.index('{',original.index('void Application::FreeProcessEnvironment()'))
    closing=original.index('\n}',opening)
    observed=original[:closing]+'\n    ::EnvironmentObserve(this,true);'+original[closing:]
    observed=observed[:opening+1]+'\n    ::EnvironmentObserve(this,false);'+observed[opening+1:]
    (directory/'src/x86_environment_test.inc').write_text(observed)
    definition=OUT/'kernel.def';definition.write_text('LIBRARY KERNEL32.DLL\nEXPORTS\n'+''.join(f'{name}@{count*4}\n' for name,count in imports.items()))
    subprocess.run([dlltool,'-m','i386','-k','-d',str(definition),'-l',str(OUT/'kernel.lib')],check=True)
    subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-DENVIRONMENT_STANDALONE','-c',str(ROOT/'tools/environment_family_fixture.c'),'-o',str(OUT/'environment.obj')],check=True)
    subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x86','/out:'+str(OUT/'ENVIRONMENT.EXE'),str(OUT/'environment.obj'),str(OUT/'kernel.lib')],check=True)
    tree=ast.parse((directory/'tools/native_app.py').read_text())
    assignment=next(n for n in ast.walk(tree) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='sources' for t in n.targets))
    sources=[s for s in dict.fromkeys(ast.literal_eval(assignment.value)) if s!='x86_environment']
    pkg=Path(unicorn.__file__).resolve().parent;lib=pkg/'lib'/('libunicorn.2.dylib' if sys.platform=='darwin' else 'libunicorn.so.2')
    command=['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(directory/'tools/test_environment_native.cpp'),str(directory/'src/fiber_aarch64.S'),*[str(directory/'src'/f'{name}.cpp') for name in sources],*_engine_link_extra(), str(lib),'-Wl,-rpath,'+str(lib.parent),'-o',str(OUT/'test-environment-native')]
    run=subprocess.run(command,capture_output=True,text=True);(OUT/'build.log').write_text(run.stdout+run.stderr)
    if run.returncode:print(run.stdout+run.stderr)
    run.check_returncode()
    env=os.environ.copy();env['UBSAN_OPTIONS']='halt_on_error=1'
    run=subprocess.run([str(OUT/'test-environment-native'),str(OUT/'ENVIRONMENT.EXE'),str(ROOT/'build/generated/notepad-fixedsys.fnt')],capture_output=True,text=True,env=env)
    (OUT/'test.log').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode()
    (OUT/'results.json').write_text(json.dumps({'asan_ubsan':'pass','native_checks':['actual backing commit failure/rollback/retry','stable live block','CRT initial copy independence','shared path lookup','host expansion','heap allocation failure','public write probe','three worker threads','actual destructor page release'],'guest_fixture':'environment_family_fixture.c','guest_sha256':hashlib.sha256((OUT/'ENVIRONMENT.EXE').read_bytes()).hexdigest(),'adapter_sha256':hashlib.sha256(original.encode()).hexdigest(),'shared_generators':False},indent=2)+'\n')
if __name__=='__main__':main()
