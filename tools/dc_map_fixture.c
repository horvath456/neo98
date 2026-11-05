/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent x86 contract for the original GDI coordinate family.
 * No port implementation is linked into this guest. Device-unit expectations
 * identify the native provider's explicit equal-axis 96-DPI profile.
 */
typedef unsigned U;
typedef struct { int x,y; } Point;
typedef struct { int left,top,right,bottom; } Rect;
#define API __declspec(dllimport) __stdcall
API U CreateCompatibleDC(U);
API U DeleteDC(U);
API U GetMapMode(U);
API U SetMapMode(U,U);
API U GetWindowOrgEx(U,Point *);
API U GetViewportOrgEx(U,Point *);
API U GetWindowExtEx(U,Point *);
API U GetViewportExtEx(U,Point *);
API U GetDCOrgEx(U,Point *);
API U SetWindowOrgEx(U,int,int,Point *);
API U SetViewportOrgEx(U,int,int,Point *);
API U OffsetWindowOrgEx(U,int,int,Point *);
API U OffsetViewportOrgEx(U,int,int,Point *);
API U SetWindowExtEx(U,int,int,Point *);
API U SetViewportExtEx(U,int,int,Point *);
API U ScaleWindowExtEx(U,int,int,int,int,Point *);
API U ScaleViewportExtEx(U,int,int,int,int,Point *);
API U LPtoDP(U,Point *,U);
API U DPtoLP(U,Point *,U);
API U SaveDC(U);
API U RestoreDC(U,int);
API U CreateBitmap(U,U,U,U,const void *);
API U GetObjectA(U,U,void *);
API U DeleteObject(U);
API U CreateSolidBrush(U);
API U FillRect(U,const Rect *,U);
API U GetPixel(U,int,int);
API U GetDC(U);
API U ReleaseDC(U,U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U,const char *,const char *,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);
API U DestroyWindow(U);
API U SetWindowPos(U,U,int,int,int,int,U);
API U MapWindowPoints(U,U,Point *,U);
API U GetModuleHandleA(const char *);
API U GetLastError(void);
API void SetLastError(U);
API void ExitProcess(U);
static void check(int okay,U error){if(!okay)ExitProcess(error);}
static int pair(Point p,int x,int y){return p.x==x && p.y==y;}
static U __stdcall procedure(U h,U m,U w,U l){return DefWindowProcA(h,m,w,l);}
void run_dc_map_test(void)
{
    U dc=CreateCompatibleDC(0);Point p={0,0};
    check(dc && GetMapMode(dc)==1,6000);
    check(GetWindowOrgEx(dc,&p) && pair(p,0,0) && GetViewportOrgEx(dc,&p) && pair(p,0,0),6001);
    check(GetWindowExtEx(dc,&p) && pair(p,1,1) && GetViewportExtEx(dc,&p) && pair(p,1,1),6002);
    check(GetDCOrgEx(dc,&p) && pair(p,0,0) && GetDCOrgEx(dc,0),6003);
    check(!SetMapMode(dc,0) && !SetMapMode(dc,9) && GetMapMode(dc)==1,6004);
    check(SetMapMode(dc,0xabcd0008)==1 && GetMapMode(dc)==8,6005);
    check(SetWindowOrgEx(dc,0x12340005,-7,&p) && pair(p,0,0),6006);
    check(SetViewportOrgEx(dc,11,13,&p) && pair(p,0,0),6007);
    check(SetWindowExtEx(dc,7,3,&p) && pair(p,1,1),6008);
    check(SetViewportExtEx(dc,3,-7,&p) && pair(p,1,1),6009);
    Point points[]={{0x12340005,-7},{12,-4},{2,-8}};
    check(LPtoDP(dc,points,3) && pair(points[0],11,13) && pair(points[1],14,6) && pair(points[2],10,15),6010);
    check(DPtoLP(dc,points,2) && pair(points[0],5,-7) && pair(points[1],12,-4),6011);
    check(OffsetWindowOrgEx(dc,2,3,&p) && pair(p,5,-7),6012);
    check(OffsetViewportOrgEx(dc,-1,1,&p) && pair(p,11,13),6013);
    check(ScaleWindowExtEx(dc,2,1,3,1,&p) && pair(p,7,3),6014);
    check(ScaleViewportExtEx(dc,1,3,1,7,&p) && pair(p,3,-7),6015);
    check(GetWindowExtEx(dc,&p) && pair(p,14,9) && GetViewportExtEx(dc,&p) && pair(p,1,-1),6016);
    check(ScaleWindowExtEx(dc,1,0,1,0,&p) && pair(p,14,9),6017);
    check(GetWindowExtEx(dc,&p) && pair(p,32767,32767),6018);
    p=(Point){71,72};check(SetWindowExtEx(dc,0,9,&p) && pair(p,0,0),6019);
    p=(Point){71,72};check(!SetViewportExtEx(dc,1,0,&p) && pair(p,71,72),6020); /* native failed-output policy */
    check(SetWindowOrgEx(dc,32767,-32768,0) && OffsetWindowOrgEx(dc,1,-1,&p) && pair(p,32767,-32768),6021);
    check(GetWindowOrgEx(dc,&p) && pair(p,-32768,32767),6022);
    check(SetMapMode(dc,1)==8 && GetWindowExtEx(dc,&p) && pair(p,1,1),6023);
    static const int units[]={254,2540,100,1000,1440};
    for(U mode=2;mode<=6;++mode)
    {
        check(SetMapMode(dc,mode)!=0 && GetWindowExtEx(dc,&p) && pair(p,units[mode-2],units[mode-2]),6024);
        check(GetViewportExtEx(dc,&p) && pair(p,96,-96),6025);
        check(SetWindowExtEx(dc,2,3,&p) && pair(p,units[mode-2],units[mode-2]),6026);
        check(GetWindowExtEx(dc,&p) && pair(p,units[mode-2],units[mode-2]),6027);
    }
    check(SetMapMode(dc,7)==6 && SetWindowExtEx(dc,100,100,0) && SetViewportExtEx(dc,200,100,0),6028);
    check(GetViewportExtEx(dc,&p) && pair(p,100,100),6029);
    check(SetMapMode(dc,8)==7 && SetWindowOrgEx(dc,5,6,0) && SetViewportOrgEx(dc,7,8,0) &&
          SetWindowExtEx(dc,2,3,0) && SetViewportExtEx(dc,4,9,0),6030);
    check(SaveDC(dc)==1,6031);
    check(SetMapMode(dc,1)==8 && SetWindowOrgEx(dc,0,0,0) && SetViewportOrgEx(dc,0,0,0),6032);
    check(RestoreDC(dc,-1) && GetMapMode(dc)==8 && GetWindowOrgEx(dc,&p) && pair(p,5,6),6033);
    check(GetViewportOrgEx(dc,&p) && pair(p,7,8) && GetWindowExtEx(dc,&p) && pair(p,2,3) &&
          GetViewportExtEx(dc,&p) && pair(p,4,9),6034);
    check(DeleteDC(dc) && !GetMapMode(dc) && !GetWindowOrgEx(dc,&p) && !SetViewportOrgEx(dc,0,0,&p),6035);
    U bitmap=CreateBitmap(8,8,1,1,0),info[24];check(bitmap && GetObjectA(bitmap,24,info)==24,6036);
    check(DeleteObject(bitmap),6037);SetLastError(0x12345678);
    check(!GetObjectA(bitmap,24,info) && GetLastError()==87,6038);
    U fresh=CreateBitmap(8,8,1,1,0);check(fresh && fresh!=bitmap && GetObjectA(fresh,24,info)==24,6039);
    SetLastError(0);check(!GetObjectA(bitmap,0,0) && GetLastError()==87 && DeleteObject(fresh),6040);

    /* A window DC draws on the screen at the window's client origin. Mapping
       APIs exclude the device origin; GetDCOrg adds it. */
    U klass[]={0,(U)procedure,0,0,GetModuleHandleA(0),0,0,0,0,(U)"NEO98DcMapContract"};
    check(RegisterClassA(klass),6041);
    /* Visible: a window DC draws on the screen inside the visible region, which
       is empty for a hidden window. A move keeps the pixels (SetWindowPos
       copies the valid part). */
    U h=CreateWindowExA(0,"NEO98DcMapContract","GDI mapping",0x90cf0000u,40,60,180,120,0,0,klass[4],0);
    check(h,6042);dc=GetDC(h);check(dc,6043);
    Point origin={0,0};MapWindowPoints(h,0,&origin,1);
    check(GetDCOrgEx(dc,&p) && pair(p,origin.x,origin.y),6044);
    check(GetViewportOrgEx(dc,&p) && pair(p,0,0),6045);
    p=(Point){0,0};check(LPtoDP(dc,&p,1) && pair(p,0,0),6046);
    U brush=CreateSolidBrush(0x123456);Rect r={2,3,6,7};
    check(brush && FillRect(dc,&r,brush) && GetPixel(dc,3,4)==0x123456,6047);
    check(SetMapMode(dc,8)==1 && SetWindowExtEx(dc,1,1,0) && SetViewportExtEx(dc,2,3,0),6048);
    r=(Rect){8,9,10,11};check(FillRect(dc,&r,brush),6049);
    check(SetMapMode(dc,1)==8 && GetPixel(dc,16,27)==0x123456 && GetPixel(dc,19,32)==0x123456,6050);
    check(ReleaseDC(h,dc),6051);
    check(SetWindowPos(h,0,90,100,0,0,0x15),6052);
    dc=GetDC(h);origin=(Point){0,0};MapWindowPoints(h,0,&origin,1);
    check(GetDCOrgEx(dc,&p) && pair(p,origin.x,origin.y) && GetViewportOrgEx(dc,&p) && pair(p,0,0),6053);
    check(GetPixel(dc,3,4)==0x123456 && ReleaseDC(h,dc) && DeleteObject(brush) && DestroyWindow(h),6054);
}
