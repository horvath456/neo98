#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Extract the original GDI ANSI font charset, including its glyph differences."""
import argparse,hashlib,json,struct,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from provenance import stamp as stamp_provenance
sys.path.insert(0, str(Path(__file__).resolve().parent))
import win98_c  # noqa: E402
ROOT=next(p for p in Path(__file__).resolve().parents if(p/'neo98.py').is_file())
EXPECTED='cc6c5c227108e7ace5ce72b42f810f718ce8c2413acda9edfe5a6c946678a16f'
def extract(source,target):
 data=Path(source).read_bytes();assert hashlib.sha256(data).hexdigest()==EXPECTED
 ne=struct.unpack_from('<I',data,60)[0];shift=struct.unpack_from('<H',data,ne+0x32)[0];segments=ne+struct.unpack_from('<H',data,ne+0x22)[0]
 offset=struct.unpack_from('<H',data,segments+10*8)[0]<<shift;raw=data[offset+0x6ee:offset+0x8ee];assert len(raw)==512
 values=struct.unpack('<256H',raw);assert values[31]==0 and values[0xb7]==0x2219 and values[128]==0x20ac
 target=Path(target);target.parent.mkdir(parents=True,exist_ok=True);target.write_text('// Generated from local GDI.EXE segment; do not commit.\nstatic const unsigned short OriginalGdiAnsiCharset[256]={\n'+''.join(' '+','.join('0x%04x'%v for v in values[i:i+16])+',\n'for i in range(0,256,16))+'};\n')
 return hashlib.sha256(raw).hexdigest()
def generate():
 source=ROOT/'build/gdi-audit/GDI.EXE'
 if not source.exists():
  source.parent.mkdir(parents=True,exist_ok=True)
  win98_c.copy('::/WINDOWS/SYSTEM/GDI.EXE', str(source))
 target=ROOT/'build/generated/font_charset_tables.h';table_hash=extract(source,target)
 report={'source_sha256':EXPECTED,'segment':11,'offset':0x6ee,'bytes':512,
         'table_sha256':table_hash,'generated_sha256':{target.name:hashlib.sha256(target.read_bytes()).hexdigest()}}
 stamp_provenance(report,'font-charset-provenance.json')
 (target.parent/'font-charset-provenance.json').write_text(json.dumps(report,indent=2)+'\n')
 return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=ROOT/'build/gdi-audit/GDI.EXE');p.add_argument('--output',type=Path,default=ROOT/'build/generated/font_charset_tables.h');a=p.parse_args();print(extract(a.source,a.output))
