/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;typedef struct{int l,t,r,b;}Rect;
#define API __declspec(dllimport) __stdcall
API U CreateCompatibleDC(U);API U DeleteDC(U);API U CreateDIBSection(U,const void*,U,void**,U,U);API U SelectObject(U,U);API U DeleteObject(U);API U DrawFocusRect(U,const Rect*);API U GetPixel(U,int,int);API U SetTextColor(U,U);API U SetBkColor(U,U);API U GetTextColor(U);API U GetBkColor(U);API U GetCurrentObject(U,U);API U IntersectClipRect(U,int,int,int,int);API U SetViewportOrgEx(U,int,int,void*);API U SaveDC(U);API U RestoreDC(U,int);API U SetMapMode(U,int);API U SetWindowExtEx(U,int,int,void*);API U SetViewportExtEx(U,int,int,void*);API void SetLastError(U);API U GetLastError(void);API void ExitProcess(U);
static void check(int b,U e){if(!b)ExitProcess(e);}
static void clear(U*p){for(U i=0;i<1024;++i)p[i]=0x123456;}
static void restored(U dc){for(int y=0;y<32;++y)for(int x=0;x<32;++x)check(GetPixel(dc,x,y)==0x563412,34808);}
void mainCRTStartup(void){
 for(U i=0;i<260;++i){U c=CreateCompatibleDC(0);check(c&&DeleteDC(c),34800);}
 U dc=CreateCompatibleDC(0),*pixels=0;int h[10]={40,32,-32,0x200001,0,4096,0,0,0,0};U bitmap=CreateDIBSection(dc,h,0,(void**)&pixels,0,0);check(dc&&bitmap&&pixels,34801);U old=SelectObject(dc,bitmap),brush=GetCurrentObject(dc,2);check(old&&brush,34802);SetTextColor(dc,0);SetBkColor(dc,0xffffff);clear(pixels);Rect r={2,3,20,15};SetLastError(0x76543210);check(DrawFocusRect(dc,&r),34803);check(GetLastError()==0x76543210,34804);
 for(int y=0;y<32;++y)for(int x=0;x<32;++x){int edge=((y==3||y==14)&&x>=2&&x<20)||((x==2||x==19)&&y>=4&&y<14);U want=0x563412;if(edge&&((x+y)&1))want^=0xffffff;check(GetPixel(dc,x,y)==want,34805);}
 check(GetCurrentObject(dc,2)==brush&&GetTextColor(dc)==0&&GetBkColor(dc)==0xffffff,34806);check(DrawFocusRect(dc,&r),34807);restored(dc);
 check(SaveDC(dc),34809);check(IntersectClipRect(dc,7,4,17,15),34810);check(DrawFocusRect(dc,&r),34811);for(int y=0;y<32;++y)for(int x=0;x<32;++x){U want=0x563412;if(y==14&&x>=7&&x<17&&((x+y)&1))want^=0xffffff;check(pixels[y*32+x]==(((want&255)<<16)|(want&0xff00)|((want>>16)&255)),34812);}check(DrawFocusRect(dc,&r)&&RestoreDC(dc,-1),34813);restored(dc);
 check(SaveDC(dc)&&SetViewportOrgEx(dc,3,2,0),34814);check(DrawFocusRect(dc,&r)&&DrawFocusRect(dc,&r)&&RestoreDC(dc,-1),34815);restored(dc);
 Rect wide={65538,65539,65556,65551};check(DrawFocusRect(dc,&wide)&&DrawFocusRect(dc,&r),34816);restored(dc);
 Rect tiny={3,4,4,5},empty={3,4,3,9};check(DrawFocusRect(dc,&tiny)&&DrawFocusRect(dc,&tiny)&&DrawFocusRect(dc,&empty)&&DrawFocusRect(dc,&empty),34817);restored(dc);
 check(SaveDC(dc)&&SetMapMode(dc,8)&&SetWindowExtEx(dc,1,1,0)&&SetViewportExtEx(dc,-1,1,0)&&SetViewportOrgEx(dc,25,0,0),34818);Rect mirror={20,3,2,15};check(DrawFocusRect(dc,&mirror)&&DrawFocusRect(dc,&mirror)&&RestoreDC(dc,-1),34819);restored(dc);
 SetTextColor(dc,0x010203);SetBkColor(dc,0x102030);check(DrawFocusRect(dc,&r)&&DrawFocusRect(dc,&r),34820);restored(dc);check(!DrawFocusRect(0,&r)&&!DrawFocusRect(dc,(void*)0),34821);check(SelectObject(dc,old)&&DeleteObject(bitmap)&&DeleteDC(dc),34822);check(!DrawFocusRect(dc,&r),34823);ExitProcess(0);
}
