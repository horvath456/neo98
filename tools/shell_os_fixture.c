// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U ordinal_437(U);API U GetModuleHandleA(const char*);API U GetProcAddress(U,const char*);API U RegCreateKeyExA(U,const char*,U,char*,U,U,void*,U*,U*);API U RegSetValueExA(U,const char*,U,U,const void*,U);API U RegCloseKey(U);API U RegDeleteKeyA(U,const char*);
static void check(int yes,U code){if(!yes)ExitProcess(code);}
void run_shell_os_test(void){U key=0,created=0;const char*path="System\\CurrentControlSet\\Control\\ProductOptions";check(!RegCreateKeyExA(0x80000002,path,0,0,0,0xf003f,0,&key,&created),41200);check(!RegSetValueExA(key,"ProductType",0,1,"Enterprise",11),41201);check(ordinal_437(0)==1,41202);
 // If initialization ran again after this write, its explicit non-ASCII
 // ProductType provider would fail. Cached original decisions must remain.
 const unsigned char changed[]={0xe9,0};check(!RegSetValueExA(key,"ProductType",0,1,changed,2),41203);const U expected[]={1,0,1,0,0,1,0,0,0,0,0,0,0};for(U n=0;n<13;++n)check(ordinal_437(n)==expected[n],41204+n);check(!ordinal_437(13)&&!ordinal_437(0xffffffff),41217);U module=GetModuleHandleA("SHLWAPI.DLL");U(__stdcall*byordinal)(U)=(void*)GetProcAddress(module,(const char*)437);check(byordinal&&byordinal(5)==1,41218);check(!RegCloseKey(key)&&!RegDeleteKeyA(0x80000002,path),41219);}
#ifdef SHELL_OS_STANDALONE
void mainCRTStartup(void){run_shell_os_test();ExitProcess(0);}
#endif
