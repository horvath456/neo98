// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U CreateFileA(const char*,U,U,const void*,U,U,U);
API U GetFileAttributesA(const char*);
API U WriteFile(U,const void*,U,U*,void*);
API U ReadFile(U,void*,U,U*,void*);
API U CloseHandle(U);
API U GetLastError(void);
API void ExitProcess(U);
static void check(int ok,U code){if(!ok)ExitProcess(code);}
void mainCRTStartup(void){
 U sa[3]={12,0xdeadbeef,1},h,done=0;char bytes[8]={0};
 U overlapped[5]={0x13579bdf,0x22222222,3,0,0};
 h=CreateFileA("C:\\SECURITY.TXT",0xc0000000,0,sa,2,0x80,0);check(h!=~0u,36000);
 /* Jet probes OVERLAPPED, then falls back on Win98 error 87.
    Failure clears the byte count and leaves both the file cursor and the
    OVERLAPPED fields alone. The following synchronous write must work. */
 done=99;check(!WriteFile(h,"NEO98",5,&done,overlapped)&&GetLastError()==87&&!done,36021);
 check(overlapped[0]==0x13579bdf&&overlapped[1]==0x22222222&&overlapped[2]==3,36022);
 check(WriteFile(h,"NEO98",5,&done,0)&&done==5,36001);check(CloseHandle(h),36002);
 sa[2]=0;h=CreateFileA("C:\\SECURITY.TXT",0,1,sa,3,0,0);check(h!=~0u,36003);
 done=99;check(!ReadFile(h,bytes,5,&done,overlapped)&&GetLastError()==87&&!done&&!bytes[0],36023);
 check(overlapped[0]==0x13579bdf&&overlapped[1]==0x22222222&&overlapped[2]==3,36024);
 check(ReadFile(h,bytes,5,&done,0)&&done==5&&bytes[0]=='A'&&bytes[4]=='8',36004);check(CloseHandle(h),36005);
 sa[0]=16;check(CreateFileA("C:\\BAD.TXT",0x40000000,0,sa,2,0,0)==~0u&&GetLastError()==13,36006);
 sa[0]=12;check(CreateFileA("C:\\BAD.TXT",1,0,sa,2,0,0)==~0u&&GetLastError()==87,36007);
 check(CreateFileA("C:\\BAD.TXT",0x40000000,4,sa,2,0,0)==~0u&&GetLastError()==87,36008);
 /* Local CreateFile accepts caching hints, including FILE_FLAG_WRITE_THROUGH used by scheduled-task logging. */
 h=CreateFileA("C:\\THROUGH.TXT",0xc0000000,1,0,4,0x80000080,0);check(h!=~0u,36009);
 check(WriteFile(h,"NEO98",5,&done,0)&&done==5,36010);check(CloseHandle(h),36011);
 h=CreateFileA("C:\\THROUGH.TXT",0x80000000,1,0,3,0x28000000,0);check(h!=~0u,36012);
 check(ReadFile(h,bytes,5,&done,0)&&done==5&&bytes[0]=='A',36013);check(CloseHandle(h),36014);
 /* FILE_ATTRIBUTE_NORMAL alone becomes zero; otherwise retain mask 0x67, then add FILE_ATTRIBUTE_ARCHIVE. */
 check(GetFileAttributesA("C:\\THROUGH.TXT")==0x20,36015);
 h=CreateFileA("C:\\HIDDEN.TXT",0xc0000000,0,0,2,0x80000002,0);check(h!=~0u,36016);
 check(CloseHandle(h)&&GetFileAttributesA("C:\\HIDDEN.TXT")==0x22,36017);
 /* Local-file template handles accept null or -1; all other values return ERROR_NOT_SUPPORTED. */
 check(CreateFileA("C:\\TEMPLATE.TXT",0x40000000,0,0,2,0x80,4)==~0u&&GetLastError()==50,36018);
 h=CreateFileA("C:\\TEMPLATE.TXT",0x40000000,0,0,2,0x80,~0u);check(h!=~0u,36019);
 check(CloseHandle(h),36020);
 ExitProcess(0);
}
