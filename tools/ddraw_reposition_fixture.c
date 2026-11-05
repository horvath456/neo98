/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* another process enters DirectDraw from WINDOWPOSCHANGING. */
typedef unsigned U;
void *memset(void *dst,int value,unsigned count){unsigned char *p=dst;while(count--)*p++=(unsigned char)value;return dst;}
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API char *GetCommandLineA(void);
API U GetModuleFileNameA(U,char *,U);
API U CreateProcessA(const char *,char *,void *,void *,U,U,void *,const char *,void *,void *);
API void Sleep(U);
API U GetTickCount(void);
API U RegisterClassA(void *);
API U CreateWindowExA(U,const char *,const char *,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);
API U GetMessageA(void *,U,U,U);
API U DispatchMessageA(void *);
API U FindWindowA(const char *,const char *);
API U SendMessageA(U,U,U,U);
API U MoveWindow(U,int,int,int,int,U);
API U SetForegroundWindow(U);
API U ChangeDisplaySettingsA(void *,U);
API U DirectDrawCreate(void *,void **,void *);
API U WaitForSingleObject(U,U);
API U GetExitCodeProcess(U,U *);
typedef U (__stdcall *M1)(void *);
typedef U (__stdcall *M2)(void *,void *);
typedef U (__stdcall *M3)(void *,U,U);
typedef U (__stdcall *M4)(void *,U,U,U);
typedef U (__stdcall *M5)(void *,void *,void *,U,U);
#define CALL(o,t,i) ((t)(*(void ***)(o))[i])
static void *draw,*surface;
static U armed,positions,locks;
static void require(U ok,U code){if(!ok)ExitProcess(code);}
static U __stdcall proc(U h,U m,U w,U l){
 if(m==0x8001){armed=0;MoveWindow(h,900,700,180,120,1);armed=1;return 1;}
 if(m==0x8002)return positions;
 if(m==0x8003)return locks;
 if(m==0x8004)ExitProcess(0);
 if(m==0x8005)return surface!=0;
 if(m==0x8006){armed=0;MoveWindow(h,400,300,180,120,1);armed=1;return 1;}
 if(m==0x46 && armed && surface){
  U desc[27]={108};++positions;
  U result=CALL(surface,M5,25)(surface,0,desc,1,0);
  require(!result,21);++locks;require(!CALL(surface,M2,32)(surface,0),22);
 }
 return DefWindowProcA(h,m,w,l);
}
void mainCRTStartup(void){
 char *cmd=GetCommandLineA();U child=0;
 for(U i=0;cmd[i];++i)if(cmd[i]=='!')child=1;
 U klass[10]={0};klass[1]=(U)proc;klass[4]=GetModuleHandleA(0);
 klass[9]=(U)(child?"NEO98RepositionChild":"NEO98RepositionParent");
 require(RegisterClassA(klass),1);
 U window=CreateWindowExA(0,(char *)klass[9],"reposition",0x90c00000,20,20,180,120,0,0,klass[4],0);
 require(window,2);require(!DirectDrawCreate(0,&draw,0)&&draw,3);
 if(child){
  require(!CALL(draw,M3,20)(draw,window,8),4);
  U desc[27]={108};desc[1]=7;desc[2]=16;desc[3]=16;desc[26]=0x840;
  require(!CALL(draw,M4,6)(draw,(U)desc,(U)&surface,0)&&surface,5);
  U msg[7];while(GetMessageA(msg,0,0,0))DispatchMessageA(msg);ExitProcess(6);
 }
 char exe[260]={0},command[280]={0};U startup[17]={sizeof startup},process[4]={0},i;
 require(GetModuleFileNameA(0,exe,sizeof exe),7);
 command[0]='"';for(i=0;exe[i];++i)command[i+1]=exe[i];
 command[++i]='"';command[++i]=' ';command[++i]='!';command[++i]=0;
 require(CreateProcessA(exe,command,0,0,0,0,0,0,startup,process),8);
 U other=0,start=GetTickCount();
 while(!(other=FindWindowA("NEO98RepositionChild",0))&&GetTickCount()-start<5000)Sleep(10);
 require(other,9);
 while(!SendMessageA(other,0x8005,0,0)&&GetTickCount()-start<5000)Sleep(10);
 require(SendMessageA(other,0x8005,0,0),9);
 SendMessageA(other,0x8001,0,0);SetForegroundWindow(window);
 require(!CALL(draw,M3,20)(draw,window,0x11),10);
 for(i=0;i<3;++i){
  require(!CALL(draw,M4,21)(draw,1024,768,16),11);
  SendMessageA(other,0x8001,0,0);
  U before=SendMessageA(other,0x8002,0,0);
  require(!CALL(draw,M4,21)(draw,640,480,16),12);
  U count=SendMessageA(other,0x8002,0,0);
  /* USER: CDS_FULLSCREEN preserves positions on this monitor. */
  require(count==before,13);
 }
 /* Keep the foreign window inside the small monitor for restoration too. */
 SendMessageA(other,0x8006,0,0);
 require(!CALL(draw,M1,19)(draw),14);CALL(draw,M3,20)(draw,window,8);
 require(SendMessageA(other,0x8002,0,0)==SendMessageA(other,0x8003,0,0),20);
 /* Ordinary changes DO synchronously reposition foreign windows. Prove
    the callback runs and can lock DirectDraw, rather than merely timing out. */
 U mode[39]={0};((unsigned short *)mode)[18]=sizeof mode;
 mode[10]=0x1c0000;mode[26]=16;
 for(i=0;i<3;++i){
  mode[27]=1024;mode[28]=768;require(!ChangeDisplaySettingsA(mode,0),16);
  SendMessageA(other,0x8001,0,0);
  U before=SendMessageA(other,0x8002,0,0);
  mode[27]=640;mode[28]=480;require(!ChangeDisplaySettingsA(mode,0),17);
  U count=SendMessageA(other,0x8002,0,0);
  require(count>before&&SendMessageA(other,0x8003,0,0)==count,18);
 }
 require(!ChangeDisplaySettingsA(0,0),19);
 SendMessageA(other,0x8004,0,0);
 U code=~0u;require(WaitForSingleObject(process[0],5000)==0&&GetExitCodeProcess(process[0],&code)&&!code,15);
 ExitProcess(0);
}
