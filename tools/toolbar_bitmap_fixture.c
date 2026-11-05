/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetModuleHandleA(const char*);API void InitCommonControls(void);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);API U DestroyWindow(U);API U SendMessageA(U,U,U,U);API U GetWindowLongA(U,int);API U ImageList_GetImageCount(U);API U ImageList_Destroy(U);API U LoadBitmapA(U,const char*);API U DeleteObject(U);API U GetDC(U);API U ReleaseDC(U,U);API U GetPixel(U,int,int);API U ScreenToClient(U,void*);API U UpdateWindow(U);
static void check(int ok,U code){if(!ok)ExitProcess(code);}
void run_toolbar_bitmap_test(void){
 InitCommonControls();U instance=GetModuleHandleA(0);U parent=CreateWindowExA(0,"STATIC","Real strip",0x90000000,20,20,240,100,0,0,instance,0);check(parent,14900);
 U bar=CreateWindowExA(0,"ToolbarWindow32","",0x5000004c,0,0,200,70,parent,71,instance,0);check(bar&&SendMessageA(bar,0x420,0,0x000f0010),14901);U add[2]={instance,101};
 check(SendMessageA(bar,0x413,2,(U)add)==0,14902);U list=SendMessageA(bar,0x431,0,0);check(list&&ImageList_GetImageCount(list)==2,14903);check(SendMessageA(bar,0x413,1,(U)add)==0&&ImageList_GetImageCount(list)==2,14904);check(SendMessageA(bar,0x413,3,(U)add)==0xffffffff&&ImageList_GetImageCount(list)==2,14905);
 U object=GetWindowLongA(bar,0);check(*(U*)(object+0x50)==1,14906);U bitmap=LoadBitmapA(instance,(const char*)101);check(bitmap,14907);add[0]=0;add[1]=bitmap;check(SendMessageA(bar,0x413,2,(U)add)==2&&ImageList_GetImageCount(list)==4&&DeleteObject(bitmap),14908);
 add[0]=instance;add[1]=999;check(SendMessageA(bar,0x413,1,(U)add)==0xffffffff&&*(U*)(object+0x50)==2&&ImageList_GetImageCount(list)==4,14909);
 SendMessageA(bar,0x41e,20,0);U buttons[10]={0,100,4,0,0xffffffff,1,101,4,0,0xffffffff};check(SendMessageA(bar,0x414,2,(U)buttons),14910);int r[4],origin[2]={0,0};check(SendMessageA(bar,0x41d,0,(U)r)&&ScreenToClient(bar,origin),14911);UpdateWindow(bar);U dc=GetDC(0);check(dc,14912);U pixel=GetPixel(dc,-origin[0]+(r[0]+r[2])/2,-origin[1]+(r[1]+r[3])/2);check(pixel==0x0000ff,14913);ReleaseDC(0,dc);
 check(SendMessageA(bar,0x41d,1,(U)r),14914);UpdateWindow(bar);dc=GetDC(0);check(GetPixel(dc,-origin[0]+(r[0]+r[2])/2,-origin[1]+(r[1]+r[3])/2)==0xff0000,14915);ReleaseDC(0,dc);
 // The strip's image list belongs to the toolbar; its generation dies with it.
 check(DestroyWindow(bar)&&!ImageList_GetImageCount(list)&&!ImageList_Destroy(list)&&DestroyWindow(parent),14916);
}
#ifdef TOOLBAR_BITMAP_STANDALONE
void mainCRTStartup(void){run_toolbar_bitmap_test();ExitProcess(0);}
#endif
