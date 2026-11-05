/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "pipe_imports.h"
#define P(x) ((U)(x))
#define CHECK(x,n) do{if(!(x))ExitProcess(n);}while(0)
static U r,w,done,reader_error;
static unsigned char bytes[8192],received[8192];
static U __stdcall writer(U ignored){U count=0;(void)ignored;CHECK(WriteFile(w,P(bytes),sizeof bytes,P(&count),0)&&count==sizeof bytes,70);CHECK(CloseHandle(w),71);return 19;}
static U __stdcall reader(U ignored){U count=0;(void)ignored;CHECK(!ReadFile(r,P(received),1,P(&count),0),72);reader_error=GetLastError();return 0;}
void mainCRTStartup(void){
#ifdef PIPE_CHILD
 U n=0;CHECK(WriteFile(GetStdHandle((U)-11),P("child"),5,P(&n),0)&&n==5,90);ExitProcess(17);
#else
 U n=0,available=0,left=9,dup=0,id=0,code=0;U security[]={12,0,1};
 U file=CreateFileA(P("C:\\attrs.bin"),0xc0000000,3,0,2,0x80,0);CHECK(file!=(U)-1,40);
 CHECK(WriteFile(file,P("abc"),3,P(&n),0)&&SetFilePointer(file,0,0,0)==0,41);
 CHECK(DuplicateHandle(GetCurrentProcess(),file,GetCurrentProcess(),P(&dup),0x80000000,0,0),42);
 CHECK(!SetFileTime(dup,0,0,0)&&GetLastError()==5,43);
 CHECK(ReadFile(dup,P(received),1,P(&n),0)&&received[0]=='a',44);
 CHECK(ReadFile(file,P(received),1,P(&n),0)&&received[0]=='b',45);
 CHECK(CloseHandle(file)&&ReadFile(dup,P(received),1,P(&n),0)&&received[0]=='c'&&CloseHandle(dup),46);
 file=CreateFileA(P("C:\\attrs.bin"),0x100,3,0,3,0x02000080,0);CHECK(file!=(U)-1,47);
 U time[2];CHECK(GetFileTime(file,0,0,P(time))&&SetFileTime(file,0,0,P(time)),48);
 CHECK(!WriteFile(file,P("z"),1,P(&n),0)&&GetLastError()==5&&CloseHandle(file),49);
 CHECK(!CreatePipe(0,P(&w),0,0)&&GetLastError()==87,1);
 CHECK(CreatePipe(P(&r),P(&w),0,4),2);CHECK(GetFileType(r)==3&&GetFileType(w)==3,3);
 CHECK(WriteFile(w,P("abc"),3,P(&n),0)&&n==3,4);
 CHECK(PeekNamedPipe(r,P(received),2,P(&n),P(&available),P(&left))&&n==2&&available==3&&left==0&&received[0]=='a'&&received[1]=='b',5);
 CHECK(ReadFile(r,P(received),2,P(&n),0)&&n==2&&received[0]=='a',6);
 CHECK(ReadFile(r,P(received),8,P(&n),0)&&n==1&&received[0]=='c',7);
 CHECK(DuplicateHandle(GetCurrentProcess(),w,GetCurrentProcess(),P(&dup),0,0,2),8);
 CHECK(CloseHandle(w)&&!CloseHandle(w)&&GetLastError()==6,9);
 CHECK(WriteFile(dup,P("z"),1,P(&n),0)&&CloseHandle(dup),10);
 CHECK(ReadFile(r,P(received),1,P(&n),0)&&received[0]=='z',11);
 CHECK(!ReadFile(r,P(received),1,P(&n),0)&&GetLastError()==109&&n==0,12);CHECK(CloseHandle(r),13);
 CHECK(CreatePipe(P(&r),P(&w),0,64),14);
 for(U i=0;i<sizeof bytes;++i)bytes[i]=(unsigned char)(i*17+3);
 U thread=CreateThread(0,0,P(writer),0,0,P(&id));CHECK(thread,15);
 U total=0;while(total<sizeof bytes){CHECK(ReadFile(r,P(received)+total,sizeof bytes-total,P(&n),0)&&n,16);total+=n;}
 CHECK(WaitForSingleObject(thread,5000)==0,17);CHECK(GetExitCodeThread(thread,P(&code))&&code==19,18);
 for(U i=0;i<sizeof bytes;++i)CHECK(bytes[i]==received[i],19);
 CHECK(CloseHandle(thread)&&CloseHandle(r),20);
 CHECK(CreatePipe(P(&r),P(&w),0,4),21);thread=CreateThread(0,0,P(reader),0,0,P(&id));CHECK(thread,22);
 Sleep(10);CHECK(CloseHandle(w),23);CHECK(WaitForSingleObject(thread,5000)==0&&reader_error==109,24);CHECK(CloseHandle(thread)&&CloseHandle(r),25);
 CHECK(CreatePipe(P(&r),P(&w),P(security),64),26);
 CHECK(DuplicateHandle(GetCurrentProcess(),r,GetCurrentProcess(),P(&dup),0,0,3),27);r=dup;
 U startup[17]={68},process[4]={0};startup[11]=0x100;startup[15]=w;startup[16]=w;
 CHECK(CreateProcessA(P("C:\\WINDOWS\\PIPECHILD.EXE"),0,0,0,1,0,0,0,P(startup),P(process)),28);
 CHECK(CloseHandle(w),29);CHECK(ReadFile(r,P(received),8,P(&n),0)&&n==5,30);
 CHECK(received[0]=='c'&&received[1]=='h'&&received[2]=='i'&&received[3]=='l'&&received[4]=='d',31);
 CHECK(!ReadFile(r,P(received),1,P(&n),0)&&GetLastError()==109,32);
 CHECK(WaitForSingleObject(process[0],5000)==0,33);CHECK(GetExitCodeProcess(process[0],P(&code))&&code==17,34);
 CHECK(CloseHandle(process[0])&&CloseHandle(process[1])&&CloseHandle(r),35);
 ExitProcess(0);
#endif
}
