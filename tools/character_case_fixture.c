// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API void SetLastError(U);API U GetModuleHandleA(const char*);API U GetProcAddress(U,const char*);API void*VirtualAlloc(void*,U,U,U);API U VirtualProtect(void*,U,U,U*);API U VirtualFree(void*,U,U);
API char*CharUpperA(char*);API char*CharLowerA(char*);API U CharUpperBuffA(char*,U);API U CharLowerBuffA(char*,U);API U CharUpperW(U);API U CharLowerW(U);API U CharUpperBuffW(U,U);API U CharLowerBuffW(U,U);
static void check(U okay,U code){if(!okay)ExitProcess(code);}static U same(const char*a,const char*b,U n){for(U i=0;i<n;++i)if(a[i]!=b[i])return 0;return 1;}
void run_character_case_test(void){
 SetLastError(98);check(CharUpperA(0)==0&&GetLastError()==98,30400);check((U)CharUpperA((char*)'a')=='A',30401);check((U)CharLowerA((char*)0x1241)=='a',30402);check((U)CharUpperA((char*)0xe4)==0xc4&&(U)CharLowerA((char*)0xc4)==0xe4,30403);check((U)CharUpperA((char*)0xdf)==0xdf&&(U)CharUpperA((char*)0xff)==0x9f,30404);
 char text[]="aBc\xe4\xdf\x9a\xff";check(CharUpperA(text)==text&&same(text,"ABC\xc4\xdf\x8a\x9f",8),30405);check(CharLowerA(text)==text&&same(text,"abc\xe4\xdf\x9a\xff",8),30406);
 char embedded[]={'a','b',0,'c','d',0x55};check(CharUpperBuffA(embedded,5)==5&&same(embedded,"AB\0CD\x55",6),30407);check(CharLowerBuffA(embedded,4)==4&&same(embedded,"ab\0cD\x55",6),30408);
 char large[]="abcdefghijklmnopqrstuvwxyz1234";check(CharUpperA(large)==large&&same(large,"ABCDEFGHIJKLMNOPQRSTUVWXYZ1234",31),30409);check(CharLowerBuffA(large,30)==30&&same(large,"abcdefghijklmnopqrstuvwxyz1234",31),30410);
 char empty[2]={0,0x55};check(CharUpperA(empty)==empty&&empty[1]==0x55,30411);SetLastError(99);check(CharUpperBuffA((char*)0xdeadc0de,0)==0&&GetLastError()==99,30412);check(CharLowerBuffA(0,15)==0&&GetLastError()==99,30413);
 SetLastError(0);check(!CharUpperA((char*)0xdeadc0de)&&GetLastError()==87,30414);
 U user=GetModuleHandleA("USER32.DLL");typedef char*(__stdcall*Scalar)(char*);typedef U(__stdcall*Buffer)(char*,U);Scalar upper=(Scalar)GetProcAddress(user,(const char*)50),lower=(Scalar)GetProcAddress(user,(const char*)34);Buffer upperbuffer=(Buffer)GetProcAddress(user,(const char*)51),lowerbuffer=(Buffer)GetProcAddress(user,(const char*)35);check(upper&&lower&&upperbuffer&&lowerbuffer,30415);check((U)upper((char*)'z')=='Z'&&(U)lower((char*)'Z')=='z',30416);check(upperbuffer(large,30)==30&&lowerbuffer(large,30)==30,30417);
 check(!CharUpperW(0xdeadc0de)&&GetLastError()==120,30418);check(!CharLowerW(0xdeadc0de)&&GetLastError()==120,30419);check(!CharUpperBuffW(0xdeadc0de,17)&&GetLastError()==120,30420);check(!CharLowerBuffW(0xdeadc0de,17)&&GetLastError()==120,30421);
 char*p=(char*)VirtualAlloc(0,4096,0x3000,4);U previous=0;check(p!=0,30422);p[0]='a';p[1]=0;check(VirtualProtect(p,4096,2,&previous),30423);SetLastError(0);check(!CharUpperA(p)&&GetLastError()==87&&p[0]=='a',30424);check(VirtualProtect(p,4096,4,&previous),30425);check(CharUpperA(p)==p&&p[0]=='A',30426);check(VirtualFree(p,0,0x8000),30427);
}
#ifdef CHARACTER_CASE_STANDALONE
void mainCRTStartup(void){run_character_case_test();ExitProcess(0);}
#endif
