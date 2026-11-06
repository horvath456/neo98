#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the Device Manager SERIALUI page provider on private drive copies."""
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: compares the FEX and Unicorn engines of macOS')  # macOS only; skipped elsewhere
        _sys.exit(77)
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(1, str(Path(__file__).resolve().parent))
import neo98
from test_release_smoke import run, ROOT


def build(case):
 env=neo98.tool_environment();drive=case/'drive-d';drive.mkdir(exist_ok=True)
 kernel={'ExitProcess':1,'LoadLibrary16':1,'GetProcAddress16':2,'FreeLibrary16':1,'QT_Thunk':0,'MapLS':1,
         'UnMapLS':1,'GetDefaultCommConfigA':3,'GetModuleHandleA':1}
 user={'RegisterClassA':1,'CreateWindowExA':12,'DefWindowProcA':4,'FindWindowA':2,'FindWindowExA':4,
       'GetDlgItem':2,'SendMessageA':4,'SetTimer':4}
 ordinals={'LoadLibrary16':35,'FreeLibrary16':36,'GetProcAddress16':37}
 (case/'enum_imports.h').write_text('typedef unsigned U;\n'+''.join(
   '__declspec(dllimport) U __stdcall '+n+'('+(','.join('U' for _ in range(c)) or 'void')+');\n'
   for n,c in {**kernel,'PropertySheetA':1}.items()))
 libraries=[]
 for dll,entries in {'KERNEL32':kernel,'COMCTL32':{'PropertySheetA':1},'USER32':user,
                     'ADVAPI32':{'RegOpenKeyExA':5,'RegEnumKeyExA':8,'RegQueryValueExA':6,'RegSetValueExA':6,'RegCloseKey':1}}.items():
  definition,library=case/f'{dll}.def',case/f'{dll}.lib'
  definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n'+''.join(
    f'{n}@{c*4}'+(f' @{ordinals[n]} NONAME' if n in ordinals else '')+'\n' for n,c in entries.items()))
  subprocess.run(['llvm-dlltool','-m','i386','-k','-d',str(definition),'-l',str(library)],env=env,check=True)
  libraries.append(str(library))
 obj=case/'enum.obj'
 subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector',
                 '-I'+str(case),'-c',str(ROOT/'tools/state_serialui_enum_fixture.c'),'-o',str(obj)],env=env,check=True)
 subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:windows','/nodefaultlib','/machine:x86','/timestamp:0',
                 '/out:'+str(drive/'ENUMSTATE.EXE'),str(obj),*libraries],env=env,check=True)
 return drive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    args = parser.parse_args()
    out = ROOT / 'build/release-serialui-enum'
    out.mkdir(parents=True, exist_ok=True)
    fixture = build(out)
    for engine in ('fex', 'unicorn'):
        case = out / engine
        case.mkdir(exist_ok=True)
        drive = case / 'c'
        if drive.exists(): raise RuntimeError('Private drive already exists')
        subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R',str(args.source.resolve()),str(drive)],check=True)
        try:
            env = dict(os.environ, NEO98_CPU=engine, SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy',
                NEO98_SETTINGS_DIR=str(case/'settings'), NEO98_LOG_TO_FILE='0', NEO98_HOST_DEADLINE='60',
                NEO98_HOST_INPUT='0:wait=|Enumerated COM1 Properties;1:idle=800;2:button=Cancel;3:idle=800;4:button=OK')
            text = run([ROOT/'build/native-app/neo98-host','-',r'C:\WINDOWS\FONTS\VGAFIX.FON',
                '--quiet','--offline','--system-tree',drive,'--drive-c',drive,'--no-registry-file',
                '--native-module','COMCTL32.DLL','--drive-readonly','D='+str(fixture),
                '--exe',r'D:\ENUMSTATE.EXE'],env,case/'host.log')
            assert 'NEO98 HOST exited code=0' in text, text[-1200:]
            print('PASS '+engine+': enumerated Port Settings, Cancel preserves DCB, Apply saves baud rate',flush=True)
        finally: shutil.rmtree(drive)


if __name__ == '__main__': main()
