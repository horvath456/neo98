// SPDX-License-Identifier: GPL-2.0-or-later
// The standard controls under the wheel in one window (tools/test_wheel_apps_host.py): a list box, a drop-down list, a multiline
// edit control with both scroll bars, and a pane beside a SCROLLBAR control
// the program moves itself on WM_VSCROLL. The program does nothing with
// WM_MOUSEWHEEL. Ten times a second the title says where everything stands:
// lb=<top row> cb=<selection>,<dropped>,<top row> edit=<first line>,<x> bar=<position>
typedef unsigned U;
typedef struct { U hwnd,message,wp,lp,time; int x,y; } MSG;
typedef struct { U style,proc,extra,classExtra,instance,icon,cursor,brush; const char *menu,*name; } CLASS;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U RegisterClassA(CLASS*);
API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);
API U GetMessageA(MSG*,U,U,U);
API U TranslateMessage(MSG*);
API U DispatchMessageA(MSG*);
API U IsDialogMessageA(U,MSG*);
API U SendMessageA(U,U,U,U);
API U SetWindowTextA(U,const char*);
API U SetForegroundWindow(U);
API U SetFocus(U);
API U SetTimer(U,U,U,U);
API int SetScrollPos(U,int,int,int);
API int GetScrollPos(U,int);
API int SetScrollRange(U,int,int,int,int);
API U GetStockObject(int);
__declspec(dllimport) int __cdecl wsprintfA(char*,const char*,...);
static U list,combo,edit,bar;
static int position;
static char last[100];
static char text[60*210];
static U __stdcall pane(U h,U m,U wp,U lp){return DefWindowProcA(h,m,wp,lp);}
static U __stdcall proc(U h,U m,U wp,U lp)
{
 if(m==0x113){ // WM_TIMER
  char title[100];
  wsprintfA(title,"lb=%d cb=%d,%d,%d edit=%d,%d bar=%d",(int)SendMessageA(list,0x18e,0,0),(int)SendMessageA(combo,0x147,0,0),
            (int)SendMessageA(combo,0x157,0,0),(int)SendMessageA(combo,0x15b,0,0),(int)SendMessageA(edit,0xce,0,0),
            GetScrollPos(edit,0),position);
  int same=1;for(int i=0;i<100;++i){if(title[i]!=last[i])same=0;if(!title[i])break;}
  if(!same){for(int i=0;i<100;++i){last[i]=title[i];if(!title[i])break;}SetWindowTextA(h,title);}
  return 0;
 }
 if(m==0x115&&lp==bar){ // WM_VSCROLL of the control
  const U code=wp&0xffff;
  if(code==0)--position;else if(code==1)++position;else if(code==2)position-=10;else if(code==3)position+=10;
  else if(code==4||code==5)position=(int)(short)(wp>>16);
  if(position<0)position=0;if(position>100)position=100;
  SetScrollPos(bar,2,position,1);
  return 0;
 }
 if(m==2)ExitProcess(0);
 return DefWindowProcA(h,m,wp,lp);
}
void mainCRTStartup(void)
{
 CLASS c={0,(U)proc,0,0,0x400000,0,0,6,0,"WHEEL_CASES"},p={0,(U)pane,0,0,0x400000,0,0,6,0,"WHEEL_PANE"};
 if(!RegisterClassA(&c)||!RegisterClassA(&p))ExitProcess(200);
 const U window=CreateWindowExA(0,c.name,"cases",0x10cf0000,40,40,620,360,0,0,c.instance,0);
 list=CreateWindowExA(0,"LISTBOX","",0x50a10001,10,10,150,150,window,1,c.instance,0);
 combo=CreateWindowExA(0,"COMBOBOX","",0x50210003,170,10,150,200,window,2,c.instance,0);
 edit=CreateWindowExA(0,"EDIT","",0x50b100c4,10,170,310,120,window,3,c.instance,0);
 bar=CreateWindowExA(0,"SCROLLBAR","",0x50000001,340,10,18,280,window,4,c.instance,0);
 CreateWindowExA(0,p.name,"",0x50800000,370,10,200,280,window,5,c.instance,0);
 for(int i=0;i<100;++i){char item[20];wsprintfA(item,"item %03d",i);SendMessageA(list,0x180,0,(U)item);if(i<50)SendMessageA(combo,0x143,0,(U)item);}
 SendMessageA(combo,0x14e,10,0);
 char*at=text;
 for(int i=0;i<60;++i){at+=wsprintfA(at,"line %02d",i);for(int k=0;k<24;++k)at+=wsprintfA(at," word%02d",k);*at++='\r';*at++='\n';}
 *at=0;SetWindowTextA(edit,text);
 SetScrollRange(bar,2,0,100,0);SetScrollPos(bar,2,0,1);
 SetForegroundWindow(window);SetFocus(list);SetTimer(window,1,100,0);
 MSG msg;while(GetMessageA(&msg,0,0,0)){if(IsDialogMessageA(window,&msg))continue;TranslateMessage(&msg);DispatchMessageA(&msg);}
 ExitProcess(0);
}
