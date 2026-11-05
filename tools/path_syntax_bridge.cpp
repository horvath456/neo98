// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_path_syntax.h"
#include <cassert>
#include <cstdio>
#include <cstring>
struct Wire{unsigned op,dbcs,cpinfo,args[2],result;unsigned char text[2048],other[2048];};
extern "C" void path_syntax(Wire*w){struct Env:Win98ShellPathSyntax::Environment{Wire&w;Env(Wire&v):w(v){}unsigned char&At(unsigned p){assert(p>=0x20000&&p<0x20800||p>=0x21000&&p<0x21800);return p<0x21000?w.text[p-0x20000]:w.other[p-0x21000];}unsigned Byte(unsigned p)override{return At(p);}unsigned Word(unsigned p)override{return Byte(p)|Byte(p+1)<<8;}void Put(unsigned p,unsigned c,bool wide)override{At(p)=c;if(wide)At(p+1)=c>>8;}void Move(unsigned dst,unsigned src,unsigned n)override{unsigned char copy[2048];assert(n<=sizeof copy);for(unsigned i=0;i<n;++i)copy[i]=Byte(src+i);for(unsigned i=0;i<n;++i)At(dst+i)=copy[i];}bool Lead(unsigned c)override{return w.dbcs&&((c>=0x81&&c<=0x9f)||(c>=0xe0&&c<=0xfc));}unsigned Next(unsigned p)override{unsigned c=Byte(p);return !c?p:p+(Lead(c)&&Byte(p+1)?2:1);}bool Dbcs()override{return w.dbcs&&w.cpinfo;}void EndBeyondTerminator()override{assert(false);}unsigned Prev(unsigned first,unsigned at,bool wide)override{if(wide)return first==at?at:at-2;unsigned prev=first;while(first<at){prev=first;unsigned next=Next(first);if(next==first)break;first=next;}return prev;}}e(*w);w->result=Win98ShellPathSyntax::Call(e,w->op,w->args);}
#ifdef PATH_SYNTAX_REPLAY
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);Wire a,b;unsigned n=0;while(fread(&a,sizeof a,1,f)){assert(fread(&b,sizeof b,1,f));path_syntax(&a);assert(!memcmp(&a,&b,sizeof a));++n;}fclose(f);printf("PATH SYNTAX sanitizer replays PASS %u\n",n);}
#endif
