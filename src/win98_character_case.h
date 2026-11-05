// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_system_locale.h"
namespace Win98CharacterCase {
using U=unsigned;
// USER32. The original uses a 20-byte inline temporary.
template<class E> U Change(E&e,U source,U count,bool lower,bool string_probe){
 if(!source)return 0;U locale=e.UserLocale();
 if(!(source>>16)&&!count){unsigned char result=0;return e.MapCharacter(locale,lower,source&255,result)?result:0;}
 if(!count)count=e.Length(source);
 if((string_probe&&e.BadString(source,65535))||(count&&e.BadWrite(source,count))){e.Error(87);return 0;}
 if(!count)return source;
 unsigned char local[20];bool allocated=count+1>=20;unsigned char*out=allocated?e.Allocate(count+1):local;if(!out)return 0;
 U result=e.MapBuffer(locale,lower,source,count,out)?source:0;
 if(result)for(U i=0;i<count;++i)e.Write8(source+i,out[i]);
 if(allocated)e.Free(out);return result;
}
// USER32/4929 and04A70/4A96 public return/count adapters.
template<class E> U Public(E&e,U source,U count,bool lower,bool buffer){
 if(buffer){if(!count)return 0;return Change(e,source,count,lower,false)?count:0;}
 return Change(e,source,0,lower,true);
}
// LCMapStringA case-only route. Existing locale resolution is
// reused. No public sort-key, linguistic or general LCMapString export is claimed.
template<class E> U KernelCase(E&e,U requested,bool lower,U source,U count,U output,U capacity){
 const U system=e.SystemLocale();U locale=Win98SystemLocale::ResolveLocale(requested,system,[&]{return e.UserLocale();});
 U data=locale==system?e.SystemTable():(locale&0xfff00000u)?0:e.LookupLocale(locale);
 if(!data||!source||!count||int(capacity)<0||(capacity&&!output)){e.Error(87);return 0;}
 if(int(count)<=-1)count=e.Length(source)+1;
 // Exact SBCS provider path, including forward overlapping
 // writes, Greek final sigma, DBCS traversal/delta tables and partial output.
 if(!capacity)return count;if(capacity<count){e.Error(122);return 0;}
 U cursor=source,end=source+count,dst=output,last=output+capacity,written=0;
 while(cursor<end&&dst<last){
  U ch=e.Read8(cursor);bool dbcs=e.Multibyte(data);
  if(dbcs&&e.Lead(data,ch)){if(cursor+1>=end)return written;ch=(ch<<8)|e.Read8(cursor+1);}
  U mapped=ch;
  if(dbcs){U index=e.CaseWord(data,lower,ch>>8);index=e.CaseWord(data,lower,index+((ch>>4)&15));mapped=(ch+e.CaseWord(data,lower,index+(ch&15)))&65535;}
  else if(lower&&e.LocaleNumber(data)==0x408&&ch==0xd3){
   if(cursor+1>=end)mapped=count<2?0xf3:0xf2;
   else{U next=e.Read8(cursor+1);bool letter=(next>=0xb8&&next!=0xbb&&next!=0xbd&&next!=0xff&&next!=0xd2)||next==0xa2;mapped=letter?0xf3:0xf2;}
  }else mapped=e.CaseByte(data,lower,ch);
  U bytes=mapped<256?1:2;if(bytes==1){e.Write8(dst,mapped);++written;}else if(dst<last-1){e.Write8(dst,mapped>>8);e.Write8(dst+1,mapped);written+=2;}
  dst+=bytes;cursor+=ch<256?1:2;
 }
 if(cursor<end){e.Error(122);return 0;}return written;
}
}
