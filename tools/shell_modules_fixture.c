// SPDX-License-Identifier: GPL-2.0-or-later
#define API __declspec(dllimport) __stdcall
typedef unsigned U;typedef unsigned short W;
API void ExitProcess(U);API U GetModuleHandleA(const char*);API U GetModuleHandleW(const W*);API U GetModuleFileNameA(U,char*,U);API U GetLastError(void);API void SetLastError(U);API U FreeLibrary(U);API U GetProcAddress(U,const char*);
API U ordinal_83(const W*);API U ordinal_80(U,W*,U);API U ordinal_105(const W*,U,U);
static void check(int yes,U n){if(!yes)ExitProcess(n);}
void run_shell_modules_test(void){
 static const W name[]={'K','E','R','N','E','L','3','2','.','D','L','L',0};
 U kernel=GetModuleHandleA("KERNEL32.DLL");check(kernel&&ordinal_83(name)==kernel,42000);check(ordinal_83(0)==GetModuleHandleA(0),42001);
 U loaded=ordinal_105(name,0x1234,8);check(loaded==kernel,42002);check(FreeLibrary(loaded),42003);
 W longname[601];for(U i=0;i<600;++i)longname[i]='x';longname[600]=0;SetLastError(99);check(!ordinal_105(longname,0,8)&&GetLastError()==161,42004);
 longname[260]=0;check(!ordinal_105(longname,0,0)&&GetLastError()==161,42005);
 char ansi[260];W wide[300];U n=GetModuleFileNameA(0,ansi,260);check(n>0&&n<260,42006);for(U i=0;i<300;++i)wide[i]=0xa5a5;
 check(ordinal_80(0,wide,300)==n+1,42007);for(U i=0;i<=n;++i)check(wide[i]==(unsigned char)ansi[i],42008);check(wide[n+1]==0xa5a5,42009);
 for(U i=0;i<300;++i)wide[i]=0xa5a5;check(ordinal_80(0,wide,4)==4,42010);check(wide[0]==(unsigned char)ansi[0]&&wide[1]==(unsigned char)ansi[1]&&wide[2]==(unsigned char)ansi[2]&&!wide[3]&&wide[4]==0xa5a5,42011);
 check(!ordinal_80(0,0,260),42012);wide[0]=0xa5a5;check(!ordinal_80(0,wide,0)&&wide[0]==0xa5a5,42013);
 SetLastError(99);check(ordinal_80(0xdeadbeef,wide,260)==1&&!wide[0]&&GetLastError()==87,42014);
 SetLastError(99);check(!GetModuleHandleW(name)&&GetLastError()==120,42015);
 U dll=GetModuleHandleA("SHLWAPI.DLL");U(__stdcall*entry)(const W*)=(void*)GetProcAddress(dll,(const char*)83);check(entry&&entry(name)==kernel,42016);
}
#ifdef SHELL_MODULES_STANDALONE
void mainCRTStartup(void){run_shell_modules_test();ExitProcess(0);}
#endif
