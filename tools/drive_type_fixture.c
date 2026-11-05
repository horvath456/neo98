// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetDriveTypeA(const char*);API U GetLastError(void);API void SetLastError(U);API void ExitProcess(U);
static void check(int ok,U code){if(!ok)ExitProcess(code);}
void mainCRTStartup(void){
 SetLastError(777);check(GetDriveTypeA("C:\\")==3,39200);check(GetLastError()==777,39201);
 check(GetDriveTypeA("c:\\")==3,39202);check(GetDriveTypeA(0)==3,39203);
 check(GetDriveTypeA("D:\\")==1,39204);check(GetDriveTypeA("C:\\Documents\\")==1,39205);
 check(GetDriveTypeA("\\\\server\\share\\")==1,39206);
 check(GetDriveTypeA("C:\\")==3,39207);ExitProcess(0);
}
