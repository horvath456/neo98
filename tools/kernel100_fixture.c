// SPDX-License-Identifier: GPL-2.0-or-later
#include "kernel100_imports.h"
#define P(x) ((U)(x))
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
static U apc_value,io_count,io_error,io_bytes,enum_count,ctrl_count;
void *memset(void *p,int v,unsigned n){unsigned char*q=p;while(n--)*q++=v;return p;}
void *memcpy(void *p,const void *s,unsigned n){unsigned char*q=p;const unsigned char*r=s;while(n--)*q++=*r++;return p;}
static U equal(const void*a,const void*b,U n){const unsigned char*x=a,*y=b;while(n--)if(*x++!=*y++)return 0;return 1;}
static U length(const char*s){U n=0;while(s[n])++n;return n;}
static void __stdcall apc(U data){apc_value=apc_value*10+data;}
static void __stdcall complete(U error,U bytes,U overlapped){CHECK(overlapped);io_error=error;io_bytes=bytes;++io_count;}
static U __stdcall enumerate(U text){CHECK(*(char*)text);++enum_count;return 1;}
static U codepage_mask;
static U __stdcall enumerate_codepage(U text){
 const char*pages[]={"437","850","866","1252","65000","65001"};
 for(U i=0;i<6;++i)if(length((char*)text)==length(pages[i])&&equal((char*)text,pages[i],length(pages[i]))){
  CHECK(!(codepage_mask&(1u<<i)));CHECK(IsValidCodePage((U)(i==0?437:i==1?850:i==2?866:i==3?1252:i==4?65000:65001)));
  codepage_mask|=1u<<i;++enum_count;return 1;
 }
 CHECK(0);return 0;
}
static U __stdcall enumerate_ex(U text,U calendar){CHECK(*(char*)text&&calendar==1);++enum_count;return 1;}
static U __stdcall control(U code){CHECK(code==0);++ctrl_count;return 1;}
static U __stdcall worker(U event){WaitForSingleObject(event,~0u);return 27;}
static void console_test(void){
 U done=0,mode=0,info[16],cursor[2]={50,0};char text[256];unsigned short attrs[16];
 CHECK(AllocConsole());CHECK(!AllocConsole()&&GetLastError()==5);
 U input=GetStdHandle(~9u),output=GetStdHandle(~10u);CHECK(input&&output);
 CHECK(GetConsoleCP()==GetOEMCP()&&GetConsoleOutputCP()==GetOEMCP());
 CHECK(GetConsoleMode(input,P(&mode))&&mode==7);CHECK(SetConsoleMode(input,0));
 CHECK(SetConsoleMode(output,3));CHECK(GetConsoleMode(output,P(&mode))&&mode==3);
 CHECK(SetStdHandle(~11u,output)&&GetStdHandle(~11u)==output);
 CHECK(SetConsoleTitleA(P("API console")));CHECK(GetConsoleTitleA(P(text),sizeof text)==11&&equal(text,"API console",12));
 CHECK(GetLargestConsoleWindowSize(output)==(80|(25u<<16)));
 CHECK(GetConsoleScreenBufferInfo(output,P(info)));CHECK(((unsigned short*)info)[0]==80&&((unsigned short*)info)[1]==25);
 CHECK(SetConsoleCursorInfo(output,P(cursor)));memset(cursor,0,sizeof cursor);CHECK(GetConsoleCursorInfo(output,P(cursor))&&cursor[0]==50&&!cursor[1]);
 CHECK(SetConsoleScreenBufferSize(output,100|(30u<<16)));
 short window[]={1,1,70,20};CHECK(SetConsoleWindowInfo(output,1,P(window)));
 CHECK(SetConsoleCursorPosition(output,3|(2u<<16)));CHECK(SetConsoleTextAttribute(output,0x1e));
 CHECK(WriteConsoleA(output,P("abc"),3,P(&done),0)&&done==3);
 CHECK(ReadConsoleOutputCharacterA(output,P(text),3,3|(2u<<16),P(&done))&&done==3&&equal(text,"abc",3));
 CHECK(ReadConsoleOutputAttribute(output,P(attrs),3,3|(2u<<16),P(&done))&&attrs[0]==0x1e&&attrs[2]==0x1e);
 CHECK(FillConsoleOutputCharacterA(output,'Z',4,0,P(&done))&&done==4);
 CHECK(FillConsoleOutputAttribute(output,0x4f,4,0,P(&done))&&done==4);
 attrs[0]=0x17;attrs[1]=0x28;CHECK(WriteConsoleOutputAttribute(output,P(attrs),2,0,P(&done)));
 CHECK(WriteConsoleOutputCharacterA(output,P("xy"),2,0,P(&done)));
 U cells[4]={'A'|(0x12u<<16),'B'|(0x23u<<16),'C'|(0x34u<<16),'D'|(0x45u<<16)},readback[4];short rect[]={0,3,1,4};
 CHECK(WriteConsoleOutputA(output,P(cells),2|(2u<<16),0,P(rect)));
 CHECK(ReadConsoleOutputA(output,P(readback),2|(2u<<16),0,P(rect))&&equal(cells,readback,16));
 short source[]={0,3,1,4};U fill='.'|(7u<<16);
 CHECK(ScrollConsoleScreenBufferA(output,P(source),0,2|(3u<<16),P(&fill)));
 CHECK(ReadConsoleOutputCharacterA(output,P(text),4,3u<<16,P(&done))&&equal(text,"..AB",4));
 U secondary=CreateConsoleScreenBuffer(0xc0000000,3,0,1,0);CHECK(secondary!=~0u&&SetConsoleActiveScreenBuffer(secondary));
 CHECK(GetConsoleScreenBufferInfo(secondary,P(info))&&((unsigned short*)info)[0]==80);
 CHECK(SetConsoleActiveScreenBuffer(output));CHECK(CloseHandle(secondary));CHECK(!GetConsoleMode(secondary,P(&mode))&&GetLastError()==6);
 for(U cycle=0;cycle<32;++cycle){
  U screen=CreateConsoleScreenBuffer(0xc0000000,3,0,1,0);CHECK(screen!=~0u);
  CHECK(GetConsoleScreenBufferInfo(screen,P(info))&&((unsigned short*)info)[2]==0&&((unsigned short*)info)[4]==7);
  CHECK(SetConsoleCursorPosition(screen,9)&&SetConsoleTextAttribute(screen,0x3e));
  CHECK(SetConsoleActiveScreenBuffer(screen)&&CloseHandle(screen));
  CHECK(SetConsoleActiveScreenBuffer(output));
 }
 U events[10]={1,1,1,('K'<<16)|0x25,0,1,1,1,('Q'<<16)|0x26,0};
 CHECK(WriteConsoleInputA(input,P(events),2,P(&done))&&done==2);
 CHECK(GetNumberOfConsoleInputEvents(input,P(&done))&&done==2);
 U observed[10];CHECK(PeekConsoleInputA(input,P(observed),2,P(&done))&&done==2&&equal(events,observed,40));
 CHECK(ReadConsoleInputA(input,P(observed),1,P(&done))&&done==1);
 CHECK(ReadConsoleA(input,P(text),1,P(&done),0)&&done==1&&text[0]=='Q');
 events[2]=3;CHECK(WriteConsoleInputA(input,P(events),1,P(&done)));
 CHECK(ReadConsoleA(input,P(text),1,P(&done),0)&&done==1&&text[0]=='K');
 CHECK(ReadConsoleA(input,P(text),2,P(&done),0)&&done==2&&equal(text,"KK",2));
 CHECK(WriteConsoleInputA(input,P(events),2,P(&done)));CHECK(FlushConsoleInputBuffer(input));
 CHECK(GetNumberOfConsoleInputEvents(input,P(&done))&&!done);
 CHECK(GetNumberOfConsoleMouseButtons(P(&done))&&done>=2);
 CHECK(SetConsoleCtrlHandler(P(control),1));CHECK(GenerateConsoleCtrlEvent(0,0)&&ctrl_count==1);CHECK(SetConsoleCtrlHandler(P(control),0));
 CHECK(SetConsoleCtrlHandler(0,1)&&GenerateConsoleCtrlEvent(0,0));
 CHECK(FreeConsole());CHECK(!GetConsoleMode(output,P(&mode))&&GetLastError()==6);
 CHECK(AllocConsole());CHECK(GetStdHandle(~10u)!=output);CHECK(FreeConsole());
}
static void heap_test(void){
 U p=GlobalAlloc(0,64);CHECK(p);CHECK(GlobalHandle(p)==p&&LocalHandle(p)==p);
 CHECK(!LocalFlags(p));CHECK(GlobalWire(p)==p);CHECK(!GlobalUnWire(p));GlobalFix(p);GlobalUnfix(p);
 CHECK(!LocalCompact(4096));CHECK(LocalShrink(p,16)>=64);CHECK(!GlobalFree(p));
 SetLastError(0);CHECK(LocalFlags(p)==0x8000&&GetLastError()==6);
 CHECK(!GlobalHandle(1)&&GetLastError()==6);
}
static void threads_test(void){
 U event=CreateEventA(0,0,0,0),tid=0;
 CHECK(event&&PulseEvent(event));CHECK(WaitForSingleObjectEx(event,0,0)==258);
 CHECK(QueueUserAPC(P(apc),~1u,1)&&QueueUserAPC(P(apc),~1u,2));
 CHECK(SleepEx(0,0)==0&&!apc_value);CHECK(SleepEx(0,1)==0xc0&&apc_value==12);
 CHECK(QueueUserAPC(P(apc),~1u,3));CHECK(WaitForSingleObjectEx(event,0,1)==0xc0&&apc_value==123);
 U thread=CreateThread(0,0,P(worker),event,4,P(&tid));CHECK(thread&&tid);
 U context[179];memset(context,0,sizeof context);context[0]=0x1001f;
 CHECK(GetThreadContext(thread,P(context)));U original=context[44];context[44]=0x12345678;
 CHECK(SetThreadContext(thread,P(context)));context[44]=0;CHECK(GetThreadContext(thread,P(context))&&context[44]==0x12345678);
 context[44]=original;CHECK(SetThreadContext(thread,P(context)));
 CHECK(TerminateThread(thread,77));U code=0;CHECK(GetExitCodeThread(thread,P(&code))&&code==77);
 CHECK(WaitForSingleObjectEx(thread,0,0)==0);CHECK(CloseHandle(thread));
 U critical[6];InitializeCriticalSection(P(critical));UninitializeCriticalSection(P(critical));
 thread=CreateThread(0,0,P(worker),event,0,0);CHECK(thread);SleepEx(10,0);
 CHECK(PulseEvent(event));CHECK(WaitForSingleObjectEx(thread,3000,0)==0);CHECK(GetExitCodeThread(thread,P(&code))&&code==27);CHECK(CloseHandle(thread));
 CHECK(CloseHandle(event));
}
static U __stdcall unload_worker(U module){FreeLibraryAndExitThread(module,33);return 99;}
static void unload_test(void){
 U module=LoadLibraryA(P("KERNEL32.DLL"));CHECK(module);
 U thread=CreateThread(0,0,P(unload_worker),module,0,0);CHECK(thread);
 CHECK(WaitForSingleObject(thread,3000)==0);U code=0;CHECK(GetExitCodeThread(thread,P(&code))&&code==33);CHECK(CloseHandle(thread));
}
static void toolhelp_test(void){
 U pid=GetCurrentProcessId(),snapshot=CreateToolhelp32Snapshot(15,0);CHECK(snapshot!=~0u);
 U data[140];memset(data,0,sizeof data);data[0]=296;
 CHECK(Process32First(snapshot,P(data))&&data[2]==pid);while(Process32Next(snapshot,P(data))){}CHECK(GetLastError()==18);
 data[0]=28;CHECK(Thread32First(snapshot,P(data))&&data[3]==pid);while(Thread32Next(snapshot,P(data))){}CHECK(GetLastError()==18);
 data[0]=548;CHECK(Module32First(snapshot,P(data))&&data[2]==pid&&data[5]);while(Module32Next(snapshot,P(data))){}CHECK(GetLastError()==18);
 data[0]=16;CHECK(Heap32ListFirst(snapshot,P(data))&&data[1]==pid);U heap=data[2];while(Heap32ListNext(snapshot,P(data))){}CHECK(GetLastError()==18);
 U block=HeapAlloc(heap,0,100);CHECK(block);data[0]=36;CHECK(Heap32First(P(data),pid,heap));U found=data[2]==block;
 while(Heap32Next(P(data)))if(data[2]==block)found=1;CHECK(found&&GetLastError()==18);CHECK(HeapFree(heap,0,block));
 U source=0x98765432,copy=0,done=0;CHECK(Toolhelp32ReadProcessMemory(pid,P(&source),P(&copy),4,P(&done))&&copy==source&&done==4);
 CHECK(CloseHandle(snapshot));data[0]=296;CHECK(!Process32First(snapshot,P(data))&&GetLastError()==6);
}
static void files_test(void){
 U file=CreateFileA(P("C:\\kernel100.bin"),0xc0000000,3,0,2,0x40000080,0);CHECK(file!=~0u);
 U other=CreateFileA(P("C:\\kernel100.bin"),0xc0000000,3,0,3,0x80,0),done=0;CHECK(other!=~0u);
 CHECK(WriteFile(file,P("abcdefgh"),8,P(&done),0));CHECK(LockFile(file,1,0,3,0));
 CHECK(!LockFile(other,2,0,1,0)&&GetLastError()==33);CHECK(SetFilePointer(other,1,0,0)==1);
 char data[32];CHECK(!ReadFile(other,P(data),1,P(&done),0)&&GetLastError()==33);
 CHECK(UnlockFile(file,1,0,3,0));CHECK(ReadFile(other,P(data),1,P(&done),0)&&data[0]=='b');
 U overlapped[5]={0,0,2,0,0};CHECK(ReadFileEx(file,P(data),3,P(overlapped),P(complete)));CHECK(!io_count);
 CHECK(CancelIo(file));CHECK(SleepEx(0,1)==0xc0&&io_count==1&&!io_error&&io_bytes==3&&equal(data,"cde",3));
 CHECK(GetOverlappedResult(file,P(overlapped),P(&done),0)&&done==3);
 overlapped[2]=1;CHECK(WriteFileEx(file,P("XYZ"),3,P(overlapped),P(complete)));CHECK(SleepEx(0,1)==0xc0&&io_count==2&&io_bytes==3);
 CHECK(SetFilePointer(file,0,0,1)==8);CHECK(CloseHandle(other)&&CloseHandle(file));
 CHECK(!MoveFileExA(P("C:\\kernel100.bin"),P("C:\\kernel100-moved.bin"),0)&&GetLastError()==120);
 CHECK(MoveFileExA(P("C:\\kernel100.bin"),P("C:\\kernel100-moved.bin"),0x10));
 unsigned char of[136];memset(of,0,sizeof of);file=OpenFile(P("C:\\kernel100-moved.bin"),P(of),0);CHECK(file!=~0u&&of[0]==136);
 CHECK(ReadFile(file,P(data),8,P(&done),0)&&equal(data,"aXYZefgh",8));CHECK(CloseHandle(file));
 U mapping=CreateFileMappingA(~0u,0,4,0,4096,0);CHECK(mapping);
 U view=MapViewOfFile(mapping,2,0,0,4096);CHECK(view);*(U*)view=123;
 CHECK(FlushViewOfFile(view+4,128));CHECK(!FlushViewOfFile(view+4090,128)&&GetLastError()==87);
 CHECK(UnmapViewOfFile(view)&&CloseHandle(mapping));
 CHECK(SetVolumeLabelA(0,P("TESTVOL")));CHECK(GetVolumeInformationA(0,P(data),sizeof data,0,0,0,0,0)&&equal(data,"TESTVOL",8));CHECK(SetVolumeLabelA(0,P("NEO98 RAM")));
 CHECK(DeleteFileA(P("C:\\kernel100-moved.bin")));
}
static void locale_test(void){
 U info[80];char text[256];CHECK(GetCPInfoExA(1252,0,P(info))&&info[0]==1&&info[5]==1252);
 CHECK(IsValidCodePage(437)&&IsValidCodePage(65001)&&!IsValidCodePage(123456));
 CHECK(IsValidLocale(0x409,1)&&!IsValidLocale(0x409,0));CHECK(ConvertDefaultLocale(9)==0x409);
 enum_count=0;codepage_mask=0;CHECK(EnumSystemCodePagesA(P(enumerate_codepage),1)&&enum_count==6&&codepage_mask==63);
 enum_count=0;CHECK(EnumSystemLocalesA(P(enumerate),1)&&enum_count==1);
 CHECK(VerLanguageNameA(0x409,P(text),sizeof text)==23&&equal(text,"English (United States)",24));
 enum_count=0;CHECK(EnumCalendarInfoA(P(enumerate),0x409,1,5)&&enum_count==7);
 enum_count=0;CHECK(EnumCalendarInfoExA(P(enumerate_ex),0x409,~0u,7)&&enum_count==1);
 enum_count=0;CHECK(EnumDateFormatsExA(P(enumerate_ex),0x409,1)&&enum_count==7);
 U year=0;CHECK(GetCalendarInfoA(0x409,1,0xa0000030,0,0,P(&year))==4&&year==2029);
 CHECK(!GetCalendarInfoA(0x409,1,1,P(text),sizeof text,0)&&GetLastError()==1004);
 CHECK(SetCalendarInfoA(0x409,1,48,P("2049")));CHECK(GetCalendarInfoA(0x409,1,48,P(text),sizeof text,0)==5&&equal(text,"2049",5));
 CHECK(SetCalendarInfoA(0x409,1,48,P("2029")));
 CHECK(SetLocaleInfoA(0x409,0xe,P(",")));CHECK(GetLocaleInfoA(0x409,0xe,P(text),sizeof text)==2&&text[0]==',');
 CHECK(GetLocaleInfoA(0x409,0x8000000e,P(text),sizeof text)==2&&text[0]=='.');CHECK(SetLocaleInfoA(0x409,0xe,P(".")));
}
static void misc_test(void){
 SetErrorMode(0x1234);CHECK(GetErrorMode()==0x1234);SetErrorMode(0);
 U flags=GetProcessFlags(GetCurrentProcessId());CHECK(RegisterServiceProcess(0,1));CHECK(GetProcessFlags(GetCurrentProcessId())==(flags|0x100));CHECK(RegisterServiceProcess(0,0));
 U power[3];CHECK(GetSystemPowerStatus(P(power))&&((unsigned char*)power)[0]==1);CHECK(!IsSystemResumeAutomatic());
 CHECK(SetThreadExecutionState(0x80000003)==0x80000000);CHECK(SetThreadExecutionState(0x80000000)==0x80000003);
 unsigned char dcb[28];U timeouts[5];memset(dcb,0,sizeof dcb);memset(timeouts,0,sizeof timeouts);
 CHECK(BuildCommDCBA(P("96,n,8,1"),P(dcb))&&*(U*)dcb==28&&*(U*)(dcb+4)==9600&&dcb[18]==8);
 CHECK(BuildCommDCBAndTimeoutsA(P("baud=110 to=on idsr=on odsr=off"),P(dcb),P(timeouts))&&dcb[20]==2&&timeouts[4]==60000&&(*(U*)(dcb+8)&8));
}
void mainCRTStartup(void){
 console_test();heap_test();threads_test();unload_test();toolhelp_test();files_test();locale_test();misc_test();
 CHECK(TerminateProcess(GetCurrentProcess(),0));ExitProcess(999);
}
