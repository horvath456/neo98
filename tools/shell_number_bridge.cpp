// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_number.h"
#include <cstring>
struct Env{unsigned char*m;unsigned failure,narrow=0,allocated=0,freed=0,next=0;
 unsigned Get(unsigned p,unsigned n){unsigned v=0;memcpy(&v,m+p-0x30000000,n);return v;}
 void Put(unsigned p,unsigned v){memcpy(m+p-0x30000000,&v,4);}
 unsigned Next(unsigned p){++next;unsigned c=Get(p,1);return p+(c?1:0);}
 unsigned Stack(){return 0x30004000;}
 unsigned Narrow(unsigned s,unsigned d,unsigned n){++narrow;if((failure==1&&!d)||(failure==2&&d))return 0;unsigned count=0,c;do{c=Get(s+count*2,2);if(d){if(count>=n)return 0;m[d+count-0x30000000]=c<=255?c:'?';}++count;}while(c);return count;}
 unsigned Allocate(unsigned n){allocated=n;return failure==3?0:0x30006000;}
 void Free(unsigned){++freed;}
};
extern "C" unsigned shell_number(unsigned op,unsigned*a,unsigned char*mem,unsigned fail,unsigned*trace){Env e{mem,fail};unsigned r=Win98ShellNumber::Call(e,op,a);trace[0]=e.narrow;trace[1]=e.allocated;trace[2]=e.freed;trace[3]=e.next;return r;}
