// SPDX-License-Identifier: GPL-2.0-or-later
// Original KERNEL32 DST and shared timezone core.
#pragma once
#include "win98_time.h"
namespace Win98Timezone {
using namespace Win98Time;
struct Zone { U bias; W standard_name[32]; SystemTime standard; U standard_bias; W daylight_name[32]; SystemTime daylight; U daylight_bias; };
static_assert(sizeof(Zone)==172,"Win98 TIME_ZONE_INFORMATION");
struct State { Zone zone{}; U flag=0,minutes=0; Q bias=0,start=0,end=0; };
inline bool Less(Q a,Q b){return static_cast<long long>(a)<static_cast<long long>(b);}
// Absolute dates must not precede now, even in current-year mode.
inline bool Transition(const SystemTime &rule,Q &out,Q now,bool current_year){
 SystemTime today;FileToSystem(now,today);
 if(rule.year){if(!SystemToFile(rule,out))return false;return !Less(out,now);}
 const int week=SignedWord(rule.day);if(week>5||!week)return false;
 W year=today.year;bool check=false;
 if(!current_year){if(SignedWord(today.month)>SignedWord(rule.month))++year;else if(today.month==rule.month)check=true;}
 for(unsigned attempt=0;attempt<2;++attempt){
  SystemTime day={year,rule.month,0,1,rule.hour,rule.minute,rule.second,rule.milliseconds};Q stamp;
  if(!SystemToFile(day,stamp))return false;SystemTime first;FileToSystem(stamp,first);
  int wanted=SignedWord(rule.weekday),actual=SignedWord(first.weekday);
  if(wanted<actual)day.day=W(day.day+wanted-actual+7);else if(wanted>actual)day.day=W(day.day+wanted-actual);
  W last=day.day;
  for(int nth=1;nth<week;++nth){day.day=W(day.day+7);if(!SystemToFile(day,stamp))break;SystemTime value;FileToSystem(stamp,value);last=value.day;}
  day.day=last;if(!SystemToFile(day,stamp))return false;
  if(check&&(SignedWord(today.day)>SignedWord(day.day)||(today.day==day.day&&Less(stamp,now)))){check=false;++year;continue;}
  out=stamp;return true;
 }
 return false;
}
// both transition months must be present. North/south seasons differ.
template<class E> U Classify(E&e,const Zone&z){
 if(!z.standard.month||!z.daylight.month)return 1;
 Q now=e.Local(),standard=0,daylight=0;
 if(!Transition(z.standard,standard,now,true)||!Transition(z.daylight,daylight,now,true))return ~0u;
 if(!Less(standard,daylight))return !Less(now,daylight)&&Less(now,standard)?2:1;
 return Less(now,standard)||!Less(now,daylight)?2:1;
}
// Windows 98 clears the repeated-hour exclusion and its persisted bounds.
template<class E> void Reset(E&e,State&s){s.start=s.end=0;U key=e.Open(false);if(key){e.Delete(key,"NoChangeStart");e.Delete(key,"NoChangeEnd");e.Close(key);}}
// registry values are REG_BINARY, exactly four bytes.
template<class E> U RefreshBias(E&e,State&s,U key){
 U base=0,extra=0,type=0,size=4,error=e.Query(key,"Bias",type,&base,size);
 if(!error&&(type!=3||size!=4))error=13;
 if(!error){size=4;error=e.Query(key,s.flag==2?"DaylightBias":"StandardBias",type,&extra,size);if(!error&&(type!=3||size!=4))error=13;}
 if(error){e.Error(error);return 0;}
 s.minutes=base+extra;s.bias=MinutesBias(s.minutes).ticks;
 error=e.Set(key,"ActiveTimeBias",3,&s.minutes,4);if(error){e.Error(error);return 0;}e.BiasChanged();return 1;
}
// Unnamed ordinal99. The argument permits changing the DOS/local clock.
template<class E> U Check(E&e,State&s,U adjust){
 if(s.start){Q now=e.Local();if(!Less(now,s.start)&&!Less(s.end,now))return 0;Reset(e,s);}
 U flag=Classify(e,s.zone);if(flag==~0u)flag=1;if(flag==s.flag)return 0;
 s.flag=flag;U key=e.Open(true);if(!key)return 1;
 e.Set(key,"DaylightFlag",3,&s.flag,4);Q previous=s.bias;RefreshBias(e,s,key);
 if(adjust){U type=0,size=4,disabled=0;if(!e.Query(key,"DisableAutoDaylightTimeSet",type,&disabled,size)&&(type==4||type==3))adjust=disabled==0;}
 U result=1;
 if(adjust){Q difference=previous-s.bias;Q old=e.Local(),updated=old+difference;e.SetLocal(updated);result=2;
  if(!Less(0,difference)){s.end=old;s.start=updated;e.Set(key,"NoChangeStart",3,&s.start,8);e.Set(key,"NoChangeEnd",3,&s.end,8);}
 }
 e.Close(key);return result;
}
// Windows 98 reads the same cached flag as GetTimeZoneInformation.
inline U GetDaylight(const State&s){return s.flag==2?1:0;}
// the original setter ignores its argument and returns1.
// This compatibility no-op must not change the zone, bias, clock or registry.
inline U SetDaylight(State&,U){return 1;}
// Windows 98 copies the cached structure; it does not recompute DST.
inline U Get(const State&s,Zone&out){out=s.zone;return s.flag;}
// Registry writes are ordered and may leave partial persisted changes.
template<class E> U Set(E&e,State&s,const Zone&z){
 if(Classify(e,z)==~0u){e.Error(87);return 0;}U key=e.Open(true);if(!key)return 0;e.Lock();U error=0;
 auto write=[&](const char*name,U type,const void*p,U size){if(!error)error=e.Set(key,name,type,p,size);};
 write("Bias",3,&z.bias,4);write("StandardBias",3,&z.standard_bias,4);write("DaylightBias",3,&z.daylight_bias,4);
 write("StandardStart",3,&z.standard,16);write("DaylightStart",3,&z.daylight,16);
 char name[33]={};if(!error){e.Name(z.daylight_name,name);write("DaylightName",1,name,33);}if(!error){e.Name(z.standard_name,name);write("StandardName",1,name,33);}
 U result=0;if(error)e.Error(error);else{s.zone=z;if(Check(e,s,0)||RefreshBias(e,s,key))result=1;}
 e.Unlock();e.Close(key);return result;
}
// The clock provider is the DOS date/time interface.
template<class E> U SetLocal(E&e,State&s,const SystemTime&value){U result=e.SetLocalFields(value);Check(e,s,0);return result;}
template<class E> U SetUtc(E&e,State&s,const SystemTime&value){Q time;if(!SystemToFile(value,time)){e.Error(13);return 0;}SystemTime local;FileToSystem(time-s.bias,local);return SetLocal(e,s,local)?1:0;}
}
