#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Own EXE and DLLs for the native loader, without a Windows CRT."""
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT));import neo98

def generate():
    out=ROOT/'build/modules';out.mkdir(exist_ok=True);env=neo98.tool_environment()
    def run(args):subprocess.run([str(a)for a in args],env=env,cwd=out,check=True)
    def library(name,exports):
        path=out/(name+'.def');path.write_text('LIBRARY '+name+'.DLL\nEXPORTS\n'+'\n'.join(exports)+'\n')
        lib=out/(name+'-import.lib');run(['llvm-dlltool','-m','i386','-k','-d',path,'-l',lib]);return lib
    kernel=library('KERNEL32',['GetModuleHandleA@4','GetModuleFileNameA@12','GetProcAddress@8','LoadLibraryA@4','FreeLibrary@4','DisableThreadLibraryCalls@4','GetLastError@0','SetLastError@4','Sleep@4','ExitProcess@4','GetVersion@0','CreateThread@24','WaitForSingleObject@8','CloseHandle@4','GetExitCodeThread@8','FindResourceA@12','FindResourceExA@16','LoadResource@8','LockResource@4','SizeofResource@8','FreeResource@4','EnumResourceTypesA@12','VirtualQuery@12'])

    user=library('USER32',['LoadStringA@16'])
    # DLL module name is MODULE.EXE, despite the import-library filename.
    definition=out/'main.def';definition.write_text('LIBRARY MODULE.EXE\nEXPORTS\nReport@12\n')
    mainlib=out/'main.lib';run(['llvm-dlltool','-m','i386','-k','-d',definition,'-l',mainlib])
    beta=library('BETA',['BetaAnswer@4']);alpha=library('ALPHA',['AlphaAnswer@4'])
    cycle_a=library('CYCLEA',['CycleAValue@4']);cycle_b=library('CYCLEB',['CycleBValue@4'])
    for index,name in ((2,'BETA'),(1,'ALPHA'),(3,'GAMMA'),(4,'FAILED'),(5,'CYCLEA'),(6,'CYCLEB'),(7,'TLS')):
        obj=out/(name+'.obj')
        run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-stack-protector','-DMODULE_ID='+str(index),'-DMODULE_NAME="'+name+'.DLL"','-c',ROOT/'tools/module_dll_fixture.c','-o',obj])
        lines=['Value=_Value@4 @17','counter DATA','worker DATA']
        if index==1:lines+=['AlphaAnswer=_Value@4']
        if index==2:lines+=['BetaAnswer=_Value@4']
        if index==5:lines+=['CycleAValue=_Value@4']
        if index==6:lines+=['CycleBValue=_Value@4']
        if index==3:lines+=['ForwardName=ALPHA.Value','ForwardOrdinal="ALPHA.#17"','ForwardMissing=NOTLOADED.Value','ForwardCycle=GAMMA.ForwardCycle']
        payload=out/(name+'-payload.bin');payload.write_bytes(bytes([index,0,98,0,0xac,0x20]))
        german=out/(name+'-german.bin');german.write_bytes(bytes([100+index,0,7,0]))
        rc=out/(name+'.rc');res=out/(name+'.res')
        rc.write_text(f'LANGUAGE 9,1\n33 RCDATA "{payload.name}"\nPAYLOAD CUSTOM "{payload.name}"\nSTRINGTABLE\nBEGIN\n7 L"Module {index} \\x20ac"\nEND\nLANGUAGE 7,1\n33 RCDATA "{german.name}"\n')
        run(['llvm-rc','/FO',res.name,rc.name])
        definition=out/(name+'-exports.def');definition.write_text('EXPORTS\n'+'\n'.join(lines)+'\n')
        run(['lld-link','/dll','/machine:x86','/entry:DllMain@12','/subsystem:windows','/nodefaultlib','/base:0x400000','/dynamicbase:no','/safeseh:no','/timestamp:0',obj,res,kernel,user,mainlib,*([beta]if index==1 else [cycle_b]if index==5 else [cycle_a]if index==6 else []),'/def:'+str(definition),'/out:'+str(out/(name+'.DLL'))])
    obj=out/'main.obj';run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-stack-protector','-c',ROOT/'tools/module_fixture.c','-o',obj])
    definition=out/'exe-exports.def';definition.write_text('EXPORTS\nReport=_Report@12\n')
    run(['lld-link','/machine:x86','/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/fixed','/safeseh:no','/timestamp:0',obj,kernel,user,alpha,'/def:'+str(definition),'/out:'+str(out/'MODULE.EXE')])
    (out/'BAD.DLL').write_bytes(b'not a PE file')
    data=bytearray((out/'GAMMA.DLL').read_bytes());nt=int.from_bytes(data[60:64],'little')
    fixed=bytearray(data);fixed[nt+22]|=1;(out/'FIXED.DLL').write_bytes(fixed)
    # A DLL that arrives through LoadLibrary keeps the TLS index its file
    # carries; the loader does not read its directory. The pointed-at bytes are
    # therefore arbitrary here on purpose.
    tls=bytearray((out/'TLS.DLL').read_bytes());tlsnt=int.from_bytes(tls[60:64],'little')
    tls[tlsnt+24+168:tlsnt+24+176]=(0x1000).to_bytes(4,'little')+(24).to_bytes(4,'little');(out/'TLS.DLL').write_bytes(tls)
    assert b'Report\0' in data
    (out/'BROKEN.DLL').write_bytes(data.replace(b'Report\0',b'ReporX\0'))
    names=['MODULE.EXE','ALPHA.DLL','BETA.DLL','GAMMA.DLL','FAILED.DLL','BAD.DLL','FIXED.DLL','TLS.DLL','BROKEN.DLL','CYCLEA.DLL','CYCLEB.DLL']
    text='// Own x86 module-lifecycle fixtures.\n'
    for i,name in enumerate(names):
        data=(out/name).read_bytes();text+=f'static const unsigned char ModuleContract{i}[]={{\n'
        text+='\n'.join(','.join(str(b)for b in data[j:j+24])+','for j in range(0,len(data),24))+'\n};\n'
    text+='static const struct { const char *name; const unsigned char *data; unsigned size; } ModuleContractFiles[]={\n'
    text+=''.join(f'{{"{name}",ModuleContract{i},sizeof ModuleContract{i}}},\n'for i,name in enumerate(names))+'};\n'
    header=ROOT/'build/generated/module_contract.h'
    if not header.exists()or header.read_text()!=text:header.write_text(text)
    return out
if __name__=='__main__':generate()
