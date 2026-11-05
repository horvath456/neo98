/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;typedef int I;
#define API(t,n,...) __declspec(dllimport) t __stdcall n(__VA_ARGS__)
API(void,ExitProcess,U);API(U,GetModuleHandleA,const char*);API(U,LoadLibraryA,const char*);API(U,FreeLibrary,U);API(U,GetLastError,void);API(void,SetLastError,U);
API(U,LoadImageA,U,const void*,U,I,I,U);API(U,LoadIconA,U,const void*);API(U,LoadCursorA,U,const void*);API(U,LoadBitmapA,U,const void*);API(U,LoadCursorFromFileA,const char*);
API(I,LookupIconIdFromDirectory,const void*,I);API(I,LookupIconIdFromDirectoryEx,const void*,I,I,I,U);API(U,CreateIconFromResource,const void*,U,I,U);API(U,CreateIconFromResourceEx,const void*,U,I,U,I,I,U);API(U,DestroyIcon,U);API(U,DestroyCursor,U);API(U,DrawIconEx,U,I,I,U,I,I,U,U,U);
API(U,LoadBitmapW,U,const void*);API(U,LoadIconW,U,const void*);API(U,LoadCursorW,U,const void*);API(U,LoadImageW,U,const void*,U,I,I,U);API(U,LoadCursorFromFileW,const void*);
API(U,CreateFileA,const char*,U,U,U,U,U,U);API(U,WriteFile,U,const void*,U,U*,U);API(U,CloseHandle,U);API(U,DeleteFileA,const char*);
API(U,CreateCompatibleDC,U);API(U,CreateBitmap,I,I,U,U,const void*);API(U,SelectObject,U,U);API(U,DeleteDC,U);API(U,DeleteObject,U);API(U,GetPixel,U,I,I);API(U,GetObjectA,U,I,void*);
#include "../build/image-resource/fixture-images.h"
#define ID(x) ((const void*)(x))
#define CHECK(x,c) do{if(!(x))return c;}while(0)
static int draw(U h){U dc=CreateCompatibleDC(0),bm=CreateBitmap(4,4,1,32,0),old=SelectObject(dc,bm);if(!dc||!bm||!old)return 0;U ok=DrawIconEx(dc,0,0,h,2,2,0,0,3)&&GetPixel(dc,0,0)==0x00ff00;SelectObject(dc,old);DeleteObject(bm);DeleteDC(dc);return ok;}
static int seed(const char*name,const void*p,U n){U h=CreateFileA(name,0x40000000,0,0,2,0,0),done=0;if(h==~0u)return 0;U ok=WriteFile(h,p,n,&done,0)&&done==n;return CloseHandle(h)&&ok;}
int run_image_resource_test(void){
 CHECK(seed("C:\\IMAGERES.DLL",image_dll,sizeof image_dll)&&seed("C:\\IMAGE.ICO",image_ico,sizeof image_ico)&&seed("C:\\IMAGE.CUR",image_cur,sizeof image_cur)&&seed("C:\\IMAGE.BMP",image_bmp,sizeof image_bmp),8337);
 U mod=GetModuleHandleA(0),h=LoadImageA(mod,ID(101),1,2,2,0);CHECK(h&&draw(h),8300);CHECK(DestroyIcon(h),8301);CHECK(!DestroyIcon(h),8302);
 U shared=LoadImageA(mod,"NAMED_ICON",1,16,16,0x8000);CHECK(shared,8303);CHECK(LoadImageA(mod,"NAMED_ICON",1,32,32,0x8040)==shared,8304);CHECK(DestroyIcon(shared)&&draw(shared),8305);CHECK(LoadIconA(mod,"NAMED_ICON")==shared,8306);
 U sys=LoadIconA(0,ID(32512));CHECK(sys&&LoadIconA(0,ID(32512))==sys,8307);CHECK(!DestroyIcon(sys),8308);U cursor=LoadCursorA(0,ID(32512));CHECK(cursor&&LoadCursorA(0,ID(32512))==cursor,8309);CHECK(!DestroyCursor(cursor),8310);
 U owncur=LoadImageA(mod,ID(201),2,2,2,0);CHECK(owncur&&DestroyCursor(owncur),8311);CHECK(LoadCursorA(mod,"NAMED_CURSOR"),8312);
 CHECK((U)LookupIconIdFromDirectory(image_group,1)==0xffff8001,8313);CHECK((U)LookupIconIdFromDirectoryEx(image_group,0x10001,0x10002,0x10002,0x10000)==0xffff8001,8314);
 h=CreateIconFromResource(image_icon,sizeof image_icon,1,0x30000);CHECK(h&&draw(h)&&DestroyIcon(h),8315);h=CreateIconFromResourceEx(image_icon,sizeof image_icon,0x10001,0x40000,0x10002,0x10002,0x10000);CHECK(h&&draw(h)&&DestroyIcon(h),8316);CHECK(!CreateIconFromResourceEx(image_icon,sizeof image_icon,1,0x40001,2,2,0),8317);CHECK(!CreateIconFromResourceEx(image_icon,39,1,0x30000,2,2,0),8318);
 h=LoadImageA(mod,ID(101),0x10001,0x10002,0x10002,0x10000);CHECK(h&&draw(h)&&DestroyIcon(h),8319);CHECK(!LoadImageA(mod,ID(101),3,0,0,0),8320);CHECK(!LoadImageA(mod,ID(65530),1,0,0,0),8321);
 U dll=LoadLibraryA("C:\\IMAGERES.DLL");CHECK(dll,8322);h=LoadImageA(dll,"NAMED_ICON",1,2,2,0);CHECK(h&&draw(h),8323);CHECK(FreeLibrary(dll)&&draw(h)&&DestroyIcon(h),8324);
 h=LoadImageA(0,"C:\\IMAGE.ICO",1,2,2,0x8010);U h2=LoadImageA(0,"C:\\IMAGE.ICO",1,2,2,0x8010);CHECK(h&&h2&&h!=h2&&draw(h)&&draw(h2),8325);CHECK(DestroyIcon(h)&&DestroyIcon(h2),8326);h=LoadCursorFromFileA("C:\\IMAGE.CUR");CHECK(h&&DestroyCursor(h),8327);
 U bm=LoadBitmapA(mod,ID(301));int b[6]={0};CHECK(bm&&GetObjectA(bm,24,b)==24&&b[1]==2&&b[2]==2,8328);CHECK(DeleteObject(bm),8329);bm=LoadBitmapA(mod,"NAMED_BITMAP");CHECK(bm&&DeleteObject(bm),8330);bm=LoadImageA(0,"C:\\IMAGE.BMP",0,0,0,0x10);CHECK(bm&&DeleteObject(bm),8331);
 SetLastError(17);CHECK(!LoadBitmapW(0,0)&&GetLastError()==120,8332);SetLastError(17);CHECK(!LoadIconW(0,0)&&GetLastError()==120,8333);SetLastError(17);CHECK(!LoadCursorW(0,0)&&GetLastError()==120,8334);SetLastError(17);CHECK(!LoadImageW(0,0,1,0,0,0)&&GetLastError()==120,8335);SetLastError(17);CHECK(!LoadCursorFromFileW(0)&&GetLastError()==120,8336);
 CHECK(DeleteFileA("C:\\IMAGERES.DLL")&&DeleteFileA("C:\\IMAGE.ICO")&&DeleteFileA("C:\\IMAGE.CUR")&&DeleteFileA("C:\\IMAGE.BMP"),8338);
 return 0;
}
#ifdef IMAGE_RESOURCE_STANDALONE
void mainCRTStartup(void){ExitProcess(run_image_resource_test());}
#endif
