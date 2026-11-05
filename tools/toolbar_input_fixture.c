/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
typedef unsigned U;typedef struct{U hwnd,message,wp,lp,time;int x,y;} MSG;typedef struct{U style,proc,extra,classExtra,instance,icon,cursor,brush;const char*menu,*name;} CLASS;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetModuleHandleA(const char*);API void InitCommonControls(void);API U RegisterClassA(CLASS*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);API U DestroyWindow(U);API U DefWindowProcA(U,U,U,U);API U SendMessageA(U,U,U,U);API U GetWindowLongA(U,int);API U SetWindowTextA(U,const char*);API U GetMessageA(MSG*,U,U,U);API U TranslateMessage(MSG*);API U DispatchMessageA(MSG*);API U GetCapture(void);API U SetCapture(U);API U ReleaseCapture(void);
static U bar,parent,commands,trace[256],used,veto,destroy_release;
static void check(U v,U code){if(!v)ExitProcess(code);}
static void event(U code){if(used<256)trace[used++]=code;}
static U state(U i){return *(unsigned char*)(*(U*)(GetWindowLongA(bar,0)+0x30)+28*i+8);}
static U hot(void){return *(U*)(GetWindowLongA(bar,0)+0xcc);}
static U __stdcall proc(U h,U m,U wp,U lp){
 if(m==0x4e){U*n=(U*)lp;event(n[2]);if(n[2]==0xfffffd37&&veto)return 1;if(n[2]==~15u&&destroy_release){check(DestroyWindow(bar),17501);bar=0;}return 0;}
 if(m==0x111){check(lp==bar&&wp>=101&&wp<=106,17502);event(0x111);++commands;return 0;}
 if(m==0x401){
  if(wp==1)check(hot()==0&&commands==0,17503);
  if(wp==2)check((state(0)&2)&&GetCapture()==bar&&trace[used-2]==0xfffffd43&&trace[used-1]==~19u,17504);
  if(wp==3)check(!(state(0)&2)&&GetCapture()==bar&&commands==0,17505);
  if(wp==4)check(state(0)&2,17506);
  if(wp==5)check(!GetCapture()&&!(state(0)&2)&&commands==1&&trace[used-4]==~15u&&trace[used-3]==0xfffffd42&&trace[used-2]==0x111&&trace[used-1]==~1u,17507);
  if(wp==6)check(commands==1&&!GetCapture()&&!(state(1)&2),17508);
  if(wp==7)check(commands==1&&!GetCapture(),17509);
  if(wp==8)check(commands==2&&(state(3)&1),17510);
  if(wp==9){check(commands==3&&(state(4)&1)&&!(state(5)&1),17511);veto=1;}
  if(wp==10){check(hot()==4,17512);veto=0;}
  if(wp==11){check(GetCapture()==bar&&(state(0)&2),17513);SetCapture(parent);ReleaseCapture();}
  if(wp==12)check(!GetCapture()&&!(state(0)&2)&&commands==3,17514);
  if(wp==13){check(GetCapture()==bar,17515);destroy_release=1;}
  if(wp==14){check(!bar&&!GetCapture()&&commands==3&&trace[used-1]==~15u,17516);ExitProcess(0);}
  char title[16]="stage00";title[5]='0'+wp/10;title[6]='0'+wp%10;SetWindowTextA(h,title);return 0;
 }
 return DefWindowProcA(h,m,wp,lp);
}
void mainCRTStartup(void){InitCommonControls();U instance=GetModuleHandleA(0);CLASS c={0,(U)proc,0,0,instance,0,0,6,0,"TOOLBAR_INPUT_PARENT"};check(RegisterClassA(&c),17517);parent=CreateWindowExA(0,c.name,"ready",0x90000000,30,30,480,160,0,0,instance,0);check(parent,17518);bar=CreateWindowExA(0,"ToolbarWindow32","",0x5000086c,4,4,450,60,parent,77,instance,0);check(bar,17519);
 SendMessageA(bar,0x41e,20,0);U buttons[30]={~1u,101,4,0,(U)"Push",~1u,102,0,0,(U)"Disabled",8,103,0x104,0,~0u,~1u,104,0x204,0,(U)"Check",~1u,105,0x604,0,(U)"Radio A",~1u,106,0x605,0,(U)"Radio B"};check(SendMessageA(bar,0x414,6,(U)buttons),17520);
 check(!SendMessageA(bar,0x455,0,0)&&!SendMessageA(bar,0x454,0,0x10)&&SendMessageA(bar,0x455,0,0)==0x10&&SendMessageA(bar,0x454,0x10,0)==0x10&&!SendMessageA(bar,0x455,0,0),17521);
 check(SendMessageA(bar,0x41f,0,0x0030003c),17522);MSG msg;while(GetMessageA(&msg,0,0,0)){TranslateMessage(&msg);DispatchMessageA(&msg);}ExitProcess(17523);
}
