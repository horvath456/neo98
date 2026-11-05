// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_compare.h"
#include <cstring>
struct Env{unsigned char*m;unsigned fail,dbcs,compares=0,alloc=0,freed=0;
 unsigned Get(unsigned p,unsigned n){unsigned v=0;memcpy(&v,m+p-0x30000000,n);return v;}void Put(unsigned p,unsigned v,unsigned n){memcpy(m+p-0x30000000,&v,n);}
 unsigned Scratch(unsigned s){return 0x30005000+s*528;}
 unsigned Narrow(unsigned p,unsigned n,unsigned d,unsigned cap){unsigned count=0;if(int(n)<0){while(Get(p+count*2,2))++count;++count;}else count=n;if(!d)return count;if(cap<count)return 0;for(unsigned i=0;i<count;++i){unsigned c=Get(p+i*2,2);Put(d+i,c<256?c:'?',1);}return count;}
 unsigned Allocate(unsigned){return 0x30007000+alloc++*4096;}void Free(unsigned){++freed;}unsigned Locale(){return 0x409;}
 unsigned Compare(unsigned,unsigned flags,unsigned a,unsigned na,unsigned b,unsigned nb){++compares;if(fail==2||(fail==1&&compares==1))return 0;unsigned i=0;for(;;++i){unsigned x=(int(na)>=0&&i>=na)?0:Get(a+i,1),y=(int(nb)>=0&&i>=nb)?0:Get(b+i,1);if(flags&1){if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;}if(x!=y)return x<y?1:3;if(!x)return 2;}}
 unsigned LocalCompare(unsigned a,unsigned b,bool i){return Compare(0x400,i,a,~0u,b,~0u)-2;}
 bool Lead(unsigned c){return dbcs&&c==0x81;}bool Dbcs(){return dbcs;}
 unsigned Next(unsigned p){unsigned c=Get(p,1);return p+(!c?0:Lead(c)&&Get(p+1,1)?2:1);}
};
extern "C" unsigned shell_compare(unsigned op,unsigned*a,unsigned char*mem,unsigned failure,unsigned dbcs,unsigned*t){Env e{mem,failure,dbcs};unsigned r=Win98ShellCompare::Call(e,op,a);t[0]=e.compares;t[1]=e.alloc;t[2]=e.freed;return r;}
