// SPDX-License-Identifier: GPL-2.0-or-later
// x86 guest probe of the KERNEL32 serial exports over the native null-modem
// pair COM1/COM2. Every check exits with its line number.
#include "kernel_comm_imports.h"
#define P(x) ((U)(x))
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
#define ERR(x,e) do{if((x)||GetLastError()!=(e))ExitProcess(__LINE__);}while(0)
enum{EV_RXCHAR=1,EV_RXFLAG=2,EV_TXEMPTY=4,EV_CTS=8,EV_DSR=0x10,EV_RLSD=0x20,EV_BREAK=0x40};
enum{MS_CTS=0x10,MS_DSR=0x20,MS_RLSD=0x80};
typedef struct{U internal,internal_high,offset,offset_high,event;}OVERLAPPED;
void *memset(void *p,int v,unsigned n){unsigned char*q=p;while(n--)*q++=v;return p;}
void *memcpy(void *p,const void *s,unsigned n){unsigned char*q=p;const unsigned char*r=s;while(n--)*q++=*r++;return p;}
static U equal(const void*a,const void*b,U n){const unsigned char*x=a,*y=b;while(n--)if(*x++!=*y++)return 0;return 1;}
static U h1,h2,hf,page;
static U __stdcall late_writer(U handle){U n;OVERLAPPED ov;memset(&ov,0,sizeof ov);Sleep(30);CHECK(WriteFile(handle,P("T"),1,P(&n),P(&ov))&&n==1);return 5;}
static void open_test(void){
 h1=CreateFileA(P("COM1"),0xC0000000,0,0,3,0,0);CHECK(h1!=~0u&&(h1>>24)==0x67);
 h2=CreateFileA(P("\\\\.\\COM2"),0xC0000000,0,0,3,0x40000000,0);CHECK(h2!=~0u&&h2!=h1);
 ERR(CreateFileA(P("COM3:"),0xC0000000,0,0,3,0,0)!=~0u,2);
 ERR(CreateFileA(P("com1:"),0xC0000000,0,0,3,0,0)!=~0u,5);
 CHECK(GetFileType(h1)==2);
 hf=CreateFileA(P("C:\\COMMTEST.TXT"),0xC0000000,0,0,2,0x80,0);CHECK(hf!=~0u);
 page=VirtualAlloc(0,4096,0x1000,4);CHECK(page);
}
static void invalid_handle_test(void){
 unsigned char dcb[28],prop[64],cc[52];U v=0,size=52,timeouts[5]={0};
 U handles[2]={0,0};handles[0]=hf;handles[1]=0x67000000;
 for(U i=0;i<2;++i){U h=handles[i];
  ERR(SetupComm(h,4096,4096),6);ERR(ClearCommBreak(h),6);ERR(SetCommBreak(h),6);ERR(EscapeCommFunction(h,3),6);
  ERR(PurgeComm(h,8),6);ERR(SetCommMask(h,1),6);ERR(TransmitCommChar(h,'x'),6);ERR(ClearCommError(h,P(&v),0),6);
  ERR(GetCommMask(h,P(&v)),6);ERR(GetCommProperties(h,P(prop)),6);ERR(GetCommModemStatus(h,P(&v)),6);
  ERR(GetCommState(h,P(dcb)),6);ERR(GetCommTimeouts(h,P(timeouts)),6);ERR(SetCommState(h,P(dcb)),6);
  ERR(SetCommTimeouts(h,P(timeouts)),6);ERR(WaitCommEvent(h,P(&v),0),6);ERR(GetCommConfig(h,P(cc),P(&size)),6);
  ERR(SetCommConfig(h,P(cc),52),6);
 }
 // The SEH probes run before the handle lookup: an unreadable buffer wins.
 ERR(GetCommState(hf,0),87);ERR(GetCommState(h1,0),87);ERR(SetCommState(h1,0),87);ERR(GetCommTimeouts(h1,0),87);
 ERR(SetCommTimeouts(h1,0),87);ERR(GetCommMask(h1,0),87);ERR(GetCommModemStatus(h1,0),87);ERR(GetCommProperties(h1,0),87);
 ERR(ClearCommError(h1,0,0),87);ERR(WaitCommEvent(h1,0,0),87);ERR(WaitCommEvent(h1,P(&v),page+4096-19),87);
 // First and last byte of the buffer: 28 bytes fit at the page end, 27 do not.
 ERR(GetCommState(h1,page+4096-27),87);CHECK(GetCommState(h1,page+4096-28));
 CHECK(ClearCommError(h1,P(&v),0));ERR(ClearCommError(h1,P(&v),page+4096-11),87);CHECK(ClearCommError(h1,P(&v),page+4096-12));
}
static void state_test(void){
 unsigned char dcb[28],copy[28];
 CHECK(GetCommState(h1,P(dcb))&&*(U*)dcb==28&&*(U*)(dcb+4)==9600&&dcb[18]==8&&(*(U*)(dcb+8)&1));
 memcpy(copy,dcb,28);*(U*)(copy+4)=230400;ERR(SetCommState(h1,P(copy)),6);
 CHECK(GetCommState(h1,P(dcb))&&*(U*)(dcb+4)==9600);
 memcpy(copy,dcb,28);*(U*)(copy+4)=19200;*(U*)(copy+8)&=~1u;copy[19]=2;copy[20]=2;
 CHECK(SetCommState(h1,P(copy))&&(*(U*)(copy+8)&1)); // forces fBinary in the caller's DCB
 CHECK(GetCommState(h1,P(dcb))&&*(U*)(dcb+4)==19200&&dcb[19]==2&&dcb[20]==2&&(*(U*)(dcb+8)&1));
 // SERIAL accepts divisors for non-enumerated baud rates and constrains 1.5 stop bits.
 *(U*)(copy+4)=12345;copy[19]=0;copy[20]=1;ERR(SetCommState(h1,P(copy)),6);
 copy[18]=5;*(U*)(copy+8)|=2;copy[12]=0x71;copy[26]=0x37;
 CHECK(SetCommState(h1,P(copy)));dcb[26]=0xa7;dcb[27]=0x5c;
 CHECK(GetCommState(h1,P(dcb))&&*(U*)(dcb+4)==12345&&dcb[18]==5&&dcb[20]==1);
 CHECK(!(*(U*)(dcb+8)&2)&&dcb[12]==0x71&&dcb[26]==0xa7&&dcb[27]==0x5c);
 *(U*)(copy+4)=128000;ERR(SetCommState(h1,P(copy)),6);*(U*)(copy+4)=19200;
 copy[18]=8;copy[20]=0;CHECK(SetCommState(h1,P(copy)));
 copy[18]=9;ERR(SetCommState(h1,P(copy)),6);copy[18]=8;copy[19]=5;ERR(SetCommState(h1,P(copy)),6);
 U timeouts[5]={1,2,3,4,5},back[5];CHECK(SetCommTimeouts(h1,P(timeouts))&&GetCommTimeouts(h1,P(back))&&equal(timeouts,back,20));
 memset(timeouts,0,sizeof timeouts);CHECK(SetCommTimeouts(h1,P(timeouts)));
}
static void queue_test(void){
 U n=0,err=0,stat[3],prop[16];unsigned char buffer[16];OVERLAPPED ov;memset(&ov,0,sizeof ov);
 CHECK(SetupComm(h1,8,0));CHECK(GetCommProperties(h1,P(prop)));
 CHECK((prop[0]&0xffff)==64&&prop[1]==1&&prop[5]==0x10000000&&prop[7]==255&&prop[8]==127&&prop[9]==0x1000ffff&&prop[6]==1&&prop[12]==8&&prop[11]==4096);
 ov.event=CreateEventA(0,1,0,0);CHECK(ov.event);
 CHECK(WriteFile(h2,P("0123456789"),10,P(&n),P(&ov))&&n==10);
 CHECK(GetOverlappedResult(h2,P(&ov),P(&n),0)&&n==10);
 CHECK(ClearCommError(h1,P(&err),P(stat))&&(err&1)&&stat[1]==8);
 CHECK(ClearCommError(h1,P(&err),P(stat))&&!err&&stat[1]==8);
 CHECK(ReadFile(h1,P(buffer),8,P(&n),0)&&n==8&&equal(buffer,"01234567",8));
 CHECK(SetupComm(h1,4096,4096));CHECK(GetCommProperties(h1,P(prop))&&prop[12]==4096);
 ERR(SetupComm(h1,1u<<21,0),6);
 // Timeouts: immediate return, then a total timeout that elapses.
 U timeouts[5]={~0u,0,0,0,0};CHECK(SetCommTimeouts(h1,P(timeouts)));
 CHECK(ReadFile(h1,P(buffer),4,P(&n),0)&&n==0);
 timeouts[0]=0;timeouts[2]=40;CHECK(SetCommTimeouts(h1,P(timeouts)));
 U start=GetTickCount();CHECK(ReadFile(h1,P(buffer),4,P(&n),0)&&n==0);CHECK(GetTickCount()-start>=30);
 // A partial read stops at the interval timeout once bytes exist.
 timeouts[0]=20;timeouts[2]=0;CHECK(SetCommTimeouts(h1,P(timeouts)));
 CHECK(WriteFile(h2,P("ab"),2,P(&n),P(&ov))&&n==2);
 CHECK(ReadFile(h1,P(buffer),8,P(&n),0)&&n==2&&equal(buffer,"ab",2));
 memset(timeouts,0,sizeof timeouts);CHECK(SetCommTimeouts(h1,P(timeouts)));
 ERR(ReadFile(h1,P(buffer),1,P(&n),P(&ov)),87); // OVERLAPPED on a non-overlapped port
 ERR(ReadFile(h2,P(buffer),1,P(&n),0),87);      // and none on an overlapped one
 ERR(WriteFile(h1,P(buffer),1,0,0),87);
 CHECK(CloseHandle(ov.event));
}
static void event_test(void){
 U n=0,mask=0x12345678;OVERLAPPED ov;memset(&ov,0,sizeof ov);ov.event=CreateEventA(0,1,0,0);CHECK(ov.event);
 CHECK(SetCommMask(h2,EV_RXCHAR|EV_TXEMPTY|EV_CTS|EV_DSR|EV_RLSD|EV_BREAK));
 CHECK(GetCommMask(h2,P(&mask))&&mask==(EV_RXCHAR|EV_TXEMPTY|EV_CTS|EV_DSR|EV_RLSD|EV_BREAK));
 ERR(SetCommMask(h2,0x200),6);
 CHECK(SetCommMask(h2,EV_RXCHAR));
 ERR(WaitCommEvent(h2,P(&mask),P(&ov)),997);CHECK(mask==0&&ov.internal==0&&ov.internal_high==P(&mask));
 CHECK(WaitForSingleObject(ov.event,0)==258);
 CHECK(WriteFile(h1,P("A"),1,P(&n),0)&&n==1);
 CHECK(WaitForSingleObject(ov.event,1000)==0&&mask==EV_RXCHAR);
 CHECK(GetOverlappedResult(h2,P(&ov),P(&n),1)&&n==P(&mask)); // Windows 98 stores the pointer in InternalHigh
 unsigned char byte=0;OVERLAPPED read;memset(&read,0,sizeof read);
 CHECK(ReadFile(h2,P(&byte),1,P(&n),P(&read))&&n==1&&byte=='A');
 // SetCommMask completes a pending wait with an empty mask.
 mask=7;ERR(WaitCommEvent(h2,P(&mask),P(&ov)),997);CHECK(SetCommMask(h2,EV_RXCHAR|EV_TXEMPTY));
 CHECK(WaitForSingleObject(ov.event,0)==0&&mask==0);
 // An empty mask cannot be waited for: VCOMM refuses, ring 3 reports 1.
 CHECK(SetCommMask(h2,0));ERR(WaitCommEvent(h2,P(&mask),P(&ov)),1);CHECK(SetCommMask(h2,EV_RXCHAR|EV_TXEMPTY));
 // Non-overlapped: a pending TXEMPTY returns at once, a late RXCHAR blocks.
 CHECK(SetCommMask(h1,EV_TXEMPTY|EV_RXCHAR|EV_CTS|EV_DSR|EV_RLSD|EV_BREAK));
 CHECK(WriteFile(h1,P("B"),1,P(&n),0)&&n==1);
 CHECK(WaitCommEvent(h1,P(&mask),0)&&mask==EV_TXEMPTY);
 U id=0,thread=CreateThread(0,0,P(late_writer),h2,0,P(&id));CHECK(thread);
 CHECK(WaitCommEvent(h1,P(&mask),0)&&(mask&EV_RXCHAR));
 CHECK(WaitForSingleObject(thread,5000)==0);CHECK(CloseHandle(thread));
 CHECK(ReadFile(h1,P(&byte),1,P(&n),0)&&n==1&&byte=='T');
 CHECK(ReadFile(h2,P(&byte),1,P(&n),P(&read))&&n==1&&byte=='B');
 CHECK(CloseHandle(ov.event));
}
static void modem_test(void){
 U ms=0,mask=0,err=0,stat[3],n=0;unsigned char byte=0;OVERLAPPED read;memset(&read,0,sizeof read);
 CHECK(EscapeCommFunction(h2,6)&&EscapeCommFunction(h2,4));CHECK(GetCommModemStatus(h1,P(&ms))&&ms==0);
 CHECK(WaitCommEvent(h1,P(&mask),0)&&(mask&(EV_DSR|EV_RLSD)));
 CHECK(EscapeCommFunction(h2,3));CHECK(GetCommModemStatus(h1,P(&ms))&&ms==MS_CTS);
 CHECK(WaitCommEvent(h1,P(&mask),0)&&mask==EV_CTS);
 CHECK(EscapeCommFunction(h2,5));CHECK(GetCommModemStatus(h1,P(&ms))&&ms==(MS_CTS|MS_DSR|MS_RLSD));
 CHECK(WaitCommEvent(h1,P(&mask),0)&&mask==(EV_DSR|EV_RLSD));
 ERR(EscapeCommFunction(h1,99),6);CHECK(EscapeCommFunction(h1,14)==1);CHECK(EscapeCommFunction(h1,1)&&EscapeCommFunction(h1,2)&&EscapeCommFunction(h1,7));
 CHECK(SetCommBreak(h2));CHECK(ClearCommError(h1,P(&err),P(stat))&&(err&0x10));
 CHECK(WaitCommEvent(h1,P(&mask),0)&&mask==EV_BREAK);CHECK(ClearCommBreak(h2));
 CHECK(WriteFile(h2,P("xyz"),3,P(&n),P(&read))&&n==3);
 CHECK(ClearCommError(h1,P(&err),P(stat))&&stat[1]==3);CHECK(PurgeComm(h1,8));
 CHECK(ClearCommError(h1,P(&err),P(stat))&&stat[1]==0);ERR(PurgeComm(h1,0x10),6);
 CHECK(TransmitCommChar(h2,'Z'));CHECK(ReadFile(h1,P(&byte),1,P(&n),0)&&n==1&&byte=='Z');
 CHECK(WaitCommEvent(h1,P(&mask),0)&&(mask&EV_RXCHAR));
}
static void config_test(void){
 unsigned char cc[52],dcb[28];U size=52;
 CHECK(GetCommConfig(h1,P(cc),P(&size))&&size==52&&*(U*)cc==52&&*(unsigned short*)(cc+4)==1&&*(U*)(cc+36)==1);
 CHECK(GetCommState(h1,P(dcb))&&equal(cc+8,dcb,26));
 size=10;ERR(GetCommConfig(h1,P(cc),P(&size)),1);CHECK(size==52);
 ERR(GetCommConfig(h1,P(cc),0),1);
 *(U*)(cc+12)=2400;CHECK(SetCommConfig(h1,P(cc),52));CHECK(GetCommState(h1,P(dcb))&&*(U*)(dcb+4)==2400);
 ERR(SetCommConfig(h1,P(cc),10),1);*(U*)(cc+12)=230400;ERR(SetCommConfig(h1,P(cc),52),1);
 // Provider lookup: the installed hive names COM1/COM2, nothing else.
 size=52;ERR(GetDefaultCommConfigA(P("COM9"),P(cc),P(&size)),87);
 ERR(SetDefaultCommConfigA(P("COM9"),P(cc),52),87);ERR(CommConfigDialogA(P("COM9"),0,P(cc)),87);
 ERR(GetDefaultCommConfigA(P("COM1"),0,P(&size)),87);ERR(CommConfigDialogA(P("COM1"),0,0),87);
 // The installed provider is a 16-bit DLL. Its native port must succeed.
 size=51;ERR(GetDefaultCommConfigA(P("COM1"),P(cc),P(&size)),122);CHECK(size==52);
 CHECK(GetDefaultCommConfigA(P("COM1"),P(cc),P(&size))&&size==52);
 CHECK(*(U*)cc==52&&*(unsigned short*)(cc+4)==1&&*(U*)(cc+8)==28&&*(U*)(cc+36)==1);
 CHECK(GetDefaultCommConfigA(P("Communications Port (COM2)"),P(cc),P(&size)));
 *(U*)(cc+12)=4800;cc[26]=7;cc[27]=2;cc[28]=2;
 // SERIALUI checks cc.dwSize, not the final dwSize parameter or live-port DCB validity.
 CHECK(SetDefaultCommConfigA(P("com2"),P(cc),0));
 CHECK(GetDefaultCommConfigA(P("COM2"),P(cc),P(&size))&&*(U*)(cc+12)==4800&&cc[26]==7&&cc[27]==2&&cc[28]==2);
 char text[64];CHECK(GetProfileStringA(P("ports"),P("COM2:"),P("missing"),P(text),64)>0);
 CHECK(equal(text,"4800,e,7,2,",11));
 // The modal success paths have an interactive guest fixture.

}
static void close_test(void){
 U mask=0,ms=0xff;OVERLAPPED read;memset(&read,0,sizeof read);
 CHECK(CloseHandle(h2));ERR(CloseHandle(h2),6);ERR(GetCommState(h2,P(mask?0:&mask)),6);
 CHECK(GetCommModemStatus(h1,P(&ms))&&ms==0);
 CHECK(WaitCommEvent(h1,P(&mask),0)&&(mask&(EV_CTS|EV_DSR|EV_RLSD)));
 h2=CreateFileA(P("COM2"),0xC0000000,0,0,3,0,0);CHECK(h2!=~0u);CHECK(CloseHandle(h2)&&CloseHandle(h1)&&CloseHandle(hf));
}
void mainCRTStartup(void){
 open_test();invalid_handle_test();state_test();queue_test();event_test();modem_test();config_test();close_test();
 ExitProcess(0);
}
