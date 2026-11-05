// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API U CreateDialogIndirectParamA(U,const void*,U,U,U);API U DialogBoxIndirectParamA(U,const void*,U,U,U);
API U CreateDialogIndirectParamW(U,const void*,U,U,U);API U DialogBoxIndirectParamW(U,const void*,U,U,U);
API U GetModuleHandleA(const char*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);
API U GetWindow(U,U);API U GetDlgItem(U,U);API U GetWindowLongA(U,int);API U GetWindowTextA(U,char*,int);
API U IsWindow(U);API U IsWindowEnabled(U);API U EnableWindow(U,int);API U DestroyWindow(U);API U EndDialog(U,U);
API U VirtualAlloc(void*,U,U,U);API U VirtualProtect(void*,U,U,U*);API U VirtualFree(void*,U,U);API U GetLastError(void);API void SetLastError(U);API void ExitProcess(U);
static void check(int okay,U code){if(!okay)ExitProcess(code);}
static int same(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
API U SetWindowLongA(U,int,U);API U SendMessageA(U,U,U,U);API U CallWindowProcA(U,U,U,U,U);API U DefDlgProcA(U,U,U,U);
API U GetDC(U);API U ReleaseDC(U,U);API U CreateSolidBrush(U);API U DeleteObject(U);API U GetPixel(U,int,int);
API U GetSysColorBrush(U);API U GetBkColor(U);API U GetTextColor(U);API U FillRect(U,const int*,U);
API U DefWindowProcA(U,U,U,U);API U SetCapture(U);API U ReleaseCapture(void);
static U button_proc,latch;
static U __stdcall button_subclass(U h,U m,U w,U l){if(m==0xf3&&!w&&latch)return DefWindowProcA(h,m,w,l);return CallWindowProcA(button_proc,h,m,w,l);}
static U background;
static U old_proc,sub_calls,dialog_calls,commands;
static U __stdcall proc(U h,U m,U w,U l){(void)w;(void)l;if(m==0x136)return background;if(m==0x110)return 1;if(m==0x8001){++dialog_calls;SetWindowLongA(h,0,0x2468ace0);return 1;}if(m==0x111){++commands;return 0;}return 0;}
static U __stdcall subclass(U h,U m,U w,U l){if(m==0x8001)++sub_calls;return CallWindowProcA(old_proc,h,m,w,l);}
static void word(unsigned char*b,U*at,U v){b[(*at)++]=(unsigned char)v;b[(*at)++]=(unsigned char)(v>>8);}
static void dword(unsigned char*b,U*at,U v){word(b,at,v);word(b,at,v>>16);}
static void string(unsigned char*b,U*at,const char*s){do{word(b,at,(unsigned char)*s);}while(*s++);}
static U make(unsigned char*b){U at=0;dword(b,&at,0x90c80080);dword(b,&at,0);word(b,&at,1);word(b,&at,0);word(b,&at,0);word(b,&at,100);word(b,&at,60);word(b,&at,0);word(b,&at,0);string(b,&at,"Indirect");while(at&3)b[at++]=0;dword(b,&at,0x50010000);dword(b,&at,0);word(b,&at,5);word(b,&at,5);word(b,&at,40);word(b,&at,14);word(b,&at,11);word(b,&at,0xffff);word(b,&at,0x80);string(b,&at,"Button");word(b,&at,0);return at;}
void mainCRTStartup(void){
 unsigned char data[256];make(data);U instance=GetModuleHandleA(0);
 U h=CreateDialogIndirectParamA(instance,data,0,(U)proc,0);check(h,40430);
 old_proc=SetWindowLongA(h,-4,(U)subclass);check(old_proc,40431);
 check(SendMessageA(h,0x8001,0,0)==0x2468ace0&&sub_calls==1&&dialog_calls==1,40432);
 SendMessageA(h,0x111,1,GetDlgItem(h,11));check(IsWindow(h)&&commands==1,40433);
 check(DefDlgProcA(h,0x8001,0,0)==0x2468ace0&&sub_calls==1&&dialog_calls==2,40434);
 check(CallWindowProcA(old_proc,h,0x8001,0,0)==0x2468ace0&&sub_calls==1&&dialog_calls==3,40435);

 // DefWindowProc supplies the brushes COMCTL32 uses to erase controls.
 U color_dc=GetDC(h);check(color_dc,40452);
 for(U type=0;type<7;++type){
  U brush=DefWindowProcA(h,0x132+type,color_dc,h);check(brush,40453+type);
  U expected=(type==1||type==2)?0xffffff:0xc0c0c0;
  int area[4]={0,0,8,8};check(FillRect(color_dc,area,brush),40460+type);
  if(type!=5){check(GetBkColor(color_dc)==expected,40467+type);check(GetPixel(color_dc,2,2)==expected,40474+type);check(GetTextColor(color_dc)==0,40481+type);}
  else {check(GetBkColor(color_dc)==0xffffff&&GetTextColor(color_dc)==0xc0c0c0,40488);check(GetPixel(color_dc,0,0)!=GetPixel(color_dc,1,0),40489);}
 }
 check(!GetSysColorBrush(31),40493);
 ReleaseDC(h,color_dc);
 // Exhaust USER handles without killing the process, then release every one.
 static U handles[1100];U count=0;
 while(count<1100){U next=CreateWindowExA(0,"STATIC","",0,0,0,1,1,0,0,instance,0);if(!next)break;handles[count++]=next;}
 check(count>192&&count<1100&&GetLastError()==8,40490);
 while(count)check(DestroyWindow(handles[--count]),40491);
 check(!CreateWindowExA(0,"UnknownNeo98Class","",0,0,0,1,1,0,0,instance,0)&&GetLastError()==1407,40492);
 U button=GetDlgItem(h,11);
 button_proc=SetWindowLongA(button,-4,(U)button_subclass);check(button_proc,40447);
 for(U round=0;round<3;++round){
  latch=1;SendMessageA(button,0x201,1,0x00010001);
  check(SendMessageA(button,0xf2,0,0)&4,40448);
  SetCapture(h);check(SendMessageA(button,0xf2,0,0)&4,40449);
  latch=0;SendMessageA(button,0xf3,0,0);ReleaseCapture();
  check(!(SendMessageA(button,0xf2,0,0)&4),40450);
 }
 SendMessageA(button,0x201,1,0x00010001);SetCapture(h);
 check(!(SendMessageA(button,0xf2,0,0)&4),40451);ReleaseCapture();
 U dc=GetDC(h);check(dc,40437);
 check(SendMessageA(h,0x14,dc,0)==1,40438);
 check(GetPixel(dc,5,40)==0xc0c0c0,40439);
 background=CreateSolidBrush(0x123456);check(background,40440);
 check(SendMessageA(h,0x14,dc,0)==1,40441);
 check(GetPixel(dc,5,40)==0x123456,40442);
 ReleaseDC(h,dc);
 // A property page is a child dialog with the same erase contract.
 *(U*)data=0x50000080;
 U child=CreateDialogIndirectParamA(instance,data,h,(U)proc,0);check(child,40443);
 dc=GetDC(child);check(dc,40444);
 check(SendMessageA(child,0x14,dc,0)==1,40445);
 check(GetPixel(dc,5,40)==0x123456,40446);
 ReleaseDC(child,dc);DestroyWindow(child);
 check(DestroyWindow(h),40436);DeleteObject(background);ExitProcess(0);
}
