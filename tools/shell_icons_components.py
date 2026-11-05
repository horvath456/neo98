# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build independent icon extraction fixtures and run the family comparisons."""
import json
from pathlib import Path
import struct
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/shell-icons'
IMPORTS={
    'SHELL32':{'ExtractIconA':12,'ExtractIconExA':20,'ExtractIconEx':20,'ExtractIconW':12,'InternalExtractIconListA':12,'InternalExtractIconListW':12,'ExtractIconExW':20,'ExtractAssociatedIconExW':16},
    'KERNEL32':{'ExitProcess':4,'CreateFileA':28,'WriteFile':20,'CloseHandle':4,'DeleteFileA':4,'SetEnvironmentVariableA':8,'SetLastError':4,'GetLastError':0},
    'USER32':{'DestroyIcon':4,'DrawIconEx':36},
    'GDI32':{'CreateCompatibleDC':4,'CreateBitmap':20,'SelectObject':8,'DeleteDC':4,'DeleteObject':4,'GetPixel':12}
}
def image(color):
    # DIB rows are bottom-up; every corner and all three color channels differ.
    pixels=(color^0x112233,color^0x0000ff,color,color^0x00ff00)
    return struct.pack('<IiiHH6I',40,2,4,1,32,0,16,0,0,0,0)+struct.pack('<4I',*pixels)+b'\0'*8
def make_files():
    def w(data,p,v):struct.pack_into('<H',data,p,v)
    def d(data,p,v):struct.pack_into('<I',data,p,v)
    def group(i):return struct.pack('<3H4B2HIH',0,1,1,2,2,0,0,1,32,64,i)
    groups=[(100,group(10)),(101,group(11))];icons=[(10,image(0x124ab7)),(11,image(0xe7812a))]
    pe=bytearray(8192);w(pe,0,0x5a4d);d(pe,0x3c,0x80);d(pe,0x80,0x4550);w(pe,0x86,1);w(pe,0x94,0xe0);d(pe,0x108,0x1000)
    d(pe,0x180,0x1e00);d(pe,0x184,0x1000);d(pe,0x188,0x1e00);d(pe,0x18c,0x200);w(pe,0x20e,2);cursor=0x220
    for ti,(kind,items) in enumerate([(3,icons),(14,groups)]):
        directory=cursor;cursor+=16+len(items)*8;d(pe,0x210+ti*8,kind);d(pe,0x214+ti*8,0x80000000|directory-0x200);w(pe,directory+14,len(items))
        for i,(name,payload) in enumerate(items):
            lang=cursor;entry=cursor+24;bits=cursor+40;cursor=(bits+len(payload)+7)&~7
            d(pe,directory+16+i*8,name);d(pe,directory+20+i*8,0x80000000|lang-0x200)
            w(pe,lang+14,1);d(pe,lang+16,0x409);d(pe,lang+20,entry-0x200)
            d(pe,entry,bits-0x200+0x1000);d(pe,entry+4,len(payload));pe[bits:bits+len(payload)]=payload
    ne=bytearray(8192);w(ne,0,0x5a4d);d(ne,0x3c,0x80);w(ne,0x80,0x454e);w(ne,0xa4,0x40);w(ne,0xa6,0xc0);ne[0xb6]=2;w(ne,0xbe,0x300);w(ne,0xc0,4)
    cursor=0xc2;bits=0x1000
    for kind,items in [(14,groups),(3,icons)]:
        w(ne,cursor,kind|0x8000);w(ne,cursor+2,len(items));cursor+=8
        for name,payload in items:
            size=(len(payload)+15)&~15;w(ne,cursor,bits>>4);w(ne,cursor+2,size>>4);w(ne,cursor+6,name|0x8000)
            ne[bits:bits+len(payload)]=payload;bits+=size;cursor+=12
    ico=struct.pack('<3H4B2H2I',0,1,1,2,2,0,0,1,32,64,22)+image(0x124ab7)
    return {'pe':bytes(pe),'ne':bytes(ne),'ico':ico}
def generate():
    OUT.mkdir(parents=True,exist_ok=True)
    text=['/* Generated own synthetic image resources; no original binary bytes. */']
    for name,data in make_files().items():
        text.append(f'static const unsigned char shell_{name}[{len(data)}] = {{')
        text.extend(f'[{i}]=0x{b:02x},' for i,b in enumerate(data) if b)
        text.append('};')
    (OUT/'fixture-images.h').write_text('\n'.join(text)+'\n')
def test():
    sys.path.insert(0,str(ROOT));import neo98
    reports={}
    for name,report in [('shell_icons','results.json'),('shell_icons_native','native/results.json')]:
        run=subprocess.run([str(neo98.analysis_python()),str(ROOT/'tools'/f'test_{name}.py')],capture_output=True,text=True,env=neo98.tool_environment())
        (OUT/f'component-{name}.log').write_text(run.stdout+run.stderr)
        if run.returncode:print(run.stdout+run.stderr)
        run.check_returncode();reports[name]=json.loads((OUT/report).read_text());print(name+': PASS',flush=True)
    return reports
if __name__=='__main__':generate()
