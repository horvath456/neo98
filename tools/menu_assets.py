#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Convert local USER.EXE system-menu resources to the native PE menu parser format."""
from pathlib import Path
import hashlib
import json
import struct
ROOT=Path(__file__).resolve().parents[1]
EXPECTED='18303935de5e88482e9630a06480a502c1b9d6281c4013c2dd1ca88086acf192'
def convert(data):
    version,offset=struct.unpack_from('<HH',data,0)
    if version:raise ValueError('Unexpected original menu version')
    position=4+offset;output=bytearray(4)
    def items(depth=0):
        nonlocal position
        if depth>16:raise ValueError('Menu nesting')
        while True:
            flags=struct.unpack_from('<H',data,position)[0];position+=2
            output.extend(struct.pack('<H',flags))
            if not flags&16:
                output.extend(data[position:position+2]);position+=2
            end=data.index(0,position);label=data[position:end].decode('cp1252');position=end+1
            output.extend(label.encode('utf-16le')+b'\0\0')
            if flags&16:items(depth+1)
            if flags&128:return
    items();return bytes(output)
def generate():
    data=(ROOT/'build/gdi-audit/USER.EXE').read_bytes()
    assert hashlib.sha256(data).hexdigest()==EXPECTED
    ne=struct.unpack_from('<I',data,60)[0];at=ne+struct.unpack_from('<H',data,ne+0x24)[0]
    align=struct.unpack_from('<H',data,at)[0];at+=2;menus={};manifest=[]
    while True:
        kind,count=struct.unpack_from('<HH',data,at);at+=8
        if not kind:break
        for _ in range(count):
            offset,length,flags,id,_,_=struct.unpack_from('<6H',data,at);at+=12
            if kind==0x8004 and id in (0x8001,0x8002):
                raw=data[offset<<align:(offset+length)<<align];native=convert(raw);menus[id&32767]=native
                manifest.append({'resource':id&32767,'original_bytes':len(raw),'original_sha256':hashlib.sha256(raw).hexdigest(),
                    'native_bytes':len(native),'native_sha256':hashlib.sha256(native).hexdigest()})
    assert set(menus)=={1,2}
    text='// Local Win98 USER.EXE menu resources; ANSI labels converted to UTF-16.\n'
    for id,menu in sorted(menus.items()):text+=f'static const unsigned char OriginalSystemMenu{id}[] = {{'+','.join(map(str,menu))+'};\n'
    out=ROOT/'build/generated/user_menus.h'
    if not out.exists()or out.read_text()!=text:out.write_text(text)
    report={'source_sha256':EXPECTED,'menus':manifest,'conversion':'Resource header/item flags and IDs preserved; CP1252 labels encoded as UTF-16LE; alignment padding omitted.'}
    (out.parent/'user-menu-provenance.json').write_text(json.dumps(report,indent=2)+'\n')
    return report
if __name__=='__main__':generate()
