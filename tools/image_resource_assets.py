#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Import original USER image resources and system-image registration tables."""
from pathlib import Path
import hashlib,json,struct,sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from generated_file import write_text
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/image-resource';HASH='18303935de5e88482e9630a06480a502c1b9d6281c4013c2dd1ca88086acf192'
def generate():
 p=(ROOT/'build/gdi-audit/USER.EXE').read_bytes();assert hashlib.sha256(p).hexdigest()==HASH
 u16=lambda at:struct.unpack_from('<H',p,at)[0];ne=struct.unpack_from('<I',p,60)[0];table=ne+u16(ne+36);shift=u16(table);at=table+2;entries=[]
 while u16(at):
  typ=u16(at);count=u16(at+2);at+=8
  for _ in range(count):
   off=u16(at)<<shift;length=u16(at+2)<<shift;name=u16(at+6);at+=12
   # RT_CURSOR, RT_BITMAP, RT_ICON, RT_DIALOG, RT_GROUP_CURSOR, RT_GROUP_ICON.
   if typ not in (0x8001,0x8002,0x8003,0x8005,0x800c,0x800e):continue
   if name&0x8000:key=name&0x7fff;text=''
   else:n=p[table+name];text=p[table+name+1:table+name+1+n].decode('latin1');key=0
   entries.append((typ&0x7fff,key,text,p[off:off+length],off))
 OUT.mkdir(exist_ok=True);h=['// Generated read-only USER.EXE resources; source SHA256 '+HASH,'#pragma once','struct OriginalImageResource {unsigned type,id;const char*name;const unsigned char*data;unsigned size;unsigned*length=nullptr;\n    constexpr unsigned Size()const{return length?*length:size;}};']
 for i,(typ,key,name,data,off)in enumerate(entries):h.append(f'inline constexpr unsigned char OriginalImageBytes{i}[]={{'+','.join(str(x)for x in data)+'};')
 h.append('inline constexpr OriginalImageResource OriginalImageResources[]={')
 for i,(typ,key,name,data,off)in enumerate(entries):h.append('{'+f'{typ},{key},'+json.dumps(name)+f',OriginalImageBytes{i},sizeof OriginalImageBytes{i}'+'},')
 h.append('};');write_text(OUT/'system_assets.h','\n'.join(h)+'\n')
 result={'sha256':HASH,'resources':[{'type':t,'id':i,'name':s,'file_offset':o,'size':len(b),'sha256':hashlib.sha256(b).hexdigest()}for t,i,s,b,o in entries]};(OUT/'system-assets.json').write_text(json.dumps(result,indent=2)+'\n');return result
if __name__=='__main__':print(json.dumps({'resources':len(generate()['resources']),'header':str(OUT/'system_assets.h')}))
