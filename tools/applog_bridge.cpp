/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_applog.h"
#include <cstring>
struct E {
 unsigned now,calls=0,hash=2166136261u;
 unsigned NowHigh(){return now;}
 bool Exists(const char*p){const char*leaf=p;for(auto*s=p;*s;++s){hash=(hash^(unsigned char)*s)*16777619u;if(*s=='\\')leaf=s+1;}hash*=16777619u;++calls;return *leaf=='K';}
};
extern "C" unsigned load_index(unsigned char*bytes,unsigned size){Win98AppLog::Index index;return Win98AppLog::Load(index,bytes,size);}
extern "C" void prune(unsigned*state,unsigned char*records,unsigned char*strings,unsigned now){
 Win98AppLog::Index s;s.count=state[0];s.string_bytes=state[1];std::memcpy(s.groups,state+4,344);s.records=records;s.strings=strings;
 E e{now};Win98AppLog::Prune(s,e);state[0]=s.count;state[1]=s.string_bytes;std::memcpy(state+4,s.groups,344);state[90]=e.calls;state[91]=e.hash;
}
extern "C" unsigned save(unsigned*state,unsigned char*records,unsigned char*strings,unsigned fail,unsigned char*out,unsigned*meta){
 Win98AppLog::Index s;s.count=state[0];s.string_bytes=state[1];s.stamp[0]=state[2];s.stamp[1]=state[3];std::memcpy(s.groups,state+4,344);s.records=records;s.strings=strings;
 struct Writer {unsigned fail,calls=0,used=0,closed=0;unsigned char*out;
  bool Open(){return fail!=1;}void Close(){++closed;}
  unsigned Write(const void*p,unsigned n){++calls;if(fail==calls+1&&n)--n;std::memcpy(out+used,p,n);used+=n;return n;}
 }e{fail,0,0,0,out};Win98AppLog::Save(s,e);meta[0]=e.calls;meta[1]=e.closed;return e.used;
}
