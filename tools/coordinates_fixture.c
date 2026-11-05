// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;typedef struct {int x,y;}P;typedef struct {int l,t,r,b;}R;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetModuleHandleA(const char*);API U GetProcAddress(U,const char*);API U RegisterClassA(U*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);API U DestroyWindow(U);API U DefWindowProcA(U,U,U,U);API U ClientToScreen(U,P*);API U ScreenToClient(U,P*);API U MapWindowPoints(U,U,P*,U);API U GetClientRect(U,R*);API U GetWindowRect(U,R*);API U GetDesktopWindow(void);API U FindWindowA(const char*,const char*);API U MoveWindow(U,int,int,int,int,U);API U ShowWindow(U,U);API U IsIconic(U);API U GetLastError(void);API void SetLastError(U);
void*memset(void*p,int v,U n){char*q=p;while(n--)*q++=v;return p;}
static void ck(U yes,U code){if(!yes)ExitProcess(code);}static U __stdcall proc(U h,U m,U w,U l){return DefWindowProcA(h,m,w,l);}static int rect(R r,int l,int t,int x,int y){return r.l==l&&r.t==t&&r.r==x&&r.b==y;}
// Win98 gives a 32-bit program the 16-bit USER handle of the window. The port
// keeps that width and tags the upper six bits with a generation, so the slot the
// runner reused 260 times comes back under a handle of a later generation.
void mainCRTStartup(void){U instance=GetModuleHandleA(0),wc[10]={0,(U)proc,0,0,instance,0,0,0,0,(U)"CoordinateProbe"};ck(RegisterClassA(wc),19100);U h=CreateWindowExA(0,"CoordinateProbe","",0x80000000,-30,-20,100,80,0,0,instance,0);ck(h&&h<=65535&&(h>>10)>1,19101);P p={0x7fffffff,(int)0x80000000};
 ck(ClientToScreen(h,&p)==(U)-19&&(U)p.x==0x7fffffe1&&(U)p.y==0x7fffffec,19102);ck(ScreenToClient(h,&p)==(U)-19&&(U)p.x==0x7fffffff&&(U)p.y==0x80000000,19103);R r;
 ck(GetClientRect(h,&r)==81&&rect(r,0,0,100,80),19104);ck(GetWindowRect(h,&r)==1&&rect(r,-30,-20,70,60),19105);
 U child=CreateWindowExA(0,"CoordinateProbe","",0x40000000,7,11,20,30,h,41,instance,0);ck(child,19106);p.x=p.y=0;ck(ClientToScreen(child,&p)==(U)-9&&p.x==-23&&p.y==-9,19107);ck(ScreenToClient(child,&p)==(U)-9&&!p.x&&!p.y,19108);
 P pts[3]={{0x12348001,0x12345678},{0x7fffffff,(int)0x80000000},{-1,-2}};ck(MapWindowPoints(child,h,pts,3)==0x000b0007,19109);ck(pts[0].x==-32760&&pts[0].y==22147&&pts[1].x==6&&pts[1].y==11&&pts[2].x==6&&pts[2].y==9,19110);
 p.x=0x12345678;p.y=0x12348001;ck(MapWindowPoints(h,h,&p,1)==0&&p.x==22136&&p.y==-32767,19111);p.x=123;p.y=456;ck(MapWindowPoints(child,h,&p,0)==0x000b0007&&p.x==123&&p.y==456&&MapWindowPoints(child,h,0,0)==0x000b0007,19112);
 U desktop=GetDesktopWindow();ck(desktop,19113);p.x=0x12345678;p.y=(int)0x80000000;ck(ClientToScreen(desktop,&p)==desktop&&ScreenToClient(desktop,&p)==desktop&&p.x==0x12345678&&(U)p.y==0x80000000,19114);ck(ClientToScreen(desktop,0)==desktop&&ScreenToClient(desktop,(P*)1)==desktop,19115);
 ck(GetClientRect(desktop,&r)==701&&rect(r,0,0,901,700)&&GetWindowRect(desktop,&r)==701&&rect(r,0,0,901,700),19116);
 U legacy=FindWindowA("LegacyCoordinate",0);ck(legacy&&GetClientRect(legacy,&r)==25&&rect(r,0,0,160,24),19117);
 SetLastError(789);p.x=81;p.y=82;ck(!ClientToScreen(0,&p)&&!ScreenToClient(0,&p)&&p.x==81&&p.y==82&&GetLastError()==789,19118);r.l=81;r.t=82;r.r=83;r.b=84;ck(!GetClientRect(0,&r)&&!GetWindowRect(0,&r)&&rect(r,81,82,83,84)&&GetLastError()==789,19119);ck(!GetClientRect(h,0)&&!GetWindowRect(h,0)&&!ClientToScreen(h,0)&&!ScreenToClient(h,0),19120);
 ck(!ClientToScreen(h,(P*)1)&&GetLastError()==87,19121);ck(!MapWindowPoints(h,0,&p,4097)&&GetLastError()==8,19122);
 ck(MoveWindow(h,32760,32760,100,80,0)&&GetWindowRect(h,&r)==1&&rect(r,32760,32760,-32676,-32696),19123);ck(GetClientRect(h,&r)==81&&rect(r,0,0,100,80),19124);
 ShowWindow(h,6);ck(IsIconic(h)&&GetClientRect(h,&r)&&(r.r!=160||r.b!=24),19125);
 typedef U(__stdcall*F)(U,P*);F ord=(F)GetProcAddress(GetModuleHandleA("USER32.DLL"),(const char*)61);ck(ord&&ord(desktop,0)==desktop,19126);
 ck(DestroyWindow(child)&&DestroyWindow(h),19127);ExitProcess(0);
}
