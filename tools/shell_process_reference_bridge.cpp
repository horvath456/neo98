// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_process_reference.h"
#include <cstring>
#include <cstdio>
struct Environment {
 Win98ShellProcessReference::State&state;const unsigned*r;unsigned*out;
 void Publish(unsigned,unsigned p){out[2]=p;}
 void AddRef(unsigned p){out[3]=1;out[4]=p;out[5]=out[2];if(r[3])Win98ShellProcessReference::Set(state,r[4]);}
};
extern "C" void shell_process_reference(const unsigned*r,unsigned*out){memset(out,0,8*4);Win98ShellProcessReference::State s{r[1]};Environment e{s,r,out};out[2]=0xdeadbeef;out[0]=r[0]?Win98ShellProcessReference::Set(s,r[2]):Win98ShellProcessReference::Get(e,s,0x20000400);out[1]=s.object;}
#ifdef SHELL_PROCESS_REFERENCE_CORPUS_MAIN
int main(int argc,char**argv){if(argc!=2)return 1;FILE*f=fopen(argv[1],"rb");if(!f)return 2;unsigned r[6],expected[8],actual[8],n=0;while(fread(r,sizeof r,1,f)){if(fread(expected,sizeof expected,1,f)!=1)return 3;shell_process_reference(r,actual);if(memcmp(expected,actual,sizeof actual))return 4;++n;}fclose(f);printf("Shell process reference original corpus PASS %u cases\n",n);}
#endif
