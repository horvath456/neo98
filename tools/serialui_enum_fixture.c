// SPDX-License-Identifier: GPL-2.0-or-later
#include "enum_imports.h"
#define P(x) ((U)(x))
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
U __stdcall call16(U,U,U,U);
__asm__(".text\n.globl _call16@16\n_call16@16:\n"
 " push %ebp\n mov %esp,%ebp\n sub $64,%esp\n mov 8(%ebp),%edx\n"
 " pushl 12(%ebp)\n pushl 16(%ebp)\n pushl 20(%ebp)\n call *__imp__QT_Thunk@0\n"
 " shll $16,%eax\n shrdl $16,%edx,%eax\n leave\n ret $16\n");
static unsigned char device[]={0,0,0,0,0,0,'N','e','o','9','8',' ','e','n','u','m',' ','t','e','s','t',0};
static unsigned char header16[38];
static U page,cc[13],n=52;
void mainCRTStartup(void){
 U module=LoadLibrary16(P("SERIALUI"));CHECK(module>=32);
 U enumerate=GetProcAddress16(module,2),collector=(enumerate&0xffff0000u)|0x2ae;
 U dev=MapLS(P(device)),header=MapLS(P(header16)),array=MapLS(P(&page));CHECK(dev&&header&&array);
 *(U*)(header16+26)=array;
 CHECK(call16(enumerate,dev,collector,header)==1&&page&&*(unsigned short*)(header16+20)==1);
 U first=page;CHECK(call16(enumerate,dev,collector,header)==1&&page==first&&*(unsigned short*)(header16+20)==1);
 CHECK(GetDefaultCommConfigA(P("COM1"),P(cc),P(&n)));U old_baud=cc[3];
 U sheet[]={40,1,0,0,0,P("Enumerated COM1"),1,0,P(&page),0};
 CHECK(PropertySheetA(P(sheet))==0);n=52;CHECK(GetDefaultCommConfigA(P("COM1"),P(cc),P(&n))&&cc[3]==old_baud);
 *(unsigned short*)(header16+20)=0;page=0;
 CHECK(call16(enumerate,dev,collector,header)==1&&page);
 CHECK(PropertySheetA(P(sheet))==1);n=52;CHECK(GetDefaultCommConfigA(P("COM1"),P(cc),P(&n))&&cc[3]==115200);
 UnMapLS(dev);UnMapLS(header);UnMapLS(array);FreeLibrary16(module);ExitProcess(0);
}
