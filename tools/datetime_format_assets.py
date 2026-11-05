#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract the original en-US format lists and differential-only field provider."""
from pathlib import Path
import argparse,hashlib,json,struct
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--out-root',type=Path,required=True);a=p.parse_args()
data=(a.root/'build/crt-locale/LOCALE.NLS').read_bytes()
assert hashlib.sha256(data).hexdigest()=='6b3ad917b262fd4a098ac8830703c6f2d20364253399d4c728a2a91b7550e74d'
record=0xc3ce
s='// SPDX-License-Identifier: GPL-2.0-or-later\n// Extracted from original LOCALE.NLS en-US record C3CE.\n#pragma once\nnamespace Win98DateTime {\n'
for name,offset in [('Times',0x2c),('ShortDates',0x34),('YearMonths',0x38),('LongDates',0x3a)]:
 start,end=struct.unpack_from('<2H',data,record+offset);values=data[record+start*2:record+end*2].decode('utf-16le').rstrip('\0').split('\0')
 s+='inline constexpr const char* '+name+'[]={'+','.join(json.dumps(x) for x in values)+'};\n'
s+='}\n';(a.out_root/'src').mkdir(parents=True,exist_ok=True);(a.out_root/'tools').mkdir(parents=True,exist_ok=True);(a.out_root/'src/win98_datetime_formats.h').write_text(s)
# The bridge data are checked against the actual NLS file offsets, not trusted JSON text.
fields=json.loads((a.root/'build/crt-locale/us-fields.json').read_text())
s='// Original LOCALE.NLS fields used only by differential bridge.\ninline const char*Field(unsigned f){switch(f){\n'
for key,value in fields.items():
 start=value['file_offset'];end=start
 while data[end:end+2]!=b'\0\0':end+=2
 text=data[start:end].decode('utf-16le');assert text==value['text'];s+='case 0x'+key+':return '+json.dumps(text)+';\n'
s+='default:return nullptr;}}\n';(a.out_root/'tools/datetime_fields.h').write_text(s)
