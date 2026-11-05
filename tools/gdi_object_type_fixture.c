// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API void SetLastError(U);API U GetModuleHandleA(const char*);API U GetProcAddress(U,const char*);
API U GetObjectType(U);API U GetCurrentObject(U,U);API U CreateCompatibleDC(U);API U DeleteDC(U);API U DeleteObject(U);API U SelectObject(U,U);API U SelectPalette(U,U,U);API U GetStockObject(U);
API U CreatePen(U,int,U);API U CreateSolidBrush(U);API U CreateRectRgn(int,int,int,int);API U CreatePalette(const void*);API U CreateDIBSection(U,const void*,U,void**,U,U);
API U CreateDCA(const char*,const char*,const char*,const void*);API U CreateICA(const char*,const char*,const char*,const void*);API U GetDC(U);API U ReleaseDC(U,U);API int SaveDC(U);API U RestoreDC(U,int);
static void check(U yes,U error){if(!yes)ExitProcess(error);}
void run_gdi_object_type_test(void){
 U stale=0;for(U i=0;i<260;++i){stale=CreatePen(0,1,0);check(stale&&DeleteObject(stale),23900);}
 U pen=CreatePen(0,1,0);check(pen>65535&&GetObjectType(pen)==1,23901);
 SetLastError(0x777);check(!GetObjectType(stale)&&GetLastError()==6,23902);check(!GetObjectType(0)&&GetLastError()==6,23903);
 U brush=CreateSolidBrush(0x123456),region=CreateRectRgn(0,0,20,20),font=GetStockObject(13);unsigned short pal[]={0x300,1,0,0};U palette=CreatePalette(pal);
 check(brush&&region&&font&&palette,23904);check(GetObjectType(brush)==2&&GetObjectType(font)==6&&GetObjectType(region)==8&&GetObjectType(palette)==5,23905);
 U dc=CreateCompatibleDC(0);check(dc&&GetObjectType(dc)==10,23906);
 struct {U size;int w,h;unsigned short planes,depth;U compression,bytes;int x,y;U colors,important;} info={40,8,-8,1,32,0,0,0,0,0,0};void*bits=0;U bitmap=CreateDIBSection(dc,&info,0,&bits,0,0);check(bitmap&&bits&&GetObjectType(bitmap)==7,23907);
 U kinds[]={1,2,5,6,7},old[5];for(U i=0;i<5;++i){old[i]=GetCurrentObject(dc,kinds[i]);check(old[i]&&GetObjectType(old[i])==kinds[i],23908);}
 U saved=SaveDC(dc);check(saved,23909);check(SelectObject(dc,pen)==old[0]&&SelectObject(dc,brush)==old[1]&&SelectPalette(dc,palette,0)==old[2]&&SelectObject(dc,font)==old[3]&&SelectObject(dc,bitmap)==old[4],23910);
 U objects[]={pen,brush,palette,font,bitmap};for(U i=0;i<5;++i)check(GetCurrentObject(dc,kinds[i])==objects[i],23911);
 check(RestoreDC(dc,-1),23912);for(U i=0;i<5;++i)check(GetCurrentObject(dc,kinds[i])==old[i],23913);
 SetLastError(0x777);check(!GetCurrentObject(dc,3)&&!GetCurrentObject(dc,4)&&!GetCurrentObject(dc,8)&&!GetCurrentObject(dc,0)&&!GetCurrentObject(0,1)&&GetLastError()==0x777,23914);
 U screen=GetDC(0);check(screen&&GetObjectType(screen)==3,23915);ReleaseDC(0,screen);
 U display=CreateDCA("DISPLAY",0,0,0),ic=CreateICA("DISPLAY",0,0,0);check(display&&ic&&GetObjectType(display)==3&&GetObjectType(ic)==3,23916);check(GetCurrentObject(display,1)&&GetCurrentObject(ic,6),23917);
 U module=GetModuleHandleA("GDI32.DLL");typedef U(__stdcall *One)(U);typedef U(__stdcall *Two)(U,U);One bytype=(One)GetProcAddress(module,(const char*)283);Two bycurrent=(Two)GetProcAddress(module,(const char*)245);check(bytype&&bycurrent&&bytype(pen)==1&&bycurrent(dc,1)==old[0],23918);
 check(DeleteDC(display)&&DeleteDC(ic)&&DeleteDC(dc),23919);check(!GetCurrentObject(dc,1)&&!GetObjectType(dc)&&GetLastError()==6,23920);
 check(DeleteObject(pen)&&DeleteObject(brush)&&DeleteObject(region)&&DeleteObject(palette)&&DeleteObject(bitmap),23921);
}
#ifdef GDI_OBJECT_TYPE_STANDALONE
void mainCRTStartup(void){run_gdi_object_type_test();ExitProcess(0);}
#endif
