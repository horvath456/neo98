// SPDX-License-Identifier: GPL-2.0-or-later
// Original SHELL32 ordinal registry wrappers and SHLWAPI Win9x Unicode adapter.
#pragma once
namespace Win98ShellRegistryOrdinal {
using U=unsigned;
//  /. Failed native opens explicitly clear the output handle.
template<class E> U Open(E&e,bool wide,U root,U path,U access,U output){
 U result=e.Open(wide,root,path,0,access,output);if(result)e.Write32(output,0);return result;
}
//  /. Only a missing queried value becomes an empty string.
template<class E> U Default(E&e,bool wide,U root,U path,U data,U size){
 typename E::Scratch local(e,8);U key=local.address,type=key+4;
 e.Write32(key,root);
 if(path && (wide?e.Read16(path):e.Read8(path))){
  e.Write32(key,path);
  // W calls ADVAPI32 directly, A goes through the handle-clearing wrapper.
  U status=wide?e.Open(true,root,path,0,1,key):Open(e,false,root,path,1,key);
  if(status)return status;
 }
 U handle=e.Read32(key),result=e.Query(wide,handle,0,0,type,data,size);
 if(handle!=root)e.Close(handle);
 if(result==2){if(data){e.Write8(data,0);if(wide)e.Write8(data+1,0);}if(size)e.Write32(size,wide?2:1);return 0;}
 return result;
}
// Type-specific ANSI buffer conversion.
template<class E> U Convert(E&e,U data,U capacity,U kind,U size){
 if(kind!=1 && kind!=2 && kind!=7)return 0;
 U count=size?e.Read32(size):~0u;
 if(!data){if(size)e.Write32(size,e.Read32(size)*2);return 0;}
 U words=e.MultiToWide(data,count,0,0),bytes=words*2;
 U temporary=e.Allocate(0x40,bytes),result=0;
 if(!temporary)result=8;
 else {
  if(bytes>capacity)result=234;
  else {e.MultiToWide(data,count,temporary,words);e.WideCopy(data,temporary);}
  e.Free(temporary);
 }
 if(size)e.Write32(size,bytes);return result;
}
// The Windows 9x path uses this provider; NT uses a different one.
template<class E> U QueryWide(E&e,U key,U name,U reserved,U type,U data,U size){
 typename E::Scratch local(e,272);U ansi=local.address,local_type=ansi+260;
 U capacity=size?e.Read32(size):0;
 if(name){e.WideToAnsi(name,ansi,260);name=ansi;}
 if(!type)type=local_type;
 U result=e.QueryAnsi(key,name,reserved,type,data,size);
 if(!result)result=Convert(e,data,capacity,e.Read32(type),size);
 return result;
}
template<class E> U Call(E&e,U ordinal,const U*a){
 switch(ordinal){
 case 505:return e.Close(a[0]);
 case 506:case 507:return Open(e,ordinal==507,a[0],a[1],0x02000000,a[2]);
 case 508:case 510:return Default(e,ordinal==510,a[0],a[1],a[2],a[3]);
 case 509:case 511:return e.Query(ordinal==511,a[0],a[1],a[2],a[3],a[4],a[5]);
 case 592:return QueryWide(e,a[0],a[1],a[2],a[3],a[4],a[5]);
 }
 return 120;
}
}
