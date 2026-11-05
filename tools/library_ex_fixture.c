// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U LoadLibraryExA(const char*,U,U);API U LoadLibraryExW(const unsigned short*,U,U);API U LoadLibraryA(const char*);API U FreeLibrary(U);API U GetProcAddress(U,const char*);API U GetModuleHandleA(const char*);API U GetLastError(void);
static void check(int yes,U e){if(!yes)ExitProcess(e);}
static void loaded(U flags,U expected,U e){U module=LoadLibraryExA("C:\\ALT\\ENTRY.DLL",0xdeadbeef,flags);check(module!=0,e);U(__stdcall*value)(void)=(void*)GetProcAddress(module,"Value");U(__stdcall*attached)(void)=(void*)GetProcAddress(module,"Attached");check(value&&attached&&value()==expected&&attached()==1,e+1);check(FreeLibrary(module),e+2);check(!GetModuleHandleA("ENTRY.DLL")&&!GetModuleHandleA("DEP.DLL"),e+3);}
void run_library_ex_test(void){check(!LoadLibraryExA(0,0,0)&&GetLastError()==87,38400);loaded(8,7,38401);loaded(0,3,38405);loaded(1,3,38409);loaded(0x8000000c,7,38413);check(!LoadLibraryExA("C:\\ALT\\MISSING.DLL",0,8)&&GetLastError()==126,38417);loaded(0,3,38418);check(!LoadLibraryExA("C:\\ALT\\ENTRY.DLL",0,2)&&GetLastError()==11,38422);check(!GetModuleHandleA("ENTRY.DLL"),38423);check(!LoadLibraryExW((void*)0xdeadbeef,0,8)&&GetLastError()==120,38424);U module=LoadLibraryA("KERNEL32.DLL");check(module&&LoadLibraryExA("KERNEL32.DLL",1,0)==module,38425);check(FreeLibrary(module)&&FreeLibrary(module),38426);}
#ifdef LIBRARY_EX_STANDALONE
void mainCRTStartup(void){run_library_ex_test();ExitProcess(0);}
#endif
