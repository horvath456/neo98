// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;typedef unsigned short W;typedef struct{U low,high;}FT;typedef struct{W year,month,weekday,day,hour,minute,second,milliseconds;}ST;
#define API __declspec(dllimport) __stdcall
API U CreateFileA(const char*,U,U,const void*,U,U,U);API U CloseHandle(U);API U WriteFile(U,const void*,U,U*,void*);
API U GetFileTime(U,FT*,FT*,FT*);API U SetFileTime(U,const FT*,const FT*,const FT*);
API U SystemTimeToFileTime(const ST*,FT*);API U FileTimeToSystemTime(const FT*,ST*);API U GetLastError(void);API void ExitProcess(U);
API U FindFirstFileA(const char*,void*);API U FindClose(U);
static void check(int ok,U code){if(!ok)ExitProcess(code);}
static int equal(FT a,FT b){return a.low==b.low&&a.high==b.high;}
void mainCRTStartup(void){
 U h=CreateFileA("C:\\TIMES.TXT",0xc0000000,3,0,2,0x80,0);check(h!=~0u,36400);
 FT c,a,w,target,zero={0,0};ST st={2002,7,0,3,10,4,5,230},decoded;
 check(GetFileTime(h,&c,&a,&w),36401);check(FileTimeToSystemTime(&c,&decoded)&&decoded.year==1998&&decoded.month==6&&decoded.day==25,36402);
 check(SystemTimeToFileTime(&st,&target)&&SetFileTime(h,&target,&target,&target),36403);
 check(GetFileTime(h,&c,&a,&w)&&equal(c,target),36404);
 check(FileTimeToSystemTime(&a,&decoded)&&decoded.year==2002&&decoded.day==3&&!decoded.hour&&!decoded.minute&&!decoded.second&&!decoded.milliseconds,36405);
 check(FileTimeToSystemTime(&w,&decoded)&&decoded.year==2002&&decoded.second==6&&!decoded.milliseconds,36406);
 check(SetFileTime(h,&zero,&zero,&zero),36407);FT cc,aa,ww;check(GetFileTime(h,&cc,&aa,&ww)&&equal(cc,c)&&equal(aa,a)&&equal(ww,w),36408);
 U find[80],search=FindFirstFileA("C:\\TIMES.TXT",find);check(search!=~0u&&find[1]==c.low&&find[2]==c.high&&find[5]==w.low&&find[6]==w.high&&FindClose(search),36409);
 U done=0;check(WriteFile(h,"time",4,&done,0)&&done==4&&GetFileTime(h,&cc,0,&ww),36410);check(equal(cc,c)&&FileTimeToSystemTime(&ww,&decoded)&&decoded.year==1998,36411);
 check(CloseHandle(h),36412);h=CreateFileA("C:\\TIMES.TXT",0x80000000,3,0,3,0,0);check(h!=~0u&&GetFileTime(h,&cc,0,0)&&equal(cc,c),36413);
 check(!SetFileTime(h,&target,0,0)&&GetLastError()==5,36414);check(CloseHandle(h),36415);check(!GetFileTime(h,&cc,0,0)&&GetLastError()==6,36416);
 ExitProcess(0);
}
