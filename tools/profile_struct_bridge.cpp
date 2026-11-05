// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_profile_struct.h"
#include <cstdlib>
#include <cstring>
struct Wire {unsigned operation,size,missing,fail,result,length,allocations,frees,reads,writes,nullwrite;unsigned char data[4096],text[8192];};
extern "C" void profile_struct(Wire*w){struct Env:Win98Profile::StructEnvironment{Wire&w;Env(Wire&v):w(v){}unsigned char*Allocate(unsigned n)override{++w.allocations;return w.fail?nullptr:(unsigned char*)malloc(n);}void Free(unsigned char*p)override{++w.frees;free(p);}unsigned Read(unsigned char*p,unsigned short n)override{++w.reads;unsigned count=w.length<n?w.length:n-1;memcpy(p,w.text,count);p[count]=0;return count;}unsigned Write(const unsigned char*p)override{++w.writes;w.nullwrite=!p;if(p){w.length=strlen((const char*)p);memcpy(w.text,p,w.length+1);}return 7;}}e(*w);w->result=w->operation?Win98Profile::ReadStruct(e,w->data,w->size):Win98Profile::WriteStruct(e,w->missing==1?nullptr:w->data,w->size);}

#ifdef PROFILE_REPLAY
#include <cstdio>
#include <cstring>
#include <cassert>
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);Wire value,expected;unsigned count=0;while(fread(&value,sizeof value,1,f)){assert(fread(&expected,sizeof expected,1,f)==1);profile_struct(&value);assert(!memcmp(&value,&expected,sizeof value));++count;}fclose(f);printf("PROFILE sanitizer original replays PASS %u\n",count);}
#endif
