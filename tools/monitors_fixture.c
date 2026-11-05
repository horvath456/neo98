// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;typedef struct {int l,t,r,b;}R;
typedef struct {U size;R monitor,work;U flags;char device[32];}MI;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetModuleHandleA(const char*);API U GetProcAddress(U,const char*);API U RegisterClassA(U*);
API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);API U DestroyWindow(U);API U DefWindowProcA(U,U,U,U);
API U MoveWindow(U,int,int,int,int,U);API U GetLastError(void);API void SetLastError(U);API int GetSystemMetrics(int);
API U MonitorFromPoint(int,int,U);API U MonitorFromRect(const R*,U);API U MonitorFromWindow(U,U);
API U GetMonitorInfoA(U,MI*);API U GetMonitorInfoW(U,MI*);API U EnumDisplayMonitors(U,const R*,U,U);
typedef struct {U cb;char name[32],string[128];U flags;char id[128],key[128];}DD;
API U EnumDisplayDevicesA(const char*,U,DD*,U);API U CreateDCA(const char*,const char*,const char*,const void*);API U DeleteDC(U);
API U GetDC(U);API U ReleaseDC(U,U);API U CreateCompatibleDC(U);
API U GetClipBox(U,R*);API U IntersectClipRect(U,int,int,int,int);
API U GetTextColor(U);API U SetTextColor(U,U);API U GetPixel(U,int,int);API U SetPixel(U,int,int,U);
typedef struct{int x,y;}P;
API U GetDCOrgEx(U,P*);API U GetViewportOrgEx(U,P*);API U SetViewportOrgEx(U,int,int,P*);
void*memset(void*p,int v,U n){char*q=p;while(n--)*q++=v;return p;}
static void ck(U yes,U code){if(!yes)ExitProcess(code);}
static U __stdcall proc(U h,U m,U w,U l){return DefWindowProcA(h,m,w,l);}
static int rect(R r,int l,int t,int x,int y){return r.l==l&&r.t==t&&r.r==x&&r.b==y;}
static U seen,stop,got[4],data_seen;static R areas[4];
static U __stdcall enumerate(U monitor,U dc,R*area,U data){
 if(seen<4){got[seen]=monitor;areas[seen]=*area;}
 data_seen=data;++seen;ck(!dc,19560);return stop?0:1;
}
static U wanted_dc;static R wanted_area;
static U __stdcall enumerate_dc(U monitor,U dc,R*area,U data){
 R clip;P origin;
 ck(monitor==got[0]&&dc==wanted_dc&&data==0x5911,19561);
 ck(rect(*area,wanted_area.l,wanted_area.t,wanted_area.r,wanted_area.b),19562);
 ck(GetClipBox(dc,&clip)>1&&rect(clip,area->l,area->t,area->r,area->b),19563);
 ck(GetDCOrgEx(dc,&origin)&&origin.x==30&&origin.y==40,19564);
 ck(SetPixel(dc,20,30,0x2468ac)==0x2468ac,19565);
 SetTextColor(dc,0x13579b);IntersectClipRect(dc,20,30,21,31);SetViewportOrgEx(dc,7,8,0);
 ++seen;return stop?0:1;
}
void mainCRTStartup(void){
 U instance=GetModuleHandleA(0),wc[10]={0,(U)proc,0,0,instance,0,0,0,0,(U)"MonitorProbe"};
 ck(RegisterClassA(wc),19500);
 // The host configured a 1024x768 display whose work area is still 800x600.
 ck(GetSystemMetrics(80)==1&&GetSystemMetrics(81)==1,19501);
 ck(!GetSystemMetrics(76)&&!GetSystemMetrics(77)&&GetSystemMetrics(78)==1024&&GetSystemMetrics(79)==768,19502);
 const U primary=MonitorFromPoint(100,100,0);ck(primary,19503);
 ck(MonitorFromPoint(0,0,0)==primary&&MonitorFromPoint(1023,767,0)==primary,19504);
 ck(!MonitorFromPoint(1024,768,0)&&!MonitorFromPoint(-1,-1,0)&&!MonitorFromPoint(2000,2000,0),19505);
 ck(MonitorFromPoint(2000,2000,1)==primary&&MonitorFromPoint(2000,2000,2)==primary,19506);
 ck(MonitorFromPoint(-5000,-5000,2)==primary&&MonitorFromPoint(-5000,-5000,3)==primary,19507);
 // A point above 32767 wraps into the WORD the original thunk pushes.
 ck(MonitorFromPoint(0x10064,0x10064,0)==primary,19508);
 R full={0,0,1024,768},inside={10,20,110,120},away={5000,5000,6000,6000},empty={10,10,10,10};
 ck(MonitorFromRect(&full,0)==primary&&MonitorFromRect(&inside,0)==primary,19509);
 ck(!MonitorFromRect(&away,0)&&MonitorFromRect(&away,1)==primary&&MonitorFromRect(&away,2)==primary,19510);
 ck(MonitorFromRect(&empty,0)==primary,19511);
 R reversed={200,200,100,100};ck(MonitorFromRect(&reversed,0)==primary,19512);
 R wrapped={0x10000,0x10000,0x10064,0x10064};ck(MonitorFromRect(&wrapped,0)==primary,19513);
 //  narrows to -1, so the rectangle empties and degrades to its own corner.
 R huge={0,0,0x7fffffff,0x7fffffff};ck(MonitorFromRect(&huge,0)==primary,19514);
 SetLastError(555);ck(MonitorFromRect(0,1)==primary&&GetLastError()==555,19515);
 ck(!MonitorFromRect(0,0)&&GetLastError()==87,19516);
// Win98 gives a 32-bit program the 16-bit USER handle of the window. The port
// keeps that width and tags the upper six bits with a generation, so the slot the
// runner reused 260 times comes back under a handle of a later generation.
 U h=CreateWindowExA(0,"MonitorProbe","",0x90000000,30,40,100,80,0,0,instance,0);ck(h&&h<=65535&&(h>>10)>1,19517);
 ck(MonitorFromWindow(h,0)==primary,19518);
 SetLastError(556);ck(!MonitorFromWindow(0,0)&&GetLastError()==556,19519);
 ck(MonitorFromWindow(0,1)==primary&&MonitorFromWindow(0,2)==primary,19520);
 ck(MoveWindow(h,5000,5000,100,80,0),19521);
 ck(!MonitorFromWindow(h,0)&&MonitorFromWindow(h,1)==primary&&MonitorFromWindow(h,2)==primary,19522);
 ck(MoveWindow(h,30,40,100,80,0)&&MonitorFromWindow(h,0)==primary,19523);
 MI info;memset(&info,0x5a,sizeof info);info.size=40;
 ck(GetMonitorInfoA(primary,&info)==1,19524);
 ck(info.size==40&&rect(info.monitor,0,0,1024,768)&&rect(info.work,0,0,800,600)&&info.flags==1,19525);
 ck(info.device[0]==0x5a&&info.device[31]==0x5a,19526);// MONITORINFO leaves the name alone
 memset(&info,0x5a,sizeof info);info.size=72;
 ck(GetMonitorInfoA(primary,&info)==1&&rect(info.monitor,0,0,1024,768)&&info.flags==1,19527);
 const char*name="\\\\.\\DISPLAY1";for(U i=0;i<13;++i)ck(info.device[i]==name[i],19528);
 memset(&info,0,sizeof info);info.size=41;SetLastError(557);
 ck(!GetMonitorInfoA(primary,&info)&&GetLastError()==557,19529);
 info.size=40;ck(!GetMonitorInfoA(primary+7,&info)&&GetLastError()==87,19530);
 ck(!GetMonitorInfoA(primary,0)&&GetLastError()==87,19531);
 info.size=40;ck(!GetMonitorInfoW(primary,&info)&&GetLastError()==120,19532);
 seen=0;stop=0;ck(EnumDisplayMonitors(0,0,(U)enumerate,0x1234)&&seen==1,19533);
 ck(got[0]==primary&&rect(areas[0],0,0,1024,768)&&data_seen==0x1234,19534);
 R clip={10,20,110,220};seen=0;ck(EnumDisplayMonitors(0,&clip,(U)enumerate,0)==1&&seen==1&&rect(areas[0],10,20,110,220),19535);
 R outside={2000,2000,2100,2100};seen=0;ck(EnumDisplayMonitors(0,&outside,(U)enumerate,0)==1&&!seen,19536);
 seen=0;stop=1;ck(!EnumDisplayMonitors(0,0,(U)enumerate,0)&&seen==1,19537);
 stop=0;SetLastError(558);ck(!EnumDisplayMonitors(0,0,0,0)&&GetLastError()==87,19538);
 ck(!EnumDisplayMonitors(0x71000001,0,(U)enumerate,0)&&GetLastError()==87,19539);
 // Deluxe CD Player draws its background inside this callback.
 // A nonzero window origin must not leak into the callback's rectangle;
 // the clip is restored on early stop, while other DC attributes survive.
 wanted_dc=GetDC(h);ck(wanted_dc,19566);
 ck(IntersectClipRect(wanted_dc,5,6,95,76)>1,19567);
 SetTextColor(wanted_dc,0xabcdef);wanted_area=(R){10,20,95,76};
 for(stop=0;stop<2;++stop){
  seen=0;ck(EnumDisplayMonitors(wanted_dc,&clip,(U)enumerate_dc,0x5911)==!stop&&seen==1,19568);
  R restored;P origin;
  ck(GetTextColor(wanted_dc)==0x13579b&&GetViewportOrgEx(wanted_dc,&origin)&&origin.x==7&&origin.y==8,19570);
  SetViewportOrgEx(wanted_dc,0,0,0);
  ck(GetClipBox(wanted_dc,&restored)>1&&rect(restored,5,6,95,76),19569);
  ck(GetPixel(wanted_dc,20,30)==0x2468ac,19571);
 }
 stop=0;seen=0;ck(EnumDisplayMonitors(wanted_dc,&outside,(U)enumerate_dc,0x5911)&&!seen,19572);
 ck(IntersectClipRect(wanted_dc,0,0,0,0)==1,19573);
 ck(EnumDisplayMonitors(wanted_dc,0,(U)enumerate_dc,0x5911)&&!seen,19574);
 ck(ReleaseDC(h,wanted_dc),19575);
 ck(!EnumDisplayMonitors(wanted_dc,0,(U)enumerate,0)&&GetLastError()==87,19576);
 U memory=CreateCompatibleDC(0);ck(memory,19577);
 ck(!EnumDisplayMonitors(memory,0,(U)enumerate,0)&&GetLastError()==87&&DeleteDC(memory),19578);
 typedef U(__stdcall*F)(int,int,U);
 F ordinal=(F)GetProcAddress(GetModuleHandleA("USER32.DLL"),(const char*)438);
 ck(ordinal&&ordinal(100,100,0)==primary,19540);
 // EnumDisplayDevices: one adapter, attached to the desktop and primary, under
 // the first standard device name. A DC over that name has to work, because
 // that is what the original DDRAW does with it in Windows 98.
 DD device;memset(&device,0x5a,sizeof device);device.cb=sizeof device;
 ck(EnumDisplayDevicesA(0,0,&device,0)==1,19542);
 ck(device.cb==sizeof device,19543);
 const char*first="\\\\.\\DISPLAY1";for(U i=0;i<13;++i)ck(device.name[i]==first[i],19544);
 ck(device.flags==5,19545);
 const char*description="NEO98 Display Adapter";for(U i=0;i<22;++i)ck(device.string[i]==description[i],19546);
 memset(&device,0x5a,sizeof device);device.cb=sizeof device;
 ck(!EnumDisplayDevicesA(0,1,&device,0),19547);
 memset(&device,0x5a,sizeof device);device.cb=sizeof device;
 ck(EnumDisplayDevicesA("\\\\.\\DISPLAY1",0,&device,0)&&device.flags==1,19548);
 SetLastError(559);device.cb=167;
 ck(!EnumDisplayDevicesA(0,0,&device,0)&&GetLastError()==87,19549);
 ck(!EnumDisplayDevicesA(0,0,0,0)&&GetLastError()==87,19550);
 // A short record stops after the state flags and leaves the rest alone.
 memset(&device,0x5a,sizeof device);device.cb=168;
 ck(EnumDisplayDevicesA(0,0,&device,0)==1&&device.flags==5&&device.id[0]==0x5a,19551);
 // Both slots accept the native display name; this fixture's
 // older expectation that the driver slot fails predates that change.
 U named=CreateDCA("\\\\.\\DISPLAY1",0,0,0);ck(named&&DeleteDC(named),19552);
 U dc=CreateDCA(0,"\\\\.\\DISPLAY1",0,0);ck(dc,19554);ck(DeleteDC(dc),19555);
 dc=CreateDCA("DISPLAY",0,0,0);ck(dc,19553);
 ck(!EnumDisplayMonitors(dc,0,(U)enumerate,0)&&GetLastError()==87,19579);ck(DeleteDC(dc),19558);
 // A device that is no \\.\DISPLAYn and has no [devices] line reaches GDI
 // without a driver name; Windows 98 ends there as well, again without
 // SetLastError.
 SetLastError(561);ck(!CreateDCA(0,"\\\\.\\NOSUCH",0,0)&&GetLastError()==561,19556);
 SetLastError(562);ck(!CreateDCA(0,"\\\\.\\DISPLAY",0,0)&&GetLastError()==562,19557);
 ck(DestroyWindow(h),19541);ExitProcess(0);
}
