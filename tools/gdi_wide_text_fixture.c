// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API void SetLastError(U);
API U CreateCompatibleDC(U);API U DeleteDC(U);API U SelectObject(U,U);API U DeleteObject(U);
API U CreateDIBSection(U,const void*,U,void**,U,U);
API U TextOutA(U,int,int,const char*,int);API U TextOutW(U,int,int,const W*,int);
API U ExtTextOutA(U,int,int,U,const void*,const char*,U,const int*);API U ExtTextOutW(U,int,int,U,const void*,const W*,U,const int*);
API U GetTextExtentPoint32A(U,const char*,int,void*);API U GetTextExtentPoint32W(U,const W*,int,void*);API U GetTextExtentPointW(U,const W*,int,void*);
API U SetTextColor(U,U);API U SetBkColor(U,U);API int SetBkMode(U,int);API U SetTextAlign(U,U);API U MoveToEx(U,int,int,void*);API U GetCurrentPositionEx(U,void*);
static void check(int yes,U code){if(!yes)ExitProcess(code);}
struct Bitmap{U size;int width,height;W planes,depth;U compression,bytes;int xppm,yppm;U colors,important;};
struct Point{int x,y;};struct Rect{int l,t,r,b;};static U reference[128*64];
static void clear(U*p){for(U i=0;i<128*64;++i)p[i]=0;}
static void save(U*p){for(U i=0;i<128*64;++i)reference[i]=p[i];}
static void compare(U*p,U code){U ink=0;for(U i=0;i<128*64;++i){check(p[i]==reference[i],code);ink|=p[i];}check(ink,code);}
void run_gdi_wide_text_test(void){
 U dc=CreateCompatibleDC(0);check(dc,30000);struct Bitmap b={40,128,-64,1,32,0,0,0,0,0,0};U*bits=0;U bitmap=CreateDIBSection(dc,&b,0,(void**)&bits,0,0);check(bitmap&&bits,30001);U old=SelectObject(dc,bitmap);check(old,30002);
 SetTextColor(dc,0xffffff);SetBkColor(dc,0);SetBkMode(dc,1);
 const W unicode[]={'W','i','n',0x2219,0x400,0,'A'};const char bytes[]={'W','i','n',(char)0xb7,0x1f,0,'A'};
 struct Point ansi,wide,wide_old;check(GetTextExtentPoint32A(dc,bytes,7,&ansi)&&GetTextExtentPoint32W(dc,unicode,7,&wide)&&GetTextExtentPointW(dc,unicode,7,&wide_old),30003);check(ansi.x==wide.x&&ansi.y==wide.y&&wide.x==wide_old.x&&wide.y==wide_old.y&&wide.x>0,30004);
 clear(bits);check(TextOutA(dc,5,5,bytes,7),30005);save(bits);clear(bits);check(TextOutW(dc,0x10005,0x10005,unicode,7),30006);compare(bits,30007);
 struct Rect clip={4,4,70,30};int dx[7]={9,10,11,8,7,6,5};clear(bits);check(ExtTextOutA(dc,5,5,4,&clip,bytes,7,dx),30008);save(bits);clear(bits);check(ExtTextOutW(dc,5,5,4,&clip,unicode,7,dx),30009);compare(bits,30010);
 // Original DIBENG measures the rightmost glyph/advance edge. Here the
 // last glyph extends two pixels beyond sum(dx): width58, not56.
 SetTextAlign(dc,1);MoveToEx(dc,5,5,0);
 check(ExtTextOutA(dc,0,0,0,0,bytes,7,dx)&&GetCurrentPositionEx(dc,&ansi)&&ansi.x==63&&ansi.y==5,30018);
 MoveToEx(dc,5,5,0);check(ExtTextOutW(dc,0,0,0,0,unicode,7,dx)&&GetCurrentPositionEx(dc,&wide)&&wide.x==ansi.x&&wide.y==ansi.y,30011);SetTextAlign(dc,0);
 // Exact first RichEdit operation: no text/dx, ETO_OPAQUE rectangle.
 clear(bits);SetBkColor(dc,0xffffff);struct Rect opaque={0,0,128,13};check(ExtTextOutW(dc,0,0,2,&opaque,0,0,0),30012);for(U y=0;y<64;++y)for(U x=0;x<128;++x)check((bits[y*128+x]&0xffffff)==(y<13?0xffffff:0),30013);
 wide.x=wide.y=-1;check(GetTextExtentPoint32W(dc,0,0,&wide)&&!wide.x&&!wide.y,30014);wide.x=wide.y=-1;check(GetTextExtentPointW(dc,0,65536,&wide)&&!wide.x&&!wide.y,30015);
 // Win98's installed raster fonts use byte character codes as glyph indices.
 // The original System/Symbol comparison is recorded in win98-glyph-raster-2026-10-22.json.
 const W glyphs[]={'W','i','n',0xb7,0x1f,0,'A'};
 clear(bits);check(ExtTextOutA(dc,5,5,0,0,bytes,7,0),30016);save(bits);clear(bits);
 check(ExtTextOutW(dc,5,5,16,0,glyphs,7,0),30016);compare(bits,30019);
 check(SelectObject(dc,old)==bitmap&&DeleteObject(bitmap)&&DeleteDC(dc),30017);
}
#ifdef GDI_WIDE_TEXT_STANDALONE
void mainCRTStartup(void){run_gdi_wide_text_test();ExitProcess(0);}
#endif
