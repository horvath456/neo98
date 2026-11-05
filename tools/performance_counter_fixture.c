/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;typedef unsigned long long Q;
#define API __declspec(dllimport) __stdcall
API U QueryPerformanceCounter(Q*);API U QueryPerformanceFrequency(Q*);API void Sleep(U);API void SetLastError(U);API U GetLastError(void);API U CreateThread(U,U,U,U,U,U);API U WaitForSingleObject(U,U);API U CloseHandle(U);API void ExitProcess(U);
static volatile Q worker_counter,worker_frequency;
static void check(int value,U error){if(!value)ExitProcess(error);}
static U __stdcall worker(U unused){Q a=0,b=0;(void)unused;check(QueryPerformanceCounter(&a)&&QueryPerformanceFrequency(&b),34000);worker_counter=a;worker_frequency=b;return 0;}
void mainCRTStartup(void){
 struct{U before;Q value;U after;} v;v.before=0x12345678;v.after=0x87654321;v.value=~0ull;
 SetLastError(0x24681357);check(QueryPerformanceFrequency(&v.value)&&v.value==1193180,34001);check(GetLastError()==0x24681357,34002);check(v.before==0x12345678&&v.after==0x87654321,34003);
 Q frequency=v.value,first=0,last=0,current=0;check(QueryPerformanceCounter(&first),34004);last=first;
 for(U n=0;n<8;++n){check(QueryPerformanceCounter(&current)&&current>=last,34005);last=current;}
 Sleep(25);check(QueryPerformanceCounter(&current)&&current>first,34006);check(current-first>=frequency/100&&current-first<frequency*10,34007);
 U thread=CreateThread(0,0,(U)worker,0,0,0);check(thread,34008);check(!WaitForSingleObject(thread,5000),34009);check(QueryPerformanceCounter(&last)&&worker_counter>=current&&worker_counter<=last&&worker_frequency==frequency,34010);check(CloseHandle(thread),34011);
 v.value=0;check(QueryPerformanceFrequency(&v.value)&&v.value==frequency,34012);SetLastError(0);check(!QueryPerformanceCounter((Q*)0)&&GetLastError()==87,34013);check(!QueryPerformanceFrequency((Q*)0)&&GetLastError()==87,34014);ExitProcess(0);
}
