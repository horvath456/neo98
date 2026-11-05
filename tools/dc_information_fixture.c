/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API void SetLastError(U);
API U WriteProfileStringA(const char*,const char*,const char*);
API U CreateDCA(const char*,const char*,const char*,const void*);API U CreateICA(const char*,const char*,const char*,const void*);
API U CreateDCW(const void*,const void*,const void*,const void*);API U CreateICW(const void*,const void*,const void*,const void*);
API U GetDeviceCaps(U,U);API U DeleteDC(U);API U CreateCompatibleDC(U);API U CreateCompatibleBitmap(U,int,int);API U GetObjectA(U,U,void*);
API U GetPixel(U,int,int);API U PatBlt(U,int,int,int,int,U);API U SaveDC(U);API U RestoreDC(U,int);API U GetMapMode(U);API U GetROP2(U);API U GetStretchBltMode(U);
API U CreateSolidBrush(U);API U SelectObject(U,U);API U DeleteObject(U);API U GetTextMetricsA(U,void*);API U GetTextExtentPoint32A(U,const char*,U,void*);
static void check(int condition,U code){if(!condition)ExitProcess(code);}
void run_dc_information_test(void){
 SetLastError(99);check(!CreateDCW((void*)1,(void*)1,(void*)1,(void*)1)&&GetLastError()==120,9900);SetLastError(99);check(!CreateICW((void*)1,0,0,0)&&GetLastError()==120,9901);
 U dc=CreateDCA("DISPLAY",0,0,0),ic=CreateICA("display",0,0,0);check(dc&&ic&&dc!=ic,9902);
 check(GetDeviceCaps(ic,0)==0x400&&GetDeviceCaps(ic,2)==1&&GetDeviceCaps(ic,8)==800&&GetDeviceCaps(ic,10)==600&&GetDeviceCaps(ic,12)==32&&GetDeviceCaps(ic,14)==1,9903);
 check(GetDeviceCaps(dc,88)==96&&GetDeviceCaps(ic,90)==96&&GetDeviceCaps(ic,16)==~0u&&GetDeviceCaps(ic,18)==~0u&&GetDeviceCaps(ic,24)==~0u,9904);
 check(!GetDeviceCaps(0,8)&&!GetDeviceCaps(0xdead,8)&&!GetDeviceCaps(ic,0xffffffff)&&!GetDeviceCaps(ic,110),9905);
 check(GetMapMode(ic)==1&&GetROP2(ic)==13&&GetStretchBltMode(ic)==1,9906);
 U text[14]={0},extent[2]={0};check(GetTextMetricsA(ic,text)&&text[0]&&GetTextExtentPoint32A(ic,"Windows 98",10,extent)&&extent[0]&&extent[1],9907);
 U color=GetPixel(dc,780,580);check(!PatBlt(ic,780,580,2,2,0x00ff0062)&&GetPixel(ic,780,580)==~0u&&GetPixel(dc,780,580)==color,9908);
 U brush=CreateSolidBrush(0x234567),old=SelectObject(dc,brush);check(brush&&old&&PatBlt(dc,780,580,2,2,0x00f00021)&&GetPixel(dc,780,580)==0x234567,9909);
 check(SelectObject(dc,old)==brush&&DeleteObject(brush),9910);
 U bitmap=CreateCompatibleBitmap(ic,4,3),info[6]={0};check(bitmap&&GetObjectA(bitmap,24,info)==24&&info[1]==4&&info[2]==3&&(info[4]>>16)==32,9911);
 check(!SelectObject(ic,bitmap)&&!SelectObject(dc,bitmap),9912);
 U mem=CreateCompatibleDC(ic),previous=SelectObject(mem,bitmap);check(mem&&previous&&GetDeviceCaps(mem,12)==32,9913);check(SelectObject(mem,previous)==bitmap&&DeleteObject(bitmap)&&DeleteDC(mem),9914);
 check(SaveDC(ic)==1&&RestoreDC(ic,-1),9915);check(DeleteDC(ic)&&!DeleteDC(ic)&&!GetDeviceCaps(ic,8)&&DeleteDC(dc),9916);
 check(!CreateICA(0,0,0,0),9917);SetLastError(0);check(!CreateDCA("not-installed",0,0,0)&&GetLastError()==0,9918);SetLastError(0);check(!CreateICA("DISPLAY",0,0,(void*)1)&&GetLastError()==50,9919);
 check(WriteProfileStringA("devices","Mapped display","DISPLAY,"),9920);ic=CreateICA("not-installed","Mapped display",0,0);check(ic&&DeleteDC(ic),9921);
 check(WriteProfileStringA("devices","Mapped display",0),9922);
 /* The native object tables run out; the original limit is the GDI local heap,
    which this provider does not implement. How many contexts fit depends on
    what the rest of this program still holds, so only the exhaustion itself and
    the usability of every context created before it are part of the contract. */
 enum { HoldLimit = 65536 }; /* Exceeds the complete WORD object-index space. */
 static U held[HoldLimit];U count=0;while(count<HoldLimit&&(held[count]=CreateICA("DISPLAY",0,0,0)))++count;
 check(count>0&&count<HoldLimit,9925);for(U i=0;i<count;++i)check(GetDeviceCaps(held[i],8)==800&&DeleteDC(held[i]),9926);
 for(U i=0;i<260;++i){ic=CreateICA("DISPLAY",0,0,0);check(ic&&GetDeviceCaps(ic,8)==800&&DeleteDC(ic),9923);}
}
#ifdef DC_INFORMATION_STANDALONE
void mainCRTStartup(void){run_dc_information_test();U dc=CreateICA("DISPLAY",0,0,0),brush=CreateSolidBrush(0x123456);check(dc&&brush&&SelectObject(dc,brush)&&SaveDC(dc)==1,9924);ExitProcess(0);}
#endif
