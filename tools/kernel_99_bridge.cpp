/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_timezone.h"
#include <cstring>
#include <cstdio>
#include <cassert>
using namespace Win98Timezone;
struct Entry {U type,size,error;unsigned char data[64];};
struct Wire {U operation,argument;State state;Zone input;SystemTime rule;Q now,output;U result,error,open_error,set_error,set_calls,clock_error,event_count;Entry entries[10];U events[2048];};
const char*names[]={"Bias","StandardBias","DaylightBias","ActiveTimeBias","DaylightFlag","DisableAutoDaylightTimeSet","NoChangeStart","NoChangeEnd","StandardStart","DaylightStart","DaylightName","StandardName"};
struct Env{
 Wire&w;void event(U code,U a=0,U b=0,U c=0){assert(w.event_count+4<=2048);U*p=w.events+w.event_count;*p++=code;*p++=a;*p++=b;*p=c;w.event_count+=4;}
 U index(const char*n){for(U i=0;i<12;++i)if(!strcmp(n,names[i]))return i;assert(false);return 0;}
 Q Local(){event(1);return w.now;}
 U Open(bool create){event(2,create);return w.open_error?0:1;}
 void Close(U key){event(3,key);}void Delete(U key,const char*n){event(4,key,index(n));}
 U Query(U key,const char*n,U&type,void*out,U&size){U at=index(n);event(5,at,size);auto&v=w.entries[at];type=v.type;U cap=size;size=v.size;if(!v.error&&v.size<=cap)memcpy(out,v.data,v.size);return v.error?v.error:v.size>cap?234:0;}
 U Set(U,const char*n,U type,const void*p,U size){U at=index(n);event(6,at,type,size);++w.set_calls;if(w.set_error&&w.set_calls==w.set_error)return 5;if(at<10){auto&v=w.entries[at];v.type=type;v.size=size;v.error=0;memcpy(v.data,p,size);}return 0;}
 void Error(U e){event(7,e);w.error=e;}void BiasChanged(){event(8);}void Lock(){event(9);}void Unlock(){event(10);}
 void Name(const W*in,char*out){event(11);for(U i=0;i<32;++i)out[i]=char(in[i]);out[32]=0;}
 U SetLocalFields(const SystemTime&t){event(12,t.year|(U(t.month)<<16),t.day|(U(t.hour)<<16),t.minute|(U(t.second)<<16));Q out;if(w.clock_error||!SystemToFile(t,out)){w.error=87;return 0;}w.now=out-(out%100000);return 1;}
 void SetLocal(Q time){SystemTime t;FileToSystem(time,t);SetLocalFields(t);}
};
extern "C" void run(Wire*w){Env e{*w};switch(w->operation){case 0:w->result=Transition(w->rule,w->output,w->now,w->argument);break;case 1:w->result=Classify(e,w->input);break;case 2:w->result=Check(e,w->state,w->argument);break;case 3:w->result=RefreshBias(e,w->state,1);break;case 4:Reset(e,w->state);w->result=0;break;case 5:w->result=Set(e,w->state,w->input);break;case 6:w->result=Get(w->state,w->input);break;case 7:w->result=SetLocal(e,w->state,w->rule);break;case 8:w->result=SetUtc(e,w->state,w->rule);break;}}
#ifdef REPLAY
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);Wire a,b;unsigned n=0;while(fread(&a,sizeof a,1,f)==1){assert(fread(&b,sizeof b,1,f)==1);run(&a);assert(!memcmp(&a,&b,sizeof a));++n;}fclose(f);printf("KERNEL99 sanitizer replay PASS %u\n",n);}
#endif
