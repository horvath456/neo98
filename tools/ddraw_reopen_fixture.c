/* SPDX-License-Identifier: GPL-2.0-or-later */
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {return DefWindowProcA(h,m,w,l);}
static void require(int okay, UINT code) {if(!okay)ExitProcess(code);}
void mainCRTStartup(void) {
 WNDCLASSA c={0}; c.lpfnWndProc=proc;c.lpszClassName="DDReopen";c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassA(&c);
 HWND h=CreateWindowA(c.lpszClassName,"DDReopen",WS_OVERLAPPEDWINDOW,20,20,400,260,0,0,0,0);require(h!=0,1);
 ShowWindow(h,SW_SHOW);UpdateWindow(h);
 LPDIRECTDRAW dd=0;LPDIRECTDRAWSURFACE primary=0,back=0;LPDIRECTDRAWCLIPPER clip=0;
 require(DirectDrawCreate(0,&dd,0)==0,2);require(IDirectDraw_SetCooperativeLevel(dd,h,DDSCL_NORMAL)==0,3);
 DDSURFACEDESC d={0};d.dwSize=sizeof d;d.dwFlags=DDSD_CAPS;d.ddsCaps.dwCaps=DDSCAPS_PRIMARYSURFACE;
 require(IDirectDraw_CreateSurface(dd,&d,&primary,0)==0,4);
 char *line=GetCommandLineA();int child=0;while(*line)if(*line++=='!')child=1;
 if(child){
  SetForegroundWindow(h);
  require(IDirectDraw_CreateClipper(dd,0,&clip,0)==0,5);require(IDirectDrawClipper_SetHWnd(clip,0,h)==0,6);require(IDirectDrawSurface_SetClipper(primary,clip)==0,7);
  d.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT;d.dwWidth=64;d.dwHeight=64;d.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY;
  require(IDirectDraw_CreateSurface(dd,&d,&back,0)==0,8);
  DDBLTFX fx={0};fx.dwSize=sizeof fx;fx.dwFillColor=0x00ff00;
  require(IDirectDrawSurface_Blt(back,0,0,0,DDBLT_COLORFILL|DDBLT_WAIT,&fx)==0,9);
  POINT p={0,0};ClientToScreen(h,&p);RECT r={p.x,p.y,p.x+64,p.y+64};
  require(IDirectDrawSurface_Blt(primary,&r,back,0,DDBLT_WAIT,0)==0,10);
  HDC dc=GetDC(h);COLORREF colour=GetPixel(dc,20,20);ReleaseDC(h,dc);
  require(colour==RGB(0,255,0),11);
  OutputDebugStringA("DDREOPEN child pixels PASS");
  IDirectDrawSurface_Release(back);IDirectDrawSurface_Release(primary);IDirectDrawClipper_Release(clip);IDirectDraw_Release(dd);DestroyWindow(h);ExitProcess(0);
 }
 char name[MAX_PATH], command[MAX_PATH+8];GetModuleFileNameA(0,name,sizeof name);
 for(int i=0;i<3;i++){
  char *out=command;*out++='"';char *in=name;while(*in)*out++=*in++;*out++='"';*out++=' ';*out++='!';*out=0;
  STARTUPINFOA si={0};si.cb=sizeof si;PROCESS_INFORMATION pi={0};require(CreateProcessA(0,command,0,0,0,0,0,0,&si,&pi),20+i);
  require(WaitForSingleObject(pi.hProcess,15000)==WAIT_OBJECT_0,30+i);DWORD code=99;GetExitCodeProcess(pi.hProcess,&code);
  require(code==0,40+i*20+code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
 }
 IDirectDrawSurface_Release(primary);IDirectDraw_Release(dd);DestroyWindow(h);ExitProcess(0);
}
