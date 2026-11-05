// SPDX-License-Identifier: GPL-2.0-or-later
// Guest fixture for the KERNEL32 fiber/debug/power family.
#include "kernel_misc_imports.h"
#define P(x) ((U)(x))
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
void *memset(void *p,int v,unsigned n){unsigned char*q=p;while(n--)*q++=v;return p;}
void *memcpy(void *p,const void *s,unsigned n){unsigned char*q=p;const unsigned char*r=s;while(n--)*q++=*r++;return p;}
static U equal(const void*a,const void*b,U n){const unsigned char*x=a,*y=b;while(n--)if(*x++!=*y++)return 0;return 1;}
static U has(const char*s,const char*word){for(;*s;++s){const char*a=s,*b=word;while(*a&&*b&&*a==*b){++a;++b;}if(!*b)return 1;}return 0;}
static U fs_read(U offset){U v;__asm__ volatile("movl %%fs:(%1),%0":"=r"(v):"r"(offset));return v;}
static U segment_cs(void){U v;__asm__ volatile("movl %%cs,%0":"=r"(v));return v&0xffff;}
static U segment_fs(void){U v;__asm__ volatile("movl %%fs,%0":"=r"(v));return v&0xffff;}
U probe_breakpoint(U handler);
U probe_fatalexit(U handler);
static U fiber_main,fiber_worker,fiber_log,fiber_data_seen,fiber_seh_seen;
static U break_seen,break_code,break_flags,break_address,break_parameters;
static void __stdcall fiber_proc(U parameter){
 U self=fs_read(0x10);fiber_data_seen=*(U*)self;fiber_seh_seen=fs_read(0);
 CHECK(parameter==0x777&&self==fiber_worker);fiber_log=fiber_log*10+1;
 SwitchToFiber(fiber_main);
 fiber_log=fiber_log*10+2;SwitchToFiber(fiber_main);
}
static U __stdcall returning_fiber(U parameter){CHECK(parameter==9);return 0x55;}
static U __stdcall fiber_thread(U parameter){U me=ConvertThreadToFiber(0);CHECK(me&&fs_read(0x10)==me);U f=CreateFiber(0,P(returning_fiber),9);CHECK(f);SwitchToFiber(f);return 0x99;}
static void __stdcall deleting_fiber(U parameter){DeleteFiber(fs_read(0x10));ExitThread(77);}
static U __stdcall fiber_delete_thread(U parameter){CHECK(ConvertThreadToFiber(0));U f=CreateFiber(4096,P(deleting_fiber),0);CHECK(f);SwitchToFiber(f);return 0x98;}
static void fibers_test(void){
 fiber_main=ConvertThreadToFiber(0x1234);CHECK(fiber_main&&fs_read(0x10)==fiber_main&&*(U*)fiber_main==0x1234);
 CHECK(*(U*)(fiber_main+4)==fs_read(0)&&*(U*)(fiber_main+8)==fs_read(4)&&*(U*)(fiber_main+0xc)==fs_read(8));
 U stack_base=fs_read(4),stack_limit=fs_read(8);
 fiber_worker=CreateFiber(0x4000,P(fiber_proc),0x777);CHECK(fiber_worker&&*(U*)fiber_worker==0x777&&*(U*)(fiber_worker+4)==~0u);
 CHECK(*(U*)(fiber_worker+8)-*(U*)(fiber_worker+0xc)==0x4000&&*(U*)(fiber_worker+0xc4)==P(fiber_proc)&&*(U*)(fiber_worker+0xb8)==0x777);
 fiber_log=0;SwitchToFiber(fiber_worker);
 CHECK(fiber_log==1&&fiber_data_seen==0x777&&fiber_seh_seen==~0u);
 CHECK(fs_read(0x10)==fiber_main&&fs_read(4)==stack_base&&fs_read(8)==stack_limit);
 CHECK(*(U*)(fiber_worker+4)==~0u&&*(U*)(fiber_worker+0xd8)<*(U*)(fiber_worker+8)&&*(U*)(fiber_worker+0xd8)>*(U*)(fiber_worker+0xc));
 SwitchToFiber(fiber_worker);CHECK(fiber_log==12&&fs_read(0x10)==fiber_main);
 DeleteFiber(fiber_worker);
 CHECK(!CreateFiber(0xffffffff,P(fiber_proc),0)&&GetLastError()==8);
 U tid=0,code=0,thread=CreateThread(0,0,P(fiber_thread),0,0,P(&tid));CHECK(thread&&WaitForSingleObject(thread,~0u)==0);
 CHECK(GetExitCodeThread(thread,P(&code))&&code==0x55);CloseHandle(thread);
 thread=CreateThread(0,0,P(fiber_delete_thread),0,0,P(&tid));CHECK(thread&&WaitForSingleObject(thread,~0u)==0);
 CHECK(GetExitCodeThread(thread,P(&code))&&code==1);CloseHandle(thread);
}
static U __cdecl break_handler(U record,U frame,U context,U dispatcher){
 const U*r=(const U*)record;break_code=r[0];break_flags=r[1];break_address=r[3];break_parameters=r[4];++break_seen;
 CHECK(frame&&context&&dispatcher);return 0;
}
static void debug_test(void){
 CHECK(probe_breakpoint(P(break_handler))==0x42&&break_seen==1&&break_code==0x80000003&&break_flags==0&&break_address&&break_parameters==0);
 U event[24];CHECK(!WaitForDebugEvent(P(event),0)&&GetLastError()==87);
 CHECK(!ContinueDebugEvent(1,1,0x10002)&&GetLastError()==87);
 CHECK(!DebugActiveProcess(GetCurrentProcessId())&&GetLastError()==87);
 CHECK(!DebugActiveProcess(0x7fff)&&GetLastError()==87);
 _DebugOut(1,P("misc fixture"));_DebugOut(5,P("quiet"));
 CHECK(_DebugPrintf("x %d",1)==0&&dprintf("y")==0);
 CHECK(Beep(750,100)==1);
}
static void selector_test(void){
 unsigned char entry[8];const U cs=segment_cs(),fs=segment_fs();
 CHECK(GetThreadSelectorEntry(GetCurrentThread(),cs,P(entry))&&entry[0]==0xff&&entry[1]==0xff&&(entry[5]&0xfe)==0x9a&&entry[6]==0xcf);
 CHECK(GetThreadSelectorEntry(~1u,fs,P(entry))&&(entry[2]|entry[3]<<8|entry[4]<<16|(U)entry[7]<<24)==fs_read(0x18)&&entry[5]==0x93);
 CHECK(!GetThreadSelectorEntry(GetCurrentThread(),cs|4,P(entry))&&GetLastError()==87);
 CHECK(!GetThreadSelectorEntry(GetCurrentThread(),0x1000,P(entry))&&GetLastError()==87);
 CHECK(!GetThreadSelectorEntry(0x12345,cs,P(entry))&&GetLastError()==6);
}
static void write_watch_test(void){
 const U size=4*4096;unsigned char*p=(unsigned char*)VirtualAlloc(0,size,0x203000,4);CHECK(p);
 U addresses[8],count=8,granularity=0;
 CHECK(GetWriteWatch(0,P(p),size,P(addresses),P(&count),P(&granularity))==0&&count==0&&granularity==4096);
 p[0]=1;p[2*4096+5]=2;count=8;
 CHECK(GetWriteWatch(1,P(p),size,P(addresses),P(&count),P(&granularity))==0&&count==2&&addresses[0]==P(p)&&addresses[1]==P(p)+8192);
 count=8;CHECK(GetWriteWatch(0,P(p),size,P(addresses),P(&count),P(&granularity))==0&&count==0);
 lstrcpyA(P(p+4096),P("native write"));count=8;
 CHECK(GetWriteWatch(0,P(p),size,P(addresses),P(&count),P(&granularity))==0&&count==1&&addresses[0]==P(p)+4096);
 p[3*4096]=1;count=1;
 CHECK(GetWriteWatch(0,P(p),size,P(addresses),P(&count),P(&granularity))==0&&count==1&&addresses[0]==P(p)+4096);
 count=8;CHECK(GetWriteWatch(0,P(p)+8192,8192,P(addresses),P(&count),P(&granularity))==0&&count==1&&addresses[0]==P(p)+12288);
 CHECK(ResetWriteWatch(P(p),size)==0);count=8;CHECK(GetWriteWatch(0,P(p),size,P(addresses),P(&count),P(&granularity))==0&&count==0);
 U other=VirtualAlloc(0,4096,0x3000,4);CHECK(other);
 CHECK(GetWriteWatch(0,other,4096,P(addresses),P(&count),P(&granularity))!=0&&GetLastError()==87);
 CHECK(ResetWriteWatch(other,4096)!=0&&GetLastError()==87);
 CHECK(VirtualFree(P(p),0,0x8000));count=8;CHECK(GetWriteWatch(0,P(p),size,P(addresses),P(&count),P(&granularity))!=0);
 CHECK(VirtualFree(other,0,0x8000));
}
static U __stdcall worker(U parameter){return 1;}
static U __stdcall sys_handler(U message,U wparam,U lparam){return 0;}
static void services_test(void){
 U index=TlsAllocInternal();CHECK(index!=~0u&&TlsSetValue(index,5)&&TlsGetValue(index)==5&&TlsFreeInternal(index)==1);
 CHECK(!TlsFreeInternal(index)&&GetLastError()==87);
 U tid=0;CHECK(!CreateKernelThread(0,0,P(worker),0,0,P(&tid))&&GetLastError()==50);
 CHECK(!CreateKernelThread(0,0,0,0,0,P(&tid))&&GetLastError()==87);
 char buffer[32];memset(buffer,'x',sizeof buffer);
 CHECK(GetProductName(P(buffer),0)==11&&buffer[0]=='x');
 CHECK(GetProductName(P(buffer),4)==(P(buffer)&0xffff)&&equal(buffer,"Win\0",4)&&buffer[4]=='x');
 CHECK(GetProductName(P(buffer),32)==(P(buffer)&0xffff)&&equal(buffer,"Windows 98 ",12));
 CHECK(K32_NtCreateFile(0,0,0,0,0,0,0,0,0,0,0)==0xc0000002);
 CHECK(K32_RtlNtStatusToDosError(0xc0000005)==998&&K32_RtlNtStatusToDosError(0xc000000d)==87&&K32_RtlNtStatusToDosError(0x80070005)==5);
 CHECK(K32_RtlNtStatusToDosError(0)==0&&K32_RtlNtStatusToDosError(0xc0000034)==2&&K32_RtlNtStatusToDosError(0xc0000002)==1);
 CHECK(K32_RtlNtStatusToDosError(0x20000001)==0x20000001&&K32_RtlNtStatusToDosError(0xc0010001)==1);
 U locale=GetUserDefaultLCID();char text[8];
 CHECK(NotifyNLSUserCache(locale,0xe,P(","))==1&&GetLocaleInfoA(locale,0xe,P(text),8)==2&&text[0]==',');
 CHECK(NotifyNLSUserCache(locale,0xe,P("."))==1&&GetLocaleInfoA(locale,0xe,P(text),8)==2&&text[0]=='.');
 unsigned char marked[2]={0xff,0};
 CHECK(NotifyNLSUserCache(locale^0x400,0xe,P(","))==0&&NotifyNLSUserCache(locale,0xe,P(marked))==0);
 CHECK(ResetNLSUserInfoCache()==1);
 CHECK(RegisterSysMsgHandler(P(sys_handler),GetCurrentThread(),0x218,0x219,0)==1);
 CHECK(!RegisterSysMsgHandler(P(sys_handler),GetCurrentThread(),0x218,0x219,4)&&GetLastError()==87);
 CHECK(!RegisterSysMsgHandler(P(sys_handler),0x12345,0x218,0x219,0)&&GetLastError()==6);
 SetLastError(0x77);CHECK(SignalSysMsgHandlers(0x300,0,0,0)==0&&GetLastError()==0);
 CHECK(SignalSysMsgHandlers(0x218,0,0,2)==~0u&&GetLastError()==8);
 CHECK(SignalSysMsgHandlers(0x219,0,0,1)==0);
 CHECK(SignalSysMsgHandlers(0x218,0,0,8)==~0u&&GetLastError()==87);
 CHECK(RegisterSysMsgHandler(P(sys_handler),GetCurrentThread(),0x218,0x219,1)==1&&SignalSysMsgHandlers(0x218,0,0,2)==0);
 CHECK(RegisterSysMsgHandler(0,GetCurrentThread(),0,0,1)==1);
 CHECK(!SetMessageWaitingIndicator(GetCurrentThread(),1)&&GetLastError()==87);
}
static void power_test(void){
 CHECK(RequestWakeupLatency(1)==1&&RequestWakeupLatency(1)==1);
 CHECK(!RequestWakeupLatency(2)&&GetLastError()==87);
 CHECK(RequestWakeupLatency(0)==1&&RequestWakeupLatency(0)==1);
 U file=CreateFileA(P("misc-wake.tmp"),0xc0000000,0,0,2,0x80,0);CHECK(file!=~0u);
 SetLastError(0x1234);CHECK(RequestDeviceWakeup(file)==0&&GetLastError()==0x1234);
 CHECK(CancelDeviceWakeupRequest(file)==0&&GetLastError()==0x1234);
 CHECK(!RequestDeviceWakeup(0x12345)&&GetLastError()==87&&!CancelDeviceWakeupRequest(0x12345)&&GetLastError()==87);
 U state=5;CHECK(!GetDevicePowerState(file,P(&state))&&GetLastError()==87&&state==5);
 CHECK(!SetSystemPowerState(1,0)&&GetLastError()==1141);
 CHECK(CloseHandle(file)&&DeleteFileA(P("misc-wake.tmp")));
}
void mainCRTStartup(void){
 const char*command=(const char*)GetCommandLineA();
 if(has(command,"fatalapp")){SetErrorMode(2);FatalAppExitA(0,P("fixture fatal"));ExitProcess(5);}
 if(has(command,"fatalexit")){SetErrorMode(2);probe_fatalexit(P(break_handler));ExitProcess(6);}
 if(has(command,"break")){DebugBreak();ExitProcess(7);}
 fibers_test();debug_test();selector_test();write_watch_test();services_test();power_test();
 ExitProcess(0);
}
