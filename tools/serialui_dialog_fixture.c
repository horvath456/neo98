// SPDX-License-Identifier: GPL-2.0-or-later
#include "kernel_comm_imports.h"
#define P(x) ((U)(x))
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
static unsigned char cc[52],saved[52];
void *memcpy(void*d,const void*s,unsigned n){unsigned char*a=d;const unsigned char*b=s;while(n--)*a++=*b++;return d;}
static U same(void){for(U i=0;i<52;++i)if(cc[i]!=saved[i])return 0;return 1;}
void mainCRTStartup(void){
 U n=52;CHECK(GetDefaultCommConfigA(P("COM1"),P(cc),P(&n)));memcpy(saved,cc,52);
 CHECK(!CommConfigDialogA(P("COM1"),0,P(cc))&&GetLastError()==1223&&same());
 CHECK(CommConfigDialogA(P("COM1"),0,P(cc)));
 CHECK(*(U*)(cc+12)==115200&&cc[26]==7&&cc[27]==2&&cc[28]==2);
 CHECK((*(U*)(cc+16)&0x3306)==0x2006);
 CHECK(CommConfigDialogA(P("COM1"),0,P(cc)));
 CHECK(*(U*)(cc+12)==9600&&cc[26]==8&&cc[27]==0&&cc[28]==0);
 CHECK((*(U*)(cc+16)&0x3306)==0x300);
 // Apply followed by Cancel retains the applied DCB, as the NE release callback does.
 CHECK(CommConfigDialogA(P("COM1"),0,P(cc))&&*(U*)(cc+12)==300);
 // The dialog never writes default configuration; only SetDefaultCommConfig does.
 n=52;CHECK(GetDefaultCommConfigA(P("COM1"),P(cc),P(&n))&&same());
 ExitProcess(0);
}
