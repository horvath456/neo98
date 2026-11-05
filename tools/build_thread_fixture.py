#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build our x86 thread/event contract test as a PE executable."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT));import neo98 as build

def generate():
    out=ROOT/'build/native-app';out.mkdir(exist_ok=True);env=build.tool_environment()
    functions={'ExitProcess':4,'CreateThread':24,'CreateEventA':16,'SetEvent':4,'ResetEvent':4,'CloseHandle':4,
        'WaitForSingleObject':8,'ResumeThread':4,'GetExitCodeThread':8,'GetCurrentThreadId':0,'GetLastError':0,
        'SetLastError':4,'Sleep':4,'GetTickCount':0}
    definition=out/'thread-kernel32.def';definition.write_text('LIBRARY KERNEL32.DLL\nEXPORTS\n'+''.join(f'{name}@{size}\n' for name,size in functions.items()))
    commands=[['llvm-dlltool','-m','i386','-k','-d',definition,'-l',out/'thread-kernel32.lib'],
        ['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-stack-protector','-c',ROOT/'tools/thread_fixture.c','-o',out/'thread-fixture.obj'],
        ['lld-link','/machine:x86','/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/fixed','/safeseh:no','/timestamp:0',
         out/'thread-fixture.obj',out/'thread-kernel32.lib','/out:'+str(out/'THREADS.EXE')]]
    for command in commands:subprocess.run([str(a) for a in command],env=env,check=True)
    data=(out/'THREADS.EXE').read_bytes();text='// Own x86 test program, not a Windows binary.\nstatic const unsigned char ThreadContractProgram[] = {\n'
    text+='\n'.join(','.join(str(b) for b in data[i:i+24])+',' for i in range(0,len(data),24))+'\n};\n'
    generated=ROOT/'build/generated';generated.mkdir(exist_ok=True);header=generated/'thread_contract.h'
    if not header.exists() or header.read_text()!=text:header.write_text(text)
    report={'source_sha256':hashlib.sha256((ROOT/'tools/thread_fixture.c').read_bytes()).hexdigest(),'exe_sha256':hashlib.sha256(data).hexdigest(),'exe_bytes':len(data)}
    (generated/'thread-contract-provenance.json').write_text(json.dumps(report,indent=2)+'\n')
    return out/'THREADS.EXE'
if __name__=='__main__':generate()
