// SPDX-License-Identifier: GPL-2.0-or-later
// Original MSVCRT clock and system-clock wrappers.
#pragma once
#include "win98_crt_time.h"
namespace Win98CrtSystemClock {
using U=unsigned;using Q=unsigned long long;using Tm=Win98CrtTime::Tm;
// Subtract with DWORD precision, then convert as signed. Every possible
// result is exactly representable in binary64; this builds those bits directly.
inline Q Difference(U a,U b){
 U value=a-b;if(!value)return 0;const bool negative=(value&0x80000000u)!=0;U magnitude=negative?0u-value:value;
 unsigned power=0;for(U n=magnitude;n>1;n>>=1)++power;
 return (Q(negative)<<63)|(Q(power+1023)<<52)|((Q(magnitude)<<(52-power))&0x000fffffffffffffull);
}
// Process attachment captures a FILETIME. The clock query
// subtracts modulo64, divides unsigned by10000, and exposes the low DWORD.
inline Q Start(Q filetime){return filetime;}
inline U Clock(Q now,Q start){return U((now-start)/10000);}
// The existing yday survives when mktime fails and its result is ignored.
template<class E> U Get(E&e,Tm&t){
 auto c=e.Local();t.sec=c.second;t.isdst=-1;t.min=c.minute;t.hour=c.hour;t.mday=c.day;t.mon=int(c.month)-1;t.year=int(c.year)-1900;t.wday=c.weekday;
 e.Normalize(t);return c.milliseconds;
}
// Normalization failure does not prevent SetLocalTime. The
// original leaves weekday uninitialized; the DOS date setter ignores it.
template<class E> U Set(E&e,Tm&t,U milliseconds){
 e.Normalize(t);Win98Time::SystemTime c{};c.year=static_cast<unsigned short>(U(t.year)+1900);c.month=static_cast<unsigned short>(U(t.mon)+1);c.day=static_cast<unsigned short>(t.mday);c.hour=static_cast<unsigned short>(t.hour);c.minute=static_cast<unsigned short>(t.min);c.second=static_cast<unsigned short>(t.sec);c.milliseconds=static_cast<unsigned short>(milliseconds);
 return e.SetLocal(c)?0:e.Error();
}
struct Export{const char*name;U argc;};constexpr Export Exports[]={{"difftime",2},{"clock",0},{"_getsystime",1},{"_setsystime",2}};
}
