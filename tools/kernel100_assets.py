#!/usr/bin/env python3
"""Extract code-page and language names from the pinned local Win98 KERNEL32."""
import hashlib
import json
from pathlib import Path
import struct
import pefile
ROOT=Path(__file__).resolve().parents[1]

def generate():
    data=(ROOT/'build/programs/KERNEL32.DLL').read_bytes()
    assert hashlib.sha256(data).hexdigest()=='9c8d574f84bfbbb6521dac54d0bfaf7a4c8fee07e468525cdb9da7db418e6c7e'
    pe=pefile.PE(data=data);pages=[];languages=[]
    for kind in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        for name in kind.directory.entries:
            entry=name.directory.entries[0].data.struct
            raw=pe.get_data(entry.OffsetToData,entry.Size)
            if str(kind.name)=='CODEPAGE' and name.id in (437,850,1252,65000,65001):
                pages.append((name.id,raw.decode('utf-16le').rstrip('\0')))
            if kind.id==6:
                offset=0
                for i in range(16):
                    n=struct.unpack_from('<H',raw,offset)[0];offset+=2
                    value=raw[offset:offset+2*n].decode('utf-16le');offset+=2*n
                    key=(name.id-1)*16+i
                    if key>=1024 and value:languages.append((key,value))
    assert len(pages)==3
    text='// SPDX-License-Identifier: GPL-2.0-or-later\n// Extracted by tools/kernel100_assets.py from pinned original KERNEL32 resources.\n#pragma once\n'
    for name,values in [('KernelCodepageNames',pages),('KernelLanguageNames',languages)]:
        text+='inline constexpr struct {unsigned id;const char *text;} '+name+'[]={\n'
        text+=''.join('    {%d,%s},\n'%(key,json.dumps(value,ensure_ascii=True)) for key,value in values)
        text+='};\n'
    (ROOT/'src/win98_kernel_nls_names.h').write_text(text)
    return {'codepages':len(pages),'languages':len(languages)}
if __name__=='__main__':print(generate())
