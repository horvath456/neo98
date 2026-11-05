// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;typedef unsigned long long Q;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U LoadLibraryA(const char*);API U FreeLibrary(U);API U GetProcAddress(U,const char*);API void Sleep(U);
API U RegCreateKeyExA(U,const char*,U,char*,U,U,void*,U*,U*);API U RegSetValueExA(U,const char*,U,U,const void*,U);API U RegCloseKey(U);API U RegDeleteKeyA(U,const char*);
static void check(int yes,U n){if(!yes)ExitProcess(n);}
void run_shell_stopwatch_test(void){
 const char*path="Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Performance";
 U dll=LoadLibraryA("SHLWAPI.DLL");check(dll!=0,43600);U(__stdcall*mode)(void)=(void*)GetProcAddress(dll,(const char*)241);Q(__stdcall*clock)(void)=(void*)GetProcAddress(dll,(const char*)250);check(mode&&clock&&!mode(),43601);
 Q before=clock();Sleep(2);Q after=clock();check(after>=before&&after-before<10000,43602);
 U key=0,created=0,value=1;check(!RegCreateKeyExA(0x80000002,path,0,0,0,0xf003f,0,&key,&created),43603);check(!RegSetValueExA(key,"Mode",0,4,&value,4),43604);check(!mode(),43605);check(!RegCloseKey(key)&&!RegDeleteKeyA(0x80000002,path),43606);
 check(FreeLibrary(dll),43607);dll=LoadLibraryA("SHLWAPI.DLL");check(dll!=0,43608);mode=(void*)GetProcAddress(dll,(const char*)241);clock=(void*)GetProcAddress(dll,(const char*)250);check(mode&&clock&&!mode()&&clock()>=after,43609);check(FreeLibrary(dll),43610);
}
#ifdef SHELL_STOPWATCH_STANDALONE
void mainCRTStartup(void){run_shell_stopwatch_test();ExitProcess(0);}
#endif
