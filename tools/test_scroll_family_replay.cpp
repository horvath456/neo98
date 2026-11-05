// SPDX-License-Identifier: GPL-2.0-or-later
#include "scroll_family_bridge.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace Win98Scroll;
static FILE*Open(const char*dir,const char*name){char p[1024];snprintf(p,sizeof p,"%s/%s-cases.bin",dir,name);FILE*f=fopen(p,"rb");assert(f);return f;}
template<class T>static T Read(FILE*f){T v;assert(fread(&v,sizeof v,1,f)==1);return v;}
int main(int argc,char**argv){assert(argc==2);unsigned total=0;FILE*f=Open(argv[1],"state");Bar b;
 while(fread(&b,sizeof b,1,f)==1){Info i=Read<Info>(f);U v=Read<U>(f),r=Read<U>(f),changed=Read<U>(f);Bar want=Read<Bar>(f);U wv=Read<U>(f),wr=Read<U>(f);assert(Update(b,i,v,r)==bool(changed)&&!memcmp(&b,&want,sizeof b)&&v==wv&&r==wr);++total;}fclose(f);
 f=Open(argv[1],"query");while(fread(&b,sizeof b,1,f)==1){Info i=Read<Info>(f);U tracking=Read<U>(f),tracked=Read<U>(f),result=Read<U>(f);Info want=Read<Info>(f);assert(Query(b,i,tracking,tracked)==result&&!memcmp(&i,&want,sizeof i));++total;}fclose(f);
 f=Open(argv[1],"valid");U size;while(fread(&size,4,1,f)==1){Info i{};i.size=size;i.mask=Read<U>(f);assert(Valid(i)==bool(Read<U>(f)));++total;}fclose(f);
 f=Open(argv[1],"geometry");while(fread(&b,sizeof b,1,f)==1){int rect[4];assert(fread(rect,sizeof rect,1,f)==1);U vertical=Read<U>(f),metric=Read<U>(f),result=Read<U>(f);Geometry want=Read<Geometry>(f),got{};assert(Calculate(b,rect[vertical?1:0],rect[vertical?3:2],rect[vertical?0:1],rect[vertical?2:3],metric,got)==bool(result));if(result)assert(!memcmp(&got,&want,sizeof got));++total;}fclose(f);
 f=Open(argv[1],"owner");Owner o;while(fread(&o,sizeof o,1,f)==1){U op=Read<U>(f),bar=Read<U>(f);Info i=Read<Info>(f);U v=Read<U>(f),visible=Read<U>(f),alloc=Read<U>(f),dc=Read<U>(f);Owner want=Read<Owner>(f);Effects we=Read<Effects>(f),got;scroll_owner(&o,op,bar,&i,v,visible,alloc,dc,&got);assert(!memcmp(&o,&want,sizeof o)&&!memcmp(&got,&we,sizeof got));++total;}fclose(f);
 f=Open(argv[1],"wrapper");U op;while(fread(&op,4,1,f)==1){U a[5];assert(fread(a,sizeof a,1,f)==1);ScrollWrapper e{};e.okay=Read<U>(f);e.result=Read<U>(f);e.minimum=Read<U>(f);e.maximum=Read<U>(f);U result=Read<U>(f),wl=Read<U>(f),wh=Read<U>(f),trace[12],low=0x1234,high=0x5678;assert(fread(trace,sizeof trace,1,f)==1);assert(scroll_wrapper(op,a,&e,&low,&high)==result&&low==wl&&high==wh&&!memcmp(trace,e.calls,sizeof trace));++total;}fclose(f);
 f=Open(argv[1],"draw");Geometry g;while(fread(&g,sizeof g,1,f)==1){U vertical=Read<U>(f),disabled=Read<U>(f),count=Read<U>(f);int want[15],out[15];assert(fread(want,sizeof want,1,f)==1);assert(scroll_draw(&g,vertical,disabled,out)==count&&!memcmp(want,out,sizeof out));++total;}fclose(f);
 printf("Scrollbar original replay ASan/UBSan PASS %u cases\n",total);
}
