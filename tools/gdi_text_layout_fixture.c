// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateCompatibleDC(U);API U DeleteDC(U);API U SelectObject(U,U);API U DeleteObject(U);
API U CreateDIBSection(U,const void*,U,void**,U,U);API int SaveDC(U);API U RestoreDC(U,int);
API U SetTextAlign(U,U);API U GetTextAlign(U);API int SetTextCharacterExtra(U,int);API int GetTextCharacterExtra(U);
API U SetTextJustification(U,int,int);API U MoveToEx(U,int,int,void*);API U GetCurrentPositionEx(U,void*);
API U GetTextExtentPoint32A(U,const char*,int,void*);API U GetTextMetricsA(U,void*);
API U TextOutA(U,int,int,const char*,int);API U ExtTextOutA(U,int,int,U,const void*,const char*,U,const int*);
API U SetTextColor(U,U);API U SetBkColor(U,U);API int SetBkMode(U,int);
API int SetMapMode(U,int);API U SetWindowExtEx(U,int,int,void*);API U SetViewportExtEx(U,int,int,void*);
API U GetModuleHandleA(const char*);API U GetProcAddress(U,const char*);
static void check(int yes,U code){if(!yes)ExitProcess(code);}
struct Point{int x,y;};struct Size{int x,y;};
struct Bitmap{U size;int width,height;unsigned short planes,depth;U compression,bytes;int xppm,yppm;U colors,important;};
static U reference[128*64];
static void clear(U*p){for(U i=0;i<128*64;++i)p[i]=0;}
static void pixels(U*p,U code){U nonzero=0;for(U i=0;i<128*64;++i){check(p[i]==reference[i],code);nonzero|=p[i];}check(nonzero!=0,code);}
void run_gdi_text_layout_test(void){
 // Force native generation bits beyond the original WORD thunk range.
 for(U i=0;i<260;++i){U h=CreateCompatibleDC(0);check(h&&DeleteDC(h),21900);}
 U dc=CreateCompatibleDC(0);check(dc>65535,21901);
 struct Bitmap info={40,128,-64,1,32,0,0,0,0,0,0};U*bits=0;
 U bitmap=CreateDIBSection(dc,&info,0,(void**)&bits,0,0);check(bitmap&&bits,21902);U old=SelectObject(dc,bitmap);check(old,21903);
 check(GetTextAlign(dc)==0&&GetTextCharacterExtra(dc)==0,21904);
 SetTextColor(dc,0xffffff);SetBkColor(dc,0);SetBkMode(dc,1);
 struct Size size;U metrics[14];check(GetTextMetricsA(dc,metrics),21905);
 check(GetTextExtentPoint32A(dc,"Hi",2,&size),21906);check(size.x>0&&size.y>0,21907);
 clear(bits);check(TextOutA(dc,30,10,"Hi",2),21908);for(U i=0;i<128*64;++i)reference[i]=bits[i];
 clear(bits);check(SetTextAlign(dc,2)==0&&TextOutA(dc,30+size.x,10,"Hi",2),21909);pixels(bits,21910);
 clear(bits);check(SetTextAlign(dc,6)==2&&TextOutA(dc,30+size.x/2,10,"Hi",2),21911);pixels(bits,21912);
 clear(bits);check(SetTextAlign(dc,24)==6&&TextOutA(dc,30,10+(int)metrics[1],"Hi",2),21913);pixels(bits,21914);
 clear(bits);check(SetTextAlign(dc,8)==24&&TextOutA(dc,30,10+(int)metrics[0],"Hi",2),21915);pixels(bits,21916);
 struct Point cp;check(MoveToEx(dc,30,10,0),21917);check(SetTextAlign(dc,1)==8,21918);
 clear(bits);check(TextOutA(dc,500,500,"Hi",2),21919);pixels(bits,21920);check(GetCurrentPositionEx(dc,&cp)&&cp.x==30+size.x&&cp.y==10,21921);
 check(MoveToEx(dc,30+size.x,10,0)&&SetTextAlign(dc,3)==1,21922);clear(bits);check(TextOutA(dc,0,0,"Hi",2),21923);pixels(bits,21924);check(GetCurrentPositionEx(dc,&cp)&&cp.x==30&&cp.y==10,21925);
 check(MoveToEx(dc,30+size.x/2,10,0)&&SetTextAlign(dc,7)==3,21926);clear(bits);check(TextOutA(dc,0,0,"Hi",2),21927);pixels(bits,21928);check(GetCurrentPositionEx(dc,&cp)&&cp.x==30+size.x/2,21929);
 check(SetTextCharacterExtra(dc,-3)==0&&GetTextCharacterExtra(dc)==3,21930);check(SetTextCharacterExtra(dc,0x10002)==3&&GetTextCharacterExtra(dc)==2,21931);
 check(SetTextJustification(dc,7,3),21932);int saved=SaveDC(dc);check(saved>0,21933);
 check(SetTextAlign(dc,0)==7&&SetTextCharacterExtra(dc,0)==2&&SetTextJustification(dc,0,0),21934);check(MoveToEx(dc,1,2,0),21935);
 check(RestoreDC(dc,saved)&&GetTextAlign(dc)==7&&GetTextCharacterExtra(dc)==2&&GetCurrentPositionEx(dc,&cp)&&cp.x==30+size.x/2&&cp.y==10,21936);
 // The restored spacing is consumed by the real DIBENG measurement/drawing path.
 struct Size justified,plain;check(GetTextExtentPoint32A(dc,"A A A A",7,&justified),21937);
 SetTextCharacterExtra(dc,0);SetTextJustification(dc,0,0);check(GetTextExtentPoint32A(dc,"A A A A",7,&plain)&&justified.x==plain.x+21,21938);
 check(SetTextAlign(dc,0)==7,21939);clear(bits);check(SetTextJustification(dc,7,3)&&TextOutA(dc,5,5,"A A A A",7),21940);U ink=0;for(U i=0;i<128*64;++i)ink|=bits[i];check(ink,21941);
 check(!SetTextJustification(dc,1,0),21942);check(SetTextJustification(dc,0,0),21943);
 // Original signed WORD arguments and logical/device spacing conversion.
 check(SetMapMode(dc,8)&&SetWindowExtEx(dc,2,1,0)&&SetViewportExtEx(dc,4,1,0),21944);
 check(SetTextCharacterExtra(dc,3)==0&&GetTextCharacterExtra(dc)==3,21945);check(SetTextCharacterExtra(dc,-2)==3&&GetTextCharacterExtra(dc)==2,21946);SetMapMode(dc,1);SetTextCharacterExtra(dc,0);
 typedef U(__stdcall*SetAlign)(U,U);SetAlign ordinal=(SetAlign)GetProcAddress(GetModuleHandleA("GDI32.DLL"),(const char*)410);check(ordinal&&ordinal(dc,0x10018)==0&&GetTextAlign(dc)==24,21947);
 // Explicit dx supplies the width used by right-alignment and CP updates.
 int dx[2]={9,11};SetTextAlign(dc,1);MoveToEx(dc,10,10,0);clear(bits);check(ExtTextOutA(dc,0,0,0,0,"Hi",2,dx)&&GetCurrentPositionEx(dc,&cp)&&cp.x==30,21948);
 check(SelectObject(dc,old)==bitmap&&DeleteObject(bitmap)&&DeleteDC(dc),21949);
}
#ifdef GDI_TEXT_LAYOUT_STANDALONE
void mainCRTStartup(void){run_gdi_text_layout_test();ExitProcess(0);}
#endif
