#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Extract original GDI32 charset/codepage/signature lookup tables."""
import argparse,hashlib,json,struct,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from provenance import stamp as stamp_provenance
sys.path.insert(0, str(Path(__file__).resolve().parent))
import win98_c  # noqa: E402
ROOT=next(p for p in Path(__file__).resolve().parents if(p/'neo98.py').is_file())
EXPECTED='8d19211138ad53f473bd276832af1d75289f074a2428f5f814c2ae53fe0bbe21'
def extract(source,target):
 data=Path(source).read_bytes();assert hashlib.sha256(data).hexdigest()==EXPECTED
 pe=struct.unpack_from('<I',data,60)[0];n=struct.unpack_from('<H',data,pe+6)[0];optional=struct.unpack_from('<H',data,pe+20)[0];sections=pe+24+optional;raw=None
 for i in range(n):
  size,rva,length,offset=struct.unpack_from('<4I',data,sections+i*40+8)
  if rva<=0x1f300 and 0x1f300+168<=rva+length:raw=data[offset+0x1f300-rva:offset+0x1f300-rva+168]
 assert raw and len(raw)==168;values=struct.unpack('<42I',raw);assert values[0]==0 and values[14]==1252 and values[28]==1
 target=Path(target);target.parent.mkdir(parents=True,exist_ok=True);target.write_text('// Generated from local GDI32.DLL; do not commit.\nstatic const unsigned OriginalGdiCharsetInfo[3][14]={\n'+''.join(' {'+','.join('0x%x'%v for v in values[i:i+14])+'},\n'for i in range(0,42,14))+'};\n');return hashlib.sha256(raw).hexdigest()
def generate():
 source=ROOT/'build/programs/GDI32.DLL'
 if not source.exists():
  source.parent.mkdir(parents=True,exist_ok=True)
  win98_c.copy('::/WINDOWS/SYSTEM/GDI32.DLL', str(source))
 target=ROOT/'build/generated/font_charset_info.h';table_hash=extract(source,target)
 report={'source_sha256':EXPECTED,'address':'BFF3F300','bytes':168,'table_sha256':table_hash,'generated_sha256':{target.name:hashlib.sha256(target.read_bytes()).hexdigest()}}
 stamp_provenance(report,'font-charset-info-provenance.json')
 (target.parent/'font-charset-info-provenance.json').write_text(json.dumps(report,indent=2)+'\n')
 return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=ROOT/'build/programs/GDI32.DLL');p.add_argument('--output',type=Path,default=ROOT/'build/generated/font_charset_info.h');a=p.parse_args();print(extract(a.source,a.output))
