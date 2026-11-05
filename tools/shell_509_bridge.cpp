// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_registry_ordinal.h"
#include <cstdio>
#include <cstring>
#include <cassert>
using U=unsigned;
struct Wire{U ordinal,args[6],open_result,open_write,open_handle,query_result,query_type,query_size,allocate_fail,multi_fail,result,last_error,event_count,events[256];unsigned char memory[0x5000];};
static Wire *wire;
using Read=U(*)(U);using Write=void(*)(U,U);static Read reader;static Write writer;
struct Env{
 U used=0;unsigned char scratch[1024]{};
 U Read8(U p){if(reader)return reader(p);if(p>=0x28000&&p<0x28400)return scratch[p-0x28000];assert(p>=0x20000&&p<0x25000);return wire->memory[p-0x20000];}
 void Write8(U p,U v){if(writer){writer(p,v&255);return;}if(p>=0x28000&&p<0x28400){scratch[p-0x28000]=v;return;}assert(p>=0x20000&&p<0x25000);wire->memory[p-0x20000]=v;}
 U Read16(U p){return Read8(p)|(Read8(p+1)<<8);}U Read32(U p){return Read16(p)|(Read16(p+2)<<16);}void Write32(U p,U v){for(U i=0;i<4;++i)Write8(p+i,v>>(i*8));}
 struct Scratch{Env&e;U address,n;Scratch(Env&v,U size):e(v),address(0x28000+e.used),n((size+3)&~3u){assert(e.used+n<=1024);memset(e.scratch+e.used,0,n);e.used+=n;}~Scratch(){e.used-=n;}};
 U Length(U p,bool wide=false){U n=0;while(wide?Read16(p+n*2):Read8(p+n))++n;return n;}
 U Hash(U p,bool wide=false){U h=2166136261;if(!p)return 0;for(U c=wide?Read16(p):Read8(p);c;c=wide?Read16(p):Read8(p)){h=(h^c)*16777619;p+=wide?2:1;}return h;}
 template<class...T>void Event(T...v){U a[]={U(v)...};for(U x:a){assert(wire->event_count<256);wire->events[wire->event_count++]=x;}}
 U Open(bool wide,U root,U path,U options,U access,U output){Event(1,wide,root,Hash(path,wide),options,access);if(wire->open_write)Write32(output,wire->open_handle);return wire->open_result;}
 U Close(U key){Event(2,key);return key==0x1234?6:0;}
 U QueryAnsi(U key,U name,U reserved,U type,U data,U size){U cap=size?Read32(size):0;Event(3,key,Hash(name),reserved,!!type,!!data,!!size,cap);if(type)Write32(type,wire->query_type);if(size)Write32(size,wire->query_size);U result=wire->query_result;if(!result&&data){if(cap<wire->query_size)result=234;else for(U i=0;i<wire->query_size;++i)Write8(data+i,Read8(0x20500+i));}return result;}
 U Query(bool wide,U key,U name,U reserved,U type,U data,U size){return wide?Win98ShellRegistryOrdinal::QueryWide(*this,key,name,reserved,type,data,size):QueryAnsi(key,name,reserved,type,data,size);}
 U Allocate(U flags,U size){Event(4,flags,size);if(wire->allocate_fail)return 0;assert(size<=0x1000);for(U i=0;i<(size<2?2:size);++i)Write8(0x24000+i,0);return 0x24000;}
 U Free(U p){Event(5,p);return 0;}
 U WideToAnsi(U input,U output,U capacity){U n=Length(input,true)+1;Event(6,Hash(input,true),capacity,n);if(n>capacity)return 0;for(U i=0;i<n;++i)Write8(output+i,Read16(input+i*2)&255);return n;}
 U MultiToWide(U input,U count,U output,U capacity){U n=count==~0u?Length(input)+1:count;Event(7,Hash(input),count,!!output,capacity);if(wire->multi_fail)return 0;if(!output)return n;if(capacity<n)return 0;for(U i=0;i<n;++i){Write8(output+i*2,Read8(input+i));Write8(output+i*2+1,0);}return n;}
 void WideCopy(U to,U from){for(;;to+=2,from+=2){U ch=Read16(from);Write8(to,ch);Write8(to+1,ch>>8);if(!ch)return;}}
}env;
extern "C" void shell_509_callbacks(Read r,Write w){reader=r;writer=w;}
extern "C" void shell_509_begin(Wire*w){wire=w;env=Env{};}
extern "C" void shell_509_run(Wire*w){shell_509_begin(w);reader=nullptr;writer=nullptr;w->result=Win98ShellRegistryOrdinal::Call(env,w->ordinal,w->args);}
extern "C" U shell_509_provider(U id,const U*a){switch(id){case 0:return env.Open(a[0],a[1],a[2],a[3],a[4],a[5]);case 1:return env.Close(a[0]);case 2:return env.QueryAnsi(a[0],a[1],a[2],a[3],a[4],a[5]);case 3:return env.Allocate(a[0],a[1]);case 4:return env.Free(a[0]);case 5:return env.WideToAnsi(a[0],a[1],a[2]);case 6:return env.MultiToWide(a[0],a[1],a[2],a[3]);}assert(false);return 0;}
#ifdef SHELL_509_REPLAY
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);Wire a,b;U count=0;while(fread(&a,sizeof a,1,f)){assert(fread(&b,sizeof b,1,f));shell_509_run(&a);assert(!memcmp(&a,&b,sizeof a));++count;}fclose(f);printf("SHELL509 sanitizer replays PASS %u\n",count);}
#endif
