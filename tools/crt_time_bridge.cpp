// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_crt_time.h"
#include <string.h>
extern "C" unsigned crt_time_run(unsigned op,const int*a,int*out){
 namespace C=Win98CrtTime;C::State s;s.timezone=a[0];s.daylight=a[1];s.dstbias=a[2];s.os_zone=a[3];memcpy(&s.zone,a+16,sizeof s.zone);C::Tm t;memcpy(&t,a+4,36);unsigned result=0;
 if(op==0)result=C::Gmtime(a[4],t);if(op==1)result=C::Localtime(s,a[4],t);if(op==2)result=C::Mktime(s,t);if(op==3)return C::Asctime(t,(char*)out);if(op==4)return C::Asctime(t,(unsigned short*)out);if(op==5)result=C::IsDst(s,t);
 if(op==6){result=C::ParseTz(s,(const char*)(a+4));out[0]=s.timezone;out[1]=s.daylight;out[2]=s.dstbias;memcpy(out+4,s.standard,64);memcpy(out+20,s.summer,64);return result;}
 if(op==7){C::Zone(s,s.zone);out[0]=s.timezone;out[1]=s.daylight;out[2]=s.dstbias;return 1;}
 if(op==8){Win98Time::SystemTime clock{};clock.year=a[4];clock.month=a[5];clock.day=a[6];clock.hour=a[7];clock.minute=a[8];clock.second=a[9];return C::FromLocal(s,clock,a[10]);}
 memcpy(out,&t,36);return result;
}
