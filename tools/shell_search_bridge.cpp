// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_search.h"
#include <cassert>
#include <cstdio>
#include <cstring>
struct Wire {unsigned operation,dbcs,cpinfo,args[3],result,error;unsigned char text[512],other[512];};
extern "C" void shell_search(Wire*w){struct Env:Win98ShellSearch::Environment{Wire&w;Env(Wire&v):w(v){}unsigned Byte(unsigned p)override{assert(p>=0x20000&&p<0x20200||p>=0x21000&&p<0x21200);return p<0x21000?w.text[p-0x20000]:w.other[p-0x21000];}unsigned Word(unsigned p)override{return Byte(p)|Byte(p+1)<<8;}bool Lead(unsigned c)override{return w.dbcs&&((c>=0x81&&c<=0x9f)||(c>=0xe0&&c<=0xfc));}unsigned Next(unsigned p)override{unsigned c=Byte(p);return !c?p:p+(Lead(c)&&Byte(p+1)?2:1);}bool Dbcs()override{return w.dbcs&&w.cpinfo;}void EndBeyondTerminator()override{w.error=1;}}e(*w);w->result=Win98ShellSearch::Call(e,w->operation,w->args);}
#ifdef SHELL_SEARCH_REPLAY
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);Wire a,b;unsigned count=0;while(fread(&a,sizeof a,1,f)){assert(fread(&b,sizeof b,1,f));shell_search(&a);assert(!memcmp(&a,&b,sizeof a));++count;}fclose(f);printf("SHELL SEARCH sanitizer replays PASS %u\n",count);}
#endif
