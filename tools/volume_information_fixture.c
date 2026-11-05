// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API void SetLastError(U);API U GetLastError(void);API U GetVolumeInformationA(const char*,char*,U,U*,U*,U*,char*,U);API U GetVolumeInformationW(const void*,void*,U,U*,U*,U*,void*,U);API U GetLogicalDrives(void);API U GetLogicalDriveStringsA(U,char*);API U GetLogicalDriveStringsW(U,void*);API U SetCurrentDirectoryA(const char*);
static void check(int v,U code){if(!v)ExitProcess(code);}static int same(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return*a==*b;}
void run_volume_information_test(void){char volume[32],fs[32],drives[16];U serial=9,max=9,flags=9;SetLastError(777);check(GetVolumeInformationA("C:\\",volume,32,&serial,&max,&flags,fs,32),34400);check(same(volume,"NEO98 RAM")&&same(fs,"NEO98")&&serial==0&&max==255&&flags==2,34401);check(GetLastError()==777,34402);
 fs[0]='!';SetLastError(777);check(!GetVolumeInformationA("C:\\",0,0,&serial,&max,&flags,fs,5)&&GetLastError()==111&&fs[0]=='!',34403);
 volume[0]='!';check(!GetVolumeInformationA("C:\\",volume,9,0,0,0,0,0)&&GetLastError()==111&&volume[0]=='!',34404);
 check(!GetVolumeInformationA("C:\\WINDOWS",0,0,0,0,0,0,0)&&GetLastError()==161,34405);check(SetCurrentDirectoryA("C:\\WINDOWS"),34406);check(GetVolumeInformationA(0,volume,32,0,&max,&flags,fs,32)&&same(fs,"NEO98"),34407);
 check(GetLogicalDrives()==4&&GetLogicalDriveStringsA(0,0)==5,34408);drives[0]='!';check(GetLogicalDriveStringsA(4,drives)==5&&drives[0]=='!',34409);check(GetLogicalDriveStringsA(5,drives)==4&&same(drives,"c:\\")&&drives[4]==0,34410);
 check(!GetVolumeInformationW(0,0,0,0,0,0,0,0)&&GetLastError()==120,34411);check(!GetLogicalDriveStringsW(0,0)&&GetLastError()==120,34412);
}
#ifdef VOLUME_INFORMATION_STANDALONE
void mainCRTStartup(void){run_volume_information_test();ExitProcess(0);}
#endif
