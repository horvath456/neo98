/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* Original CP1252 CTYPE1/2/3 values and native public API lifetime checks. */
typedef unsigned U;
typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
int API GetStringTypeA(U,U,const char*,int,W*);
int API GetStringTypeExA(U,U,const char*,int,W*);
int API GetStringTypeW(U,const W*,int,W*);
int API GetStringTypeExW(U,U,const W*,int,W*);
void API SetLastError(U);
U API GetLastError(void);
void API ExitProcess(U);
API void *VirtualAlloc(void*,U,U,U);
int API VirtualProtect(void*,U,U,U*);
int API VirtualFree(void*,U,U);
static void check(int value,U code){if(!value)ExitProcess(code);}
void run_character_type_test(void){
 const char input[]={'A','a','0',' ','\t',(char)0xe9,0};
 const W expected[3][7]={{0x181,0x182,0x84,0x48,0x68,0x102,0x20},{1,1,3,10,9,1,0},{0x8040,0x8040,0x40,0x48,8,0x8003,0}};
 const U kinds[]={1,2,4};W out[10],other[10];
 for(U kind=0;kind<3;++kind){
  for(U i=0;i<10;++i)out[i]=other[i]=0x9898;
  SetLastError(98);
  check(GetStringTypeA(0x409,kinds[kind],input,-2,out)&&GetLastError()==98,13300);
  check(GetStringTypeExA(9,kinds[kind],input,7,other),13301);
  for(U i=0;i<7;++i)check(out[i]==expected[kind][i]&&other[i]==out[i],13302);
  check(out[7]==0x9898&&other[7]==0x9898,13303);
 }
 char overlap[16]={'A','a','0',' ',0};
 check(!GetStringTypeA(0x409,1,overlap,4,(W*)overlap)&&GetLastError()==87&&overlap[0]=='A',13304);
 check(!GetStringTypeA(0x409,1,input,0,out)&&GetLastError()==87,13305);
 check(!GetStringTypeA(0x409,3,input,7,out)&&GetLastError()==87,13306);
 check(!GetStringTypeA(0x411,1,input,7,out)&&GetLastError()==87,13307); /* Shared locale provider has only US data. */
 const W wide[]={'A',0};
 check(!GetStringTypeW(1,wide,2,out)&&GetLastError()==120,13308);
 check(!GetStringTypeExW(0x409,1,wide,2,out)&&GetLastError()==120,13309);
 void *page=VirtualAlloc(0,4096,0x3000,4);U old=0;check(page!=0,13310);
 check(VirtualProtect(page,4096,2,&old),13311);
 check(!GetStringTypeA(0x409,1,input,7,(W*)page)&&GetLastError()==87,13312);
 check(VirtualFree(page,0,0x8000),13313);
}
#ifdef CHARACTER_TYPE_STANDALONE
void mainCRTStartup(void){run_character_type_test();ExitProcess(0);}
#endif
