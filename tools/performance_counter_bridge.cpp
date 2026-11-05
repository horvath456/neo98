// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_performance_counter.h"
using U=unsigned;using Q=unsigned long long;
extern "C" Q pit_query(const U*c,U*out){Win98Performance::PitState s{c[0],c[1],c[2],c[3]};struct E{U sample;U LatchPit(){return sample;}}e{c[4]};Q value=Win98Performance::ReadPit(e,s);out[0]=s.last;return value;}
extern "C" Q microseconds(Q us){return Win98Performance::FromMicroseconds(us);}
extern "C" U public_query(U frequency,U service_result,U low,U high,U*out){struct E{U result,low,high,*out,n=0;bool Probe(U){return true;}void Error(U){}void Write32(U p,U value){out[n++]=p;out[n++]=value;}U VwinQuery(U p){out[n++]=0x2a0044;out[n++]=p;return result;}}e{service_result,low,high,out};return Win98Performance::Query(e,0x201000,frequency);}
extern "C" U vwin_query(U low,U high,U*out){struct E{U*out,n=0;void Write32(U p,U value){out[n++]=p;out[n++]=value;}}e{out};return Win98Performance::Pack(e,0x201000,(Q(high)<<32)|low);}
