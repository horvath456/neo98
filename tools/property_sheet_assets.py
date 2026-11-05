#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract the original COMCTL32 property-sheet and wizard frame resources into ignored build assets."""
from pathlib import Path
import argparse,hashlib,json,struct
import pefile
SHA='2b2f3147fd6c95d939b01b50390732a359866322c4feaaeeccaa8c70c12f8aae'
def build(root,out):
 raw=(root/'build/programs/COMCTL32.DLL').read_bytes();assert hashlib.sha256(raw).hexdigest()==SHA
 pe=pefile.PE(data=raw);out.mkdir(parents=True,exist_ok=True);entries=[];records=[]
 for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
  if t.id!=5:continue
  for n in t.directory.entries:
   if n.id not in(1006,1020):continue
   for lang in n.directory.entries:
    if lang.id!=0x409:continue
    d=lang.data.struct;data=pe.get_data(d.OffsetToData,d.Size);entries.append((n.id,data));records.append({'type':5,'id':n.id,'language':lang.id,'size':len(data),'sha256':hashlib.sha256(data).hexdigest()})
 assert {i for i,_ in entries}=={1006,1020}
 text='// Generated from local COMCTL32.DLL. Original binary bytes; do not commit.\n#pragma once\nnamespace OriginalPropertySheetAssets {\n'
 for i,data in entries:text+=f'static const unsigned char Template{i}[]={{'+','.join(str(x)for x in data)+'};\n'
 for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
  if t.id!=6:continue
  for n in t.directory.entries:
   if n.id!=261:continue
   for lang in n.directory.entries:
    if lang.id!=0x409:continue
    d=lang.data.struct;data=pe.get_data(d.OffsetToData,d.Size);at=0
    for j in range(3):
     length=struct.unpack_from('<H',data,at)[0];at+=2;value=data[at:at+length*2];at+=length*2
    value+=bytes(2);text+='static const unsigned char CaptionFormat[]={'+','.join(str(x)for x in value)+'};\n';records.append({'type':6,'id':4162,'language':0x409,'size':len(value),'sha256':hashlib.sha256(value).hexdigest()})
 text+='}\n';header=out/'templates.h'
 if not header.exists()or header.read_text()!=text:header.write_text(text)
 (out/'manifest.json').write_text(json.dumps({'source_sha256':SHA,'resources':records},indent=2)+'\n')
 return records
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--out',type=Path);a=p.parse_args();print(json.dumps(build(a.root.resolve(),a.out or a.root/'build/property-sheet-assets')))
