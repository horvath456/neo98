// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;typedef unsigned long long Q;
#define API __declspec(dllimport) __stdcall
API U GetDiskFreeSpaceA(const char*,U*,U*,U*,U*);API U GetDiskFreeSpaceExA(const char*,Q*,Q*,Q*);
API U CreateFileA(const char*,U,U,const void*,U,U,U);API U CloseHandle(U);API U WriteFile(U,const void*,U,U*,void*);API U DeleteFileA(const char*);API U GetLastError(void);API void ExitProcess(U);
static void check(int ok,U code){if(!ok)ExitProcess(code);}
void mainCRTStartup(void){
 U sectors,bytes,free,total;Q available,capacity,volume_free,after,restored;
 check(GetDiskFreeSpaceA("C:\\",&sectors,&bytes,&free,&total)&&sectors&&bytes&&total<=65535&&(Q)sectors*bytes*total==32*1024*1024,37600);
 Q unit=(Q)sectors*bytes,legacy_total=unit*total,legacy_free=unit*free;
 check(GetDiskFreeSpaceExA("C:\\WINDOWS\\",&available,&capacity,&volume_free)&&available>=legacy_free&&available-legacy_free<unit&&capacity==legacy_total&&volume_free==available,37601);
 check(GetDiskFreeSpaceExA("C:\\",0,0,0),37602);check(GetDiskFreeSpaceA(0,0,0,0,0),37603);
 U h=CreateFileA("C:\\SPACE.TXT",0x40000000,0,0,2,0x80,0),done=0;check(h!=~0u&&WriteFile(h,"1234567",7,&done,0)&&done==7&&CloseHandle(h),37604);
 check(GetDiskFreeSpaceExA("C:\\",&after,&capacity,&volume_free)&&after==available-7&&capacity==legacy_total&&volume_free==after,37605);
 check(DeleteFileA("C:\\SPACE.TXT")&&GetDiskFreeSpaceExA("C:\\",&restored,&capacity,&volume_free)&&restored==available,37606);
 check(!GetDiskFreeSpaceA("D:\\",0,0,0,0)&&GetLastError()==15,37607);
 check(!GetDiskFreeSpaceA("C:\\WINDOWS",0,0,0,0)&&GetLastError()==123,37608);
 ExitProcess(0);
}
