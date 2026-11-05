/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API void SetLastError(U);API U SetErrorMode(U);API U CreateDirectoryA(const char*,void*);API U RemoveDirectoryA(const char*);API U CreateFileA(const char*,U,U,void*,U,U,U);API U CloseHandle(U);API U DeleteFileA(const char*);API U ordinal_45(const char*);
API U PathFileExistsA(const char*);API U PathFileExistsW(const W*);API U PathIsDirectoryA(const char*);API U PathIsDirectoryW(const W*);API U PathIsDirectoryEmptyA(const char*);API U PathIsDirectoryEmptyW(const W*);API U PathIsUNCServerA(const char*);API U PathIsUNCServerW(const W*);API U PathIsUNCServerShareA(const char*);API U PathIsUNCServerShareW(const W*);
static void check(int yes,U code){if(!yes)ExitProcess(code);}
static void widen(const char*s,W*d){do{*d++=(unsigned char)*s;}while(*s++);}
void run_path_probes_test(void){
 const char*dir="C:\\WINDOWS\\TEMP\\PathProbe",*file="C:\\WINDOWS\\TEMP\\PathProbe\\caf\xe9.txt";W wdir[260],wfile[260],unc[32];widen(dir,wdir);widen(file,wfile);
 U mode=SetErrorMode(0x8000);SetLastError(0x7777);check(!PathFileExistsA(0)&&!PathFileExistsW(0)&&GetLastError()==0x7777,25200);
 check(!PathFileExistsA("")&&!ordinal_45("")&&!PathIsDirectoryA(""),25220);
 check(CreateDirectoryA(dir,0),25201);check(ordinal_45(dir)&&PathFileExistsA(dir)&&PathFileExistsW(wdir),25202);
 check(PathIsDirectoryA(dir)==16&&PathIsDirectoryW(wdir)==16,25203);check(PathIsDirectoryEmptyA(dir)&&PathIsDirectoryEmptyW(wdir),25204);
 U h=CreateFileA(file,0xc0000000,3,0,2,0,0);check(h!=~0u&&CloseHandle(h),25205);
 check(PathFileExistsA(file)&&PathFileExistsW(wfile)&&ordinal_45(file),25206);check(!PathIsDirectoryA(file)&&!PathIsDirectoryW(wfile),25207);check(!PathIsDirectoryEmptyA(dir)&&!PathIsDirectoryEmptyW(wdir),25208);
 check(!PathFileExistsA("C:\\WINDOWS\\TEMP\\NoSuchPathProbe")&&!ordinal_45("C:\\WINDOWS\\TEMP\\NoSuchPathProbe"),25209);check(SetErrorMode(0x8000)==0x8000,25210);
 check(PathIsUNCServerA("\\\\")&&PathIsUNCServerA("\\\\server")&&!PathIsUNCServerA("\\\\server\\"),25211);check(PathIsUNCServerShareA("\\\\server\\")&&PathIsUNCServerShareA("\\\\server\\share")&&!PathIsUNCServerShareA("\\\\server\\share\\"),25212);
 widen("\\\\server",unc);check(PathIsUNCServerW(unc)&&!PathIsUNCServerShareW(unc),25213);widen("\\\\server\\share",unc);check(!PathIsUNCServerW(unc)&&PathIsUNCServerShareW(unc),25214);
 check(!PathIsDirectoryA("\\\\server"),25215);check(!ordinal_45("\\\\server\\share")&&GetLastError()==50,25216);check(SetErrorMode(0x8000)==0x8000,25217);
 check(DeleteFileA(file)&&PathIsDirectoryEmptyW(wdir)&&RemoveDirectoryA(dir),25218);check(!PathIsDirectoryEmptyA(dir)&&!PathIsDirectoryA(0)&&!PathIsDirectoryW(0),25219);SetErrorMode(mode);
}
#ifdef PATH_PROBES_STANDALONE
void mainCRTStartup(void){run_path_probes_test();ExitProcess(0);}
#endif
