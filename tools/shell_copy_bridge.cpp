// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_copy.h"
#include <cstring>
struct Env{unsigned char*memory;bool failure;unsigned allocated=0;bool task=false;unsigned Base=0x30000000;
 unsigned Get(unsigned p,unsigned n){unsigned v=0;memcpy(&v,memory+p-Base,n);return v;}
 void Put(unsigned p,unsigned v,unsigned n){memcpy(memory+p-Base,&v,n);}
 unsigned Allocate(unsigned n,bool com){allocated=n;task=com;return failure?0:Base+0x5000;}
 unsigned Wide(unsigned s,unsigned d,unsigned n){if(!s)return 0;unsigned count=0;do{unsigned c=Get(s++,1);if(c==0x80)c=0x20ac;if(d){if(count>=n)return 0;Put(d+count*2,c,2);}++count;if(!c)break;}while(true);return count;}
};
extern "C" unsigned shell_copy(unsigned op,unsigned*a,unsigned char*mem,unsigned fail,unsigned*alloc,unsigned*task){Env e{mem,fail!=0};unsigned r=Win98ShellCopy::Call(e,op,a);*alloc=e.allocated;*task=e.task;return r;}
