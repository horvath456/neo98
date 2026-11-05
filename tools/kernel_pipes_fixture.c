// SPDX-License-Identifier: GPL-2.0-or-later
// x86 guest probe for the KERNEL32 pipe, mailslot, device and Winsock
// handle services. Every failed CHECK exits
// with its line number.
#include "kernel_pipes_imports.h"
#define P(x) ((U)(x))
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
#define EXPECT_ERROR(call,code) do{SetLastError(0x7777);CHECK(!(call));CHECK(GetLastError()==(code));}while(0)
void *memset(void *p,int v,unsigned n){unsigned char*q=p;while(n--)*q++=v;return p;}
void *memcpy(void *p,const void *s,unsigned n){unsigned char*q=p;const unsigned char*r=s;while(n--)*q++=*r++;return p;}
static U equal(const void*a,const void*b,U n){const unsigned char*x=a,*y=b;while(n--)if(*x++!=*y++)return 0;return 1;}
static U late_written;
static U __stdcall late_writer(U client){
 U done=0;Sleep(30);CHECK(WriteFile(client,P("late"),4,P(&done),0)&&done==4);late_written=1;return 7;
}
static void completion_test(void){
 U bytes=0x55,key=0x66,overlapped=0x77;
 EXPECT_ERROR(GetQueuedCompletionStatus(0x1234,P(&bytes),P(&key),P(&overlapped),0),50);
 CHECK(bytes==0x55&&key==0x66&&overlapped==0);
 EXPECT_ERROR(GetQueuedCompletionStatus(0,P(&bytes),P(&key),0,~0u),50);
}
static void context_test(U event){
 EXPECT_ERROR(GetHandleContext(0x12345),6);
 EXPECT_ERROR(GetHandleContext(event),6);
 EXPECT_ERROR(SetHandleContext(event,1),6);
 EXPECT_ERROR(SetHandleContext(0x12345,1),6);
 U s=CreateSocketHandle();CHECK(s);
 SetLastError(0x1234);CHECK(GetHandleContext(s)==0&&GetLastError()==0x1234);
 CHECK(SetHandleContext(s,0xabc));CHECK(GetHandleContext(s)==0xabc);
 EXPECT_ERROR(SetHandleContext(s,5),183);
 CHECK(GetHandleContext(s)==0xabc);
 U t=CreateSocketHandle();CHECK(t&&t!=s&&GetHandleContext(t)==0);
 CHECK(GetFileType(s)==0);
 EXPECT_ERROR(OpenVxDHandle(s),6);
 CHECK(CloseHandle(s)&&CloseHandle(t));
 EXPECT_ERROR(GetHandleContext(s),6);
 EXPECT_ERROR(CloseHandle(s),6);
}
static void vxd_handle_test(U event){
 U v=OpenVxDHandle(event);CHECK(v&&v!=event);
 EXPECT_ERROR(OpenVxDHandle(0x123),6);
 EXPECT_ERROR(CloseSystemHandle(event),6);
 EXPECT_ERROR(CloseSystemHandle(v),6);
}
static void dos_device_test(void){
 char text[300];
 EXPECT_ERROR(QueryDosDeviceA(0,P(text),10),87);
 EXPECT_ERROR(QueryDosDeviceA(P("C:"),0,10),87);
 EXPECT_ERROR(QueryDosDeviceA(P("C:"),P(text),300),87);
 EXPECT_ERROR(QueryDosDeviceA(P("C:"),P(text),2),122);
 memset(text,'x',sizeof text);CHECK(QueryDosDeviceA(P("C:"),P(text),3)==2&&equal(text,"C:\0x",4));
 CHECK(QueryDosDeviceA(P("c:"),P(text),64)==2&&equal(text,"c:",3));
 EXPECT_ERROR(QueryDosDeviceA(P("D:"),P(text),64),2);
 EXPECT_ERROR(QueryDosDeviceA(P("C:\\"),P(text),64),2);
 EXPECT_ERROR(QueryDosDeviceA(P("\\\\.\\C:"),P(text),64),2);
 EXPECT_ERROR(QueryDosDeviceA(P("XYZ"),P(text),64),2);
 CHECK(QueryDosDeviceA(P("COM1"),P(text),64)==4&&equal(text,"COM1",5));
 CHECK(QueryDosDeviceA(P("lpt2:"),P(text),64)==5&&equal(text,"lpt2:",6));
 EXPECT_ERROR(QueryDosDeviceA(P("COM"),P(text),64),2);
 EXPECT_ERROR(QueryDosDeviceA(P("COM0"),P(text),64),2);
 EXPECT_ERROR(QueryDosDeviceA(P("COM1x"),P(text),64),2);
 EXPECT_ERROR(QueryDosDeviceA(P("COM1:x"),P(text),64),2);
 CHECK(QueryDosDeviceA(P("com12:"),P(text),64)==6&&equal(text,"com12:",7));
 EXPECT_ERROR(QueryDosDeviceA(P("COM1"),P(text),4),122);
 EXPECT_ERROR(DefineDosDeviceA(0,0,P("C:\\")),87);
 EXPECT_ERROR(DefineDosDeviceA(0,P("E:"),0),87);
 CHECK(DefineDosDeviceA(2,P("E:"),0)==1);
 CHECK(DefineDosDeviceA(2,P("e:"),P("C:\\"))==1);
 EXPECT_ERROR(DefineDosDeviceA(0,P("E:"),P("C:\\")),161);
 EXPECT_ERROR(DefineDosDeviceA(0,P("E:"),P("")),161);
 EXPECT_ERROR(DefineDosDeviceA(2,P("C:"),0),5);
 EXPECT_ERROR(DefineDosDeviceA(0,P("C:"),P("C:\\")),5);
 EXPECT_ERROR(DefineDosDeviceA(0,P("abc"),P("C:\\")),161);
 EXPECT_ERROR(DefineDosDeviceA(2,P("C"),0),161);
 EXPECT_ERROR(DefineDosDeviceA(2,P("1:"),0),161);
}
static void named_pipe_test(U file,U pipe_read,U pipe_write){
 U in=0x11223344,out=0,read=99,state=0,mode=0;
 EXPECT_ERROR(WaitNamedPipeA(P("\\\\.\\pipe\\probe"),100),2);
 EXPECT_ERROR(WaitNamedPipeA(P("\\\\server\\pipe\\probe"),~0u),2);
 EXPECT_ERROR(WaitNamedPipeA(P("probe"),0),253);
 EXPECT_ERROR(CallNamedPipeA(P("\\\\.\\pipe\\probe"),P(&in),4,P(&out),4,P(&read),0),2);
 CHECK(read==0);
 EXPECT_ERROR(CallNamedPipeA(P("pipe"),P(&in),4,P(&out),4,P(&read),0),253);
 EXPECT_ERROR(CallNamedPipeA(P("\\\\.\\pipe\\probe"),P(&in),4,P(&out),4,0,0),87);
 read=99;EXPECT_ERROR(TransactNamedPipe(file,P(&in),4,P(&out),4,P(&read),0),6);CHECK(read==99);
 read=99;EXPECT_ERROR(TransactNamedPipe(pipe_read,P(&in),4,P(&out),4,P(&read),0),12);CHECK(read==99);
 EXPECT_ERROR(TransactNamedPipe(pipe_write,P(&in),4,P(&out),4,P(&read),P(&state)),12);
 EXPECT_ERROR(TransactNamedPipe(0xdead,P(&in),4,P(&out),4,P(&read),0),6);
 EXPECT_ERROR(GetNamedPipeHandleStateA(pipe_read,P(&state),0,0,0,0,0),12);
 EXPECT_ERROR(GetNamedPipeHandleStateA(pipe_read,P(&state),0,0,0,P("user"),4),12);
 EXPECT_ERROR(GetNamedPipeHandleStateA(file,P(&state),0,0,0,0,0),6);
 EXPECT_ERROR(GetNamedPipeHandleStateA(0,P(&state),0,0,0,0,0),6);
 EXPECT_ERROR(SetNamedPipeHandleState(pipe_write,P(&mode),0,0),12);
 EXPECT_ERROR(SetNamedPipeHandleState(file,0,0,0),6);
 EXPECT_ERROR(GetNamedPipeInfo(pipe_read,P(&state),0,0,0),12);
 EXPECT_ERROR(GetNamedPipeInfo(file,P(&state),0,0,0),6);
 EXPECT_ERROR(GetNamedPipeInfo(0xdead,0,0,0,0),6);
 CHECK(state==0&&mode==0);
}
static void mailslot_test(U file,U event){
 U maximum=9,next=9,count=9,timeout=9,done=0;char text[64];
 U sa[3]={8,0,0};
 EXPECT_ERROR(CreateMailslotA(P("\\\\.\\mailslot\\probe"),0,0,P(sa))!=~0u,13);
 U h=CreateMailslotA(P("\\\\.\\mailslot\\probe"),0,0,0);CHECK(h!=~0u&&h);
 EXPECT_ERROR(CreateMailslotA(P("\\\\.\\MAILSLOT\\PROBE"),0,0,0)!=~0u,183);
 EXPECT_ERROR(CreateMailslotA(P("\\\\.\\mailslot\\"),0,0,0)!=~0u,3);
 EXPECT_ERROR(CreateMailslotA(P("\\\\.\\mailslot"),0,0,0)!=~0u,3);
 EXPECT_ERROR(CreateMailslotA(P("\\\\.\\other\\x"),0,0,0)!=~0u,3);
 EXPECT_ERROR(CreateMailslotA(P("\\\\server\\mailslot\\x"),0,0,0)!=~0u,123);
 EXPECT_ERROR(CreateMailslotA(P("C:\\mailslot\\x"),0,0,0)!=~0u,123);
 EXPECT_ERROR(CreateMailslotA(P("\\\\."),0,0,0)!=~0u,3);
 CHECK(GetMailslotInfo(h,P(&maximum),P(&next),P(&count),P(&timeout)));
 CHECK(maximum==0&&next==~0u&&count==0&&timeout==0);
 EXPECT_ERROR(GetMailslotInfo(file,P(&maximum),0,0,0),6);
 EXPECT_ERROR(GetMailslotInfo(event,P(&maximum),0,0,0),6);
 EXPECT_ERROR(SetMailslotInfo(file,5),6);
 CHECK(SetMailslotInfo(h,5)&&GetMailslotInfo(h,0,0,0,P(&timeout))&&timeout==5);
 CHECK(SetMailslotInfo(h,0));
 CHECK(GetFileType(h)==0);
 U c=CreateFileA(P("\\\\.\\MAILSLOT\\probe"),0x40000000,1,0,3,0,0);CHECK(c!=~0u);
 CHECK(WriteFile(c,P("hello"),5,P(&done),0)&&done==5);
 CHECK(WriteFile(c,P("ab"),2,P(&done),0)&&done==2);
 CHECK(GetMailslotInfo(h,P(&maximum),P(&next),P(&count),P(&timeout))&&maximum==0&&next==5&&count==2);
 EXPECT_ERROR(ReadFile(h,P(text),3,P(&done),0),111);CHECK(done==0);
 CHECK(GetMailslotInfo(h,0,P(&next),P(&count),0)&&next==5&&count==2);
 CHECK(ReadFile(h,P(text),64,P(&done),0)&&done==5&&equal(text,"hello",5));
 CHECK(ReadFile(h,P(text),64,P(&done),0)&&done==2&&equal(text,"ab",2));
 done=9;CHECK(ReadFile(h,P(text),64,P(&done),0)&&done==0);
 CHECK(GetMailslotInfo(h,0,P(&next),P(&count),0)&&next==~0u&&count==0);
 EXPECT_ERROR(WriteFile(h,P("x"),1,P(&done),0),5);
 EXPECT_ERROR(ReadFile(c,P(text),64,P(&done),0),5);
 CHECK(SetMailslotInfo(h,30));
 EXPECT_ERROR(ReadFile(h,P(text),64,P(&done),0),121);
 CHECK(SetMailslotInfo(h,~0u));
 U tid=0,thread=CreateThread(0,0,P(late_writer),c,0,P(&tid));CHECK(thread);
 CHECK(ReadFile(h,P(text),64,P(&done),0)&&done==4&&equal(text,"late",4)&&late_written);
 CHECK(WaitForSingleObject(thread,5000)==0);U code=0;CHECK(GetExitCodeThread(thread,P(&code))&&code==7&&CloseHandle(thread));
 CHECK(SetMailslotInfo(h,0));
 U orphan=CreateFileA(P("\\\\.\\mailslot\\nothere"),0x40000000,1,0,3,0,0);CHECK(orphan!=~0u);
 EXPECT_ERROR(WriteFile(orphan,P("x"),1,P(&done),0),2);
 CHECK(CloseHandle(orphan));
 U small=CreateMailslotA(P("\\\\.\\mailslot\\small"),4,0,0);CHECK(small!=~0u);
 CHECK(GetMailslotInfo(small,P(&maximum),0,0,0)&&maximum==4);
 U sc=CreateFileA(P("\\\\.\\mailslot\\small"),0x40000000,1,0,3,0,0);CHECK(sc!=~0u);
 EXPECT_ERROR(WriteFile(sc,P("hello"),5,P(&done),0),4);
 CHECK(WriteFile(sc,P("hell"),4,P(&done),0)&&done==4);
 CHECK(CloseHandle(sc)&&CloseHandle(small));
 EXPECT_ERROR(GetMailslotInfo(small,P(&maximum),0,0,0),6);
 static unsigned char big[4096];memset(big,0xa5,sizeof big);
 CHECK(WriteFile(c,P(big),4093,P(&done),0)&&done==4093);
 EXPECT_ERROR(WriteFile(c,P("x"),1,P(&done),0),121);
 CHECK(GetMailslotInfo(h,0,P(&next),P(&count),0)&&next==4093&&count==1);
 CHECK(ReadFile(h,P(big),4096,P(&done),0)&&done==4093&&big[4092]==0xa5);
 CHECK(WriteFile(c,P("x"),1,P(&done),0)&&done==1);
 CHECK(CloseHandle(h));
 EXPECT_ERROR(WriteFile(c,P("x"),1,P(&done),0),2);
 EXPECT_ERROR(GetMailslotInfo(h,P(&maximum),0,0,0),6);
 CHECK(CloseHandle(c));
 EXPECT_ERROR(CloseHandle(c),6);
 U again=CreateMailslotA(P("\\\\.\\mailslot\\probe"),0,0,0);CHECK(again!=~0u&&CloseHandle(again));
}
static void device_test(U file,U pipe_read,U event){
 U in[7],out[7],returned=0;
 EXPECT_ERROR(CreateFileA(P("\\\\.\\NOSUCH"),0,0,0,3,0x04000000,0)!=~0u,2);
 U d=CreateFileA(P("\\\\.\\VWIN32"),0,0,0,3,0x04000000,0);CHECK(d!=~0u);
 CHECK(GetFileType(d)==0);
 memset(in,0,sizeof in);memset(out,0xee,sizeof out);
 EXPECT_ERROR(DeviceIoControl(d,0,P(in),28,P(out),28,P(&returned),0),87);
 EXPECT_ERROR(DeviceIoControl(0xdead,1,P(in),28,P(out),28,P(&returned),0),6);
 EXPECT_ERROR(DeviceIoControl(file,1,P(in),28,P(out),28,P(&returned),0),87);
 EXPECT_ERROR(DeviceIoControl(pipe_read,1,P(in),28,P(out),28,P(&returned),0),87);
 EXPECT_ERROR(DeviceIoControl(event,1,P(in),28,P(out),28,P(&returned),0),87);
 EXPECT_ERROR(DeviceIoControl(d,7,P(in),28,P(out),28,P(&returned),0),50);
 EXPECT_ERROR(DeviceIoControl(d,1,P(in),10,P(out),28,P(&returned),0),87);
 U overlapped[5]={0,0,0,0,0x1234};
 in[3]=0x4408;in[0]=3;
 // A handle without FILE_FLAG_OVERLAPPED ignores lpOverlapped.
 CHECK(DeviceIoControl(d,1,P(in),28,P(out),28,P(&returned),P(overlapped))&&out[3]==1&&overlapped[0]==0);
 U od=CreateFileA(P("\\\\.\\VWIN32"),0,0,0,3,0x44000000,0);CHECK(od!=~0u&&od!=d);
 EXPECT_ERROR(DeviceIoControl(od,1,P(in),28,P(out),28,P(&returned),P(overlapped)),87);
 overlapped[4]=0;EXPECT_ERROR(DeviceIoControl(od,1,P(in),28,P(out),28,P(&returned),P(overlapped)),87);
 overlapped[4]=event;CHECK(DeviceIoControl(od,1,P(in),28,P(out),28,P(&returned),P(overlapped))&&out[3]==1&&overlapped[0]==event);
 CHECK(DeviceIoControl(od,1,P(in),28,P(out),28,P(&returned),0)&&out[3]==1&&CloseHandle(od));
 CHECK(DeviceIoControl(d,1,P(in),28,P(out),28,P(&returned),0)&&returned==28&&out[3]==1&&!(out[6]&1));
 CHECK(DeviceIoControl(d,1,P(in),28,P(out),28,0,0)&&out[3]==1);
 in[0]=0;CHECK(DeviceIoControl(d,1,P(in),28,P(out),28,P(&returned),0)&&out[3]==1);
 in[0]=4;CHECK(DeviceIoControl(d,1,P(in),28,P(out),28,P(&returned),0)&&(out[6]&1)&&out[3]==15);
 in[3]=0x4409;in[0]=3;in[1]=0xffff;CHECK(DeviceIoControl(d,1,P(in),28,P(out),28,P(&returned),0)&&!(out[6]&1)&&out[1]==0);
 in[3]=0x4400;CHECK(DeviceIoControl(d,1,P(in),28,P(out),28,P(&returned),0)&&(out[6]&1)&&out[3]==1);
 unsigned char mid[25];memset(mid,0xcc,sizeof mid);
 in[3]=0x440d;in[2]=0x0866;in[0]=3;in[1]=P(mid);
 CHECK(DeviceIoControl(d,1,P(in),28,P(out),28,P(&returned),0)&&!(out[6]&1)&&mid[0]==0&&mid[1]==0&&equal(mid+17,"NEO98   ",8));
 in[3]=0x7303;in[1]=P("C:\\");unsigned space[11];memset(space,0,sizeof space);in[4]=P(space);in[2]=44;
 CHECK(DeviceIoControl(d,6,P(in),28,P(out),28,P(&returned),0)&&!(out[6]&1)&&(space[0]&0xffff)==44&&space[1]==1&&space[2]==1&&space[4]>=space[3]&&space[3]>0);
 in[1]=P("D:\\");CHECK(DeviceIoControl(d,6,P(in),28,P(out),28,P(&returned),0)&&(out[6]&1)&&out[3]==15);
 in[1]=P("C:\\");in[2]=20;CHECK(DeviceIoControl(d,6,P(in),28,P(out),28,P(&returned),0)&&(out[6]&1)&&out[3]==87);
 in[3]=0x7305;in[2]=44;CHECK(DeviceIoControl(d,6,P(in),28,P(out),28,P(&returned),0)&&(out[6]&1)&&out[3]==1);
 in[3]=0x0201;CHECK(DeviceIoControl(d,2,P(in),28,P(out),28,P(&returned),0)&&(out[6]&1)&&out[3]==1);
 CHECK(DeviceIoControl(d,4,P(in),28,P(out),28,P(&returned),0)&&(out[6]&1));
 returned=5;CHECK(DeviceIoControl(d,~0u,P(in),28,P(out),28,P(&returned),0)&&returned==0);
 CHECK(CloseHandle(d));
 EXPECT_ERROR(DeviceIoControl(d,1,P(in),28,P(out),28,P(&returned),0),6);
}
void mainCRTStartup(void){
 U event=CreateEventA(0,1,0,0);CHECK(event);
 U file=CreateFileA(P("C:\\PIPEPROBE.DAT"),0xc0000000,0,0,2,0x80,0);CHECK(file!=~0u);
 U pipe_read=0,pipe_write=0;CHECK(CreatePipe(P(&pipe_read),P(&pipe_write),0,0));
 completion_test();context_test(event);vxd_handle_test(event);dos_device_test();
 named_pipe_test(file,pipe_read,pipe_write);mailslot_test(file,event);device_test(file,pipe_read,event);
 CHECK(CloseHandle(pipe_read)&&CloseHandle(pipe_write)&&CloseHandle(file)&&CloseHandle(event));
 ExitProcess(0);
}
