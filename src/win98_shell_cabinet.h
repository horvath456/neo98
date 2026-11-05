// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string.h>
namespace Win98ShellCabinet {
using U=unsigned;
static constexpr char Explorer[]="Software\\Microsoft\\Windows\\CurrentVersion\\Explorer";
static constexpr char Cabinet[]="Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CabinetState";
struct Settings{U words[7]={};};
struct Record{unsigned short size,version;U flags,attributes;};
static_assert(sizeof(Record)==12,"Win98 cabinet record size");
// Platform identity and low-memory classification are providers.
template<class E>void Defaults(E&e,Settings&s){s.words[6]=10;U low=e.OtherPlatform()?0:!e.LowMemory();s.words[1]=(s.words[1]&~0x40u)|((low|0x20)<<6);}
// Per-process INSTDATA cache.
template<class E>void Refresh(E&e,Settings&s){e.Lock();U key=e.ExplorerKey(),bytes=28,type=0;
 if(!key||e.Query(key,"ShellState",&type,s.words,&bytes)){
  memset(s.words+1,0,24);s.words[1]|=0x820;s.words[0]=28;s.words[5]=1;Defaults(e,s);
 }else{
  if(s.words[0]==28&&s.words[6]==9)s.words[6]=10;
  if(s.words[0]!=28||s.words[6]!=10){s.words[1]=(s.words[1]&3)|0x820;s.words[0]=28;Defaults(e,s);}
 }
 if(e.Restricted(0x40000004))s.words[1]&=~0x40u;
 if(e.Restricted(0x40000008))s.words[1]=(s.words[1]&~0x20u)|0x80;
 U advanced=0;if(key&&!e.Open(key,"Advanced",0x02000000,advanced)){
  U hidden[7]={4};if(!e.Query(advanced,"Hidden",&type,hidden,&bytes))s.words[1]=(s.words[1]&~9u)|(hidden[0]==1?1u:0u)|(hidden[0]==2?8u:0u);U value=hidden[0];
  const char*names[]={"ShowCompColor","HideFileExt","DontPrettyPath","ShowInfoTip","HideIcons","ShowAttribCol","MapNetDrvBtn"};
  const U bits[]={0x10,2,0x100,0x800,0x1000,0x200,0x400};
  for(U i=0;i<7;++i){bytes=4;if(!e.Query(advanced,names[i],&type,&value,&bytes)){U bit=i==1?!value:value&1;s.words[1]=(s.words[1]&~bits[i])|(bit?bits[i]:0);}}
  e.Close(advanced);
 }e.Unlock();
}
// Read-only bit projection of original #68,  +.
// SHGetSettings masks the separate pointer/sort operations before this path.
template<class E>U SettingsBits(E&e,Settings&s,U current,U mask){if(s.words[0]!=28)Refresh(e,s);
 const U masks[]={1,0x20,2,8,0x8000,0x80,0x200,0x400,0x800,0x2000,0x4000,0x100,0x1000};
 const U bits[]={1,8,2,0x10,4,0x20,0x40,0x80,0x100,0x800,0x1000,0x200,0x400};
 for(U i=0;i<13;++i)if(mask&masks[i])current=(current&~bits[i])|(s.words[1]&bits[i]);return current;
}
// Complete named SHGetSettings adapter.
template<class E>U GetSettings(E&e,Settings&s,U mask){Refresh(e,s);return SettingsBits(e,s,0,mask&~0x14u);}
template<class E>U Read(E&,Settings&,void*,U);
// #652. Short input merges with current full record.
template<class E>U Write(E&e,Settings&s,const Record*input){if(!input)return 0;Record local;const Record*p=input;
 if(input->size<12){Read(e,s,&local,12);memcpy(&local,input,input->size);local.size=12;p=&local;}
 U key=0;if(e.Create(Cabinet,key))return 0;U full=p->flags&1;
 U result=!e.Set(key,"Settings",3,p,12);e.Set(key,"FullPath",4,&full,4);e.Close(key);return result;
}
// #654,  +.
// InitialFlags models the original uninitialized local bits; native uses zero.
template<class E>U Read(E&e,Settings&s,void*out,U bytes){U settings=SettingsBits(e,s,e.InitialSettingsBits(),0x400),result=0;if(!out||!bytes)return 0;
 Record record{12,1,(e.InitialFlags()&0xffff0010u)|0x10a|((settings>>2)&0x20),0x60};
 if(!e.CleanBoot()){
  U key=0;if(!e.Open(0x80000001,Cabinet,0x20019,key)){
   U size=12,type=0,full=0;result=!e.Query(key,"Settings",&type,&record,&size);size=4;
   if(!e.Query(key,"FullPath",nullptr,&full,&size))record.flags=(record.flags&~1u)|(full!=0);e.Close(key);
  }
 }
 if(result&&record.version<1){if(!record.version){record.flags=(record.flags&0xffff01dfu)|0x100|((settings>>2)&0x20);record.attributes=0x60;}record.size=12;record.version=1;Write(e,s,&record);}
 record.size=bytes<=12?bytes:12;e.CopyResult(out,&record,bytes);return result;
}
}
