// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98FileTimes {
using U=unsigned;using Q=unsigned long long;
struct Dos {U date=0,time=0,hundredths=0;};
// KERNEL32 /. Native providers replace
// object resolution, INT21 file services and handle-reference bookkeeping.
// Optional outputs and partial updates keep the original operation order.
template<class E> U Run(E&e,const U*a,bool set){
 e.Lock();U cookie=e.EnterFile(a[0]),object=e.Resolve(a[0],set?0x100:0);
 struct Scope {E&e;U cookie,object;~Scope(){if(object)e.Release(object);e.LeaveFile(cookie);e.Unlock();}}scope{e,cookie,object};
 if(!object)return 0;
 U handle=e.DosHandle(object);if(handle==~0u){e.Error(12);return 0;}
 for(U i=0;i<3;++i){
  U p=a[i+1];if(!p)continue;Dos stamp;
  if(set){
   Q value=e.ReadFileTime(p);if(!value)continue;
   if(!e.ToDos(value,stamp))return 0;
   if(i==2&&stamp.hundredths)stamp.time=(stamp.time+1)&65535;
   U error=e.Set(handle,i,stamp);
   if(error&&!(i<2&&error==1)){e.Error(error&65535);return 0;}
  }else{
   U error=e.Get(handle,i,stamp);
   if(error){if(i<2&&error==1){e.WriteFileTime(p,0);continue;}e.Error(error&65535);return 0;}
   Q value=0;if(!e.FromDos(stamp,value))return 0;e.WriteFileTime(p,value);
  }
 }
 return 1;
}
}
