#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Convert local SERIALUI NE dialog resources to native USER's PE template format."""
import hashlib,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SHA='b3207725c6a9bc325744add821993d1e753c6fd8d6482a78dc26b354ba49d95e'
def resources(raw):
 ne=struct.unpack_from('<I',raw,60)[0];at=ne+struct.unpack_from('<H',raw,ne+36)[0]
 shift=struct.unpack_from('<H',raw,at)[0];at+=2;result={}
 while True:
  kind,count=struct.unpack_from('<HH',raw,at);at+=8
  if not kind:return result
  for _ in range(count):
   offset,size,flags,name,handle,usage=struct.unpack_from('<6H',raw,at);at+=12
   result[kind&32767,name&32767]=raw[offset<<shift:(offset+size)<<shift]
def dialog(data):
 at=0
 def unpack(fmt):
  nonlocal at
  values=struct.unpack_from('<'+fmt,data,at);at+=struct.calcsize('<'+fmt);return values
 def string(klass=False):
  nonlocal at
  if klass and data[at]&128:
   value=data[at];at+=1;return struct.pack('<HH',65535,value)
  if data[at]==255:
   at+=1;return struct.pack('<HH',65535,unpack('H')[0])
  end=data.index(0,at);text=data[at:end].decode('cp1252');at=end+1
  if klass and text=='msctls_trackbar':text='msctls_trackbar32'
  return text.encode('utf-16le')+b'\0\0'
 style,count,x,y,w,h=unpack('IB4h');out=bytearray(struct.pack('<IIH4h',style,0,count,x,y,w,h))
 out+=string();out+=string();out+=string()
 if style&64:out+=struct.pack('<H',unpack('H')[0])+string()
 for _ in range(count):
  x,y,w,h,id,style=unpack('4hHI');out+=bytes((-len(out))%4)
  out+=struct.pack('<II4hH',style,0,x,y,w,h,id)+string(True)+string()
  extra=unpack('B')[0];out+=struct.pack('<H',extra)+data[at:at+extra];at+=extra
 return bytes(out)
def generate():
 path=ROOT/'build/win98-original/SERIALUI.DLL'
 if not path.exists():
  import win98_c
  path.parent.mkdir(parents=True,exist_ok=True)
  win98_c.copy('::/WINDOWS/SYSTEM/SERIALUI.DLL', path)
 raw=path.read_bytes();assert hashlib.sha256(raw).hexdigest()==SHA
 rs=resources(raw);out=ROOT/'build/serialui-assets';out.mkdir(parents=True,exist_ok=True)
 arrays={f'Template{id}':dialog(rs[5,id]) for id in (119,101)}
 strings={}
 for (kind,id),data in rs.items():
  if kind!=6:continue
  at=0
  for i in range(16):
   length=data[at];at+=1;value=data[at:at+length];at+=length
   if length:strings[(id-1)*16+i]=value+b'\0'
 arrays.update({f'String{id}':value for id,value in strings.items()})
 text='// Generated from local SERIALUI.DLL; do not commit binary resources.\n#pragma once\nnamespace OriginalSerialUiAssets {\n'
 for name,data in arrays.items():text+=f'static const unsigned char {name}[]={{'+','.join(map(str,data))+'};\n'
 text+='inline const char *String(unsigned id){switch(id){\n'
 for id in strings:text+=f'case {id}:return reinterpret_cast<const char *>(String{id});\n'
 text+='default:return "";}}\n}\n';header=out/'templates.h'
 if not header.exists() or header.read_text()!=text:header.write_text(text)
 (out/'manifest.json').write_text(json.dumps({'source_sha256':SHA,'resources':{name:hashlib.sha256(data).hexdigest() for name,data in arrays.items()}},indent=2)+'\n')
 return header
if __name__=='__main__':print(generate())
