/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* a 16-bit primary updated at 60 Hz; no guest busy wait. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API U GetTickCount(void);
API U RegisterClassA(void *);
API U CreateWindowExA(U,const char *,const char *,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);
API U ShowCursor(int);
API U DirectDrawCreate(void *,void **,void *);
typedef U (__stdcall *M1)(void *);
typedef U (__stdcall *M2)(void *,void *);
typedef U (__stdcall *M3)(void *,U,U);
typedef U (__stdcall *M4)(void *,U,U,U);
typedef U (__stdcall *M5)(void *,void *,void *,U,U);
#define CALL(o,t,i) ((t)(*(void ***)(o))[i])
static void check(U ok,U code){if(!ok)ExitProcess(code);}
static U __stdcall proc(U h,U m,U w,U l){return DefWindowProcA(h,m,w,l);}
void mainCRTStartup(void){
 U c[10]={0};c[1]=(U)proc;c[9]=(U)"ScanoutCPU";check(RegisterClassA(c),1);
 U h=CreateWindowExA(0,(char *)c[9],"Scanout CPU",0x90000000,0,0,640,480,0,0,0,0);check(h,2);
 ShowCursor(0);
 void *dd=0,*primary=0;check(!DirectDrawCreate(0,&dd,0),3);
 check(!CALL(dd,M3,20)(dd,h,0x11),4);check(!CALL(dd,M4,21)(dd,640,480,16),5);
 U d[27]={108};d[1]=1;d[26]=0x200;check(!CALL(dd,M4,6)(dd,(U)d,(U)&primary,0),6);
 U start=GetTickCount();
 for(U f=0;f<600;++f){
  d[0]=108;check(!CALL(primary,M5,25)(primary,0,d,1,0),7);
  /* One row changes; the host still converts the complete primary. */
  volatile unsigned short *p=(volatile unsigned short *)(d[9]+(f%480)*d[4]);
  for(U x=0;x<640;++x)p[x]=(f&1)?0x7c00:0x03e0;
  check(!CALL(primary,M2,32)(primary,0),8);
  U target=start+(f+1)*1000/60,now=GetTickCount();if(target>now)Sleep(target-now);
 }
 CALL(primary,M1,2)(primary);CALL(dd,M1,19)(dd);CALL(dd,M1,2)(dd);ExitProcess(0);
}
