// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_crt_system_clock.h"
#include <string.h>
extern "C" unsigned long long crt_difference(unsigned a,unsigned b){return Win98CrtSystemClock::Difference(a,b);}
extern "C" unsigned crt_clock(unsigned long long now,unsigned long long start){return Win98CrtSystemClock::Clock(now,Win98CrtSystemClock::Start(start));}
extern "C" unsigned crt_system_wrapper(unsigned set,const int*in,const unsigned short*clock,unsigned ms,unsigned change,unsigned success,unsigned error,int*out,unsigned short*setclock,int*normalized){
 struct E{Win98Time::SystemTime clock,setclock{};unsigned change,success,error;int*normalized;
 auto Local(){return clock;}void Normalize(Win98CrtTime::Tm&t){memcpy(normalized,&t,36);if(change){t.sec=3;t.min=2;t.hour=1;t.mon=6;t.mday=8;t.year=99;t.wday=4;t.yday=188;t.isdst=1;}}
 bool SetLocal(const Win98Time::SystemTime&t){setclock=t;return success!=0;}unsigned Error(){return error;}
 }e{{},{},change,success,error,normalized};memcpy(&e.clock,clock,16);Win98CrtTime::Tm t;memcpy(&t,in,36);unsigned result=set?Win98CrtSystemClock::Set(e,t,ms):Win98CrtSystemClock::Get(e,t);memcpy(out,&t,36);memcpy(setclock,&e.setclock,16);return result;
}
