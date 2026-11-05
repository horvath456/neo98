/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;typedef struct{int x,y;}Point;
typedef struct{U size;int width,height;unsigned short planes,depth;U compression,bytes;int xppm,yppm;U colors,important;}Header;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API void SetLastError(U);
API U CreatePen(U,int,U);API U CreatePenIndirect(const void*);API U ExtCreatePen(U,U,const void*,U,const U*);API U GetObjectA(U,int,void*);API U GetStockObject(U);
API U CreateCompatibleDC(U);API U CreateDIBSection(U,const void*,U,void**,U,U);API U SelectObject(U,U);API U DeleteObject(U);API U DeleteDC(U);
API U SaveDC(U);API U RestoreDC(U,int);API U MoveToEx(U,int,int,Point*);API U GetCurrentPositionEx(U,Point*);API U LineTo(U,int,int);API U Polyline(U,const Point*,U);
API U SetROP2(U,U);API U GetROP2(U);API U GetPixel(U,int,int);API U SetBkMode(U,U);API U SetBkColor(U,U);API U IntersectClipRect(U,int,int,int,int);API U SetViewportOrgEx(U,int,int,Point*);
static void check(int value,U code){if(!value)ExitProcess(code);}
void run_pen_line_test(void){
 Header h={40,32,-32,1,32,0,0,0,0,0,0};U *bits=0;U bitmap=CreateDIBSection(0,&h,0,(void**)&bits,0,0),dc=CreateCompatibleDC(0),oldbitmap=SelectObject(dc,bitmap);check(bitmap&&bits&&dc&&oldbitmap,9200);
 U pen=CreatePen(0,1,0xb74a12),oldpen=SelectObject(dc,pen);check(pen&&oldpen,9201);U info[4]={0};check(GetObjectA(pen,16,info)==16&&info[0]==0&&info[1]==1&&!info[2]&&info[3]==0xb74a12,9202);check(GetObjectA(pen,0,0)==16,9203);SetLastError(0);check(!GetObjectA(pen,0,info)&&GetLastError()==87,9204);
 Point point;check(SaveDC(dc)==1&&DeleteObject(pen)&&GetObjectA(pen,16,info)==16,9205);check(MoveToEx(dc,1,1,&point)&&point.x==0&&point.y==0&&LineTo(dc,7,4),9206);check(bits[33]==0x124ab7&&bits[34]==0x124ab7&&!bits[4*32+7],9207);check(GetPixel(dc,1,1)==0xb74a12&&GetCurrentPositionEx(dc,&point)&&point.x==7&&point.y==4,9208);
 check(SetROP2(dc,7)==13&&GetROP2(dc)==7&&!SetROP2(dc,17)&&GetROP2(dc)==7,9209);check(RestoreDC(dc,-1)&&GetROP2(dc)==13&&GetCurrentPositionEx(dc,&point)&&!point.x&&!point.y,9210);check(SelectObject(dc,oldpen)==pen&&DeleteObject(pen),9211);
 U logical[]={0x10003,0xfffffff9,0x18001,0x123456};pen=CreatePenIndirect(logical);check(pen&&GetObjectA(pen,16,info)==16&&info[0]==3&&info[1]==0xfffffff9&&info[2]==0xffff8001&&info[3]==0x123456,9212);check(DeleteObject(pen)&&!CreatePen(9,1,0),9213);
 // Win32 accepts PS_USERSTYLE/PS_ALTERNATE as solid cosmetic pens.
 for(U style=7;style<=8;++style){pen=CreatePen(style,1,0);check(pen&&GetObjectA(pen,16,info)==16&&info[0]==0&&DeleteObject(pen),9250+style);}
 U brush[]={0,0xb74a12,0};check(!ExtCreatePen(0x10000,0x4001,brush,0,0),9214);pen=ExtCreatePen(0x12203,1,brush,7,(const U*)1);check(pen&&GetObjectA(pen,16,info)==16&&info[0]==3&&info[1]==1&&info[2]==1&&DeleteObject(pen),9215);
 // Original directional dash phase includes 16 on pixels before this first gap.
 for(U i=0;i<1024;++i)bits[i]=0;pen=CreatePen(1,1,0xb74a12);check(pen&&SelectObject(dc,pen)==oldpen&&SetBkMode(dc,1),9216);check(MoveToEx(dc,0,0,0)&&LineTo(dc,31,0),9217);for(U x=0;x<31;++x)check(bits[x]==((x<16||x>=24)?0x124ab7:0),9218);check(!bits[31],9219);
 // The original clipped dash path restarts its rotated phase. Polyline keeps the current point.
 check(SaveDC(dc)==1&&IntersectClipRect(dc,8,4,25,9)!=0,9220);Point line[]={{0,5},{31,5}};check(Polyline(dc,line,2)&&GetCurrentPositionEx(dc,&point)&&point.x==31&&!point.y,9221);for(U x=0;x<32;++x)check(bits[5*32+x]==((x>=8&&x<25)?0x124ab7:0),9222);check(RestoreDC(dc,-1),9223);
 check(MoveToEx(dc,0x18001,0x10002,&point)&&point.x==31&&!point.y&&GetCurrentPositionEx(dc,&point)&&point.x==-32767&&point.y==2,9224);
 check(SaveDC(dc)==1&&SelectObject(dc,oldpen)==pen&&DeleteObject(pen)&&GetObjectA(pen,16,info)==16&&RestoreDC(dc,-1)&&SelectObject(dc,oldpen)==pen&&DeleteObject(pen),9225);
 // Selected stock pens are available through the same drawing provider.
 U nullpen=GetStockObject(8),black=GetStockObject(7),white=GetStockObject(6);check(nullpen&&black&&white&&DeleteObject(black)&&GetObjectA(black,0,0)==16&&SelectObject(dc,nullpen)==oldpen,9226);check(MoveToEx(dc,0,12,0)&&LineTo(dc,20,12)&&!bits[12*32],9227);check(SelectObject(dc,white)==nullpen&&MoveToEx(dc,1,12,0)&&LineTo(dc,3,12)&&bits[12*32+1]==0xffffff&&!bits[12*32+3],9228);check(SelectObject(dc,oldpen)==white,9229);
 // Wide strokes: a wide pen strokes with round caps and joins instead of failing.
 // Width 4 at x=10 covers 8..11 (top-left rule), width 3 at y=15 covers 14..16.
 for(U i=0;i<1024;++i)bits[i]=0;pen=CreatePen(0,4,0x00ff00);check(pen&&SelectObject(dc,pen)==oldpen,9232);
 check(MoveToEx(dc,10,20,0)&&LineTo(dc,10,28),9233);for(U x=6;x<14;++x)check(bits[24*32+x]==((x>=8&&x<12)?0x00ff00:0),9234);
 check(bits[29*32+10]==0x00ff00&&!bits[31*32+10],9235);
 check(SelectObject(dc,oldpen)==pen&&DeleteObject(pen),9236);pen=CreatePen(0,3,0x0000ff);check(pen&&SelectObject(dc,pen)==oldpen,9237);
 check(MoveToEx(dc,20,15,0)&&LineTo(dc,28,15),9238);for(U y=12;y<19;++y)check(bits[y*32+24]==((y>=14&&y<=16)?0xff0000:0),9239);
 // With R2_XORPEN every pixel of the stroke is written once: the joint stays set.
 check(SetROP2(dc,7)==13,9240);Point corner[]={{3,3},{3,12},{12,12}};for(U i=0;i<1024;++i)bits[i]=0;
 check(Polyline(dc,corner,3)&&bits[12*32+3]==0xff0000&&bits[7*32+3]==0xff0000&&bits[12*32+8]==0xff0000,9241);
 check(SetROP2(dc,13)==7&&SelectObject(dc,oldpen)==pen&&DeleteObject(pen),9242);
 check(SelectObject(dc,oldbitmap)==bitmap&&DeleteObject(bitmap)&&DeleteDC(dc),9230);
}
#ifdef PEN_LINE_STANDALONE
void mainCRTStartup(void){run_pen_line_test();U dc=CreateCompatibleDC(0),pen=CreatePen(0,1,0x123456);check(dc&&pen&&SelectObject(dc,pen)&&SaveDC(dc)==1&&DeleteObject(pen),9231);ExitProcess(0);}
#endif
