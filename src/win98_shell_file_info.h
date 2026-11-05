// SPDX-License-Identifier: GPL-2.0-or-later
// SHELL32 4.72.3110.6, SHGetFileInfo[A],
// cold continuations. Namespace/COM/icon services are providers.
#pragma once
namespace Win98ShellFileInfo {
using U=unsigned;
template<class E> U Query(E&e,U path,U attributes,U output,U size,U flags){
 (void)size;U desktop=e.Desktop(),result=1;
 if(!path){e.NullPath();return 0;}
 if(flags==0x2000)return e.ExecutableType(path);
 if(!output)return 0;
 e.Put(output,0);e.Byte(output+12,0);e.Byte(output+272,0);
 if(!(flags&8)){
  if((flags&16)&&(((attributes&16)&&(attributes&5))||e.IsRoot(path)))flags&=~16u;
  if(e.Relative(path))path=e.Absolute(path);
 }
 U pidl=(flags&8)?path:e.Parse(path,(flags&16)!=0);
 if(!pidl)return 0;
 U tail=e.Last(pidl);
 if(flags&16){U type=e.Type(tail);if((type&0x70)==0x30||(type&0x70)==0x20)e.Type(tail,type|(attributes&16?0x31:0x32));}
 if(flags&0x400)e.TypeName(tail,output+272,80);
 if(flags&0x5b00){
  U folder=0;int bound=0;
  if(tail==pidl){folder=desktop;e.AddRef(folder);}else bound=e.Bind(desktop,pidl,tail,folder);
  if(bound<0)result=0;
  else {
   if(flags&0x800){if(!(flags&0x20000))e.Put(output+8,~0u);if(e.Attributes(folder,tail,output+8)<0)e.Put(output+8,0);}
   if(flags&0x1000){U extractor=0;if(e.Extractor(folder,tail,extractor)>=0){U location_flags=0;e.Location(extractor,output+12,260,output+4,location_flags);e.Release(extractor);if(location_flags&8){if(e.GetByte(output+12)!='*')e.Put(output+4,0);e.Byte(output+12,0);}}}
   if(flags&0x4100){
    if(!e.LargeList())e.InitializeLists();
    if(flags&0x4000)result=(flags&1)?e.SmallList():e.LargeList();
    U index=e.IconIndex(folder,tail,(flags&2)!=0,output+4);if(!(flags&2))e.Put(output+4,index);
    if(flags&0x100){
     U list=(flags&1)?e.SmallList():e.LargeList();U cx=e.Metric(flags&1?49:11),cy=e.Metric(flags&1?50:12);
     if(!(flags&0x800)){e.Put(output+8,0x10000);e.Attributes(folder,tail,output+8);}
     U draw=(e.Get(output+8)&0x10000)||(flags&0x8000)?0x200:0;if(flags&0x10000)draw|=4;
     U icon=e.Icon(list,e.Get(output+4),draw);e.Put(output,icon);
     if(icon&&!(flags&4))e.Put(output,e.ResizeIcon(icon,cx,cy));
    }
   }
   if(flags&0x200)e.DisplayName(folder,tail,output+12,260);
   if(folder)e.Release(folder);
  }
 }
 if(!(flags&8))e.FreePidl(pidl);
 return result;
}
// Original SHGetFileInfoW neither reads pointers nor sets error.
inline U Wide(){return 0;}
}
