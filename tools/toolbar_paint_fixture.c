/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API void SetLastError(U);API U GetModuleHandleA(const char*);
API void InitCommonControls(void);API U ImageList_Create(int,int,U,int,int);API int ImageList_ReplaceIcon(U,int,U);API U ImageList_Destroy(U);API U ImageList_GetImageCount(U);
API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);API U DestroyWindow(U);API U SendMessageA(U,U,U,U);API U LoadIconA(U,const char*);API U GetDC(U);API U ReleaseDC(U,U);API U GetPixel(U,int,int);API U ScreenToClient(U,void*);API U GetWindowLongA(U,int);API U CreateFontA(int,int,int,int,int,U,U,U,U,U,U,U,U,const char*);API U DeleteObject(U);API U UpdateWindow(U);
static void check(int ok,U code){if(!ok)ExitProcess(code);}static U bar;static int origin[2];
static U pixel(int x,int y){U dc=GetDC(0);check(dc,14500);U color=GetPixel(dc,origin[0]+x,origin[1]+y);check(ReleaseDC(0,dc),14501);return color;}
// GetDC does not paint. Pump the toolbar before reading its pixels.
static U hash(int l,int t,int r,int b){UpdateWindow(bar);U value=0x12345678;for(int y=t;y<b;y+=2)for(int x=l;x<r;x+=2)value=(value*33)^pixel(x,y);return value;}
void run_toolbar_paint_test(void){
 InitCommonControls();U instance=GetModuleHandleA(0),parent=CreateWindowExA(0,"STATIC","Toolbar example",0x90000000,30,30,380,140,0,0,instance,0);check(parent,14502);
 bar=CreateWindowExA(0,"ToolbarWindow32","",0x5000004c,4,4,360,110,parent,77,instance,0);check(bar&&ScreenToClient(bar,origin),14503);origin[0]=-origin[0];origin[1]=-origin[1];
 check(SendMessageA(bar,0x420,0,0x000f0010)&&SendMessageA(bar,0x43a,0,0)==0x00150017,14504);
 U o=GetWindowLongA(bar,0),old_bitmap=*(U*)(o+0x2c);check(old_bitmap,14505);check(SendMessageA(bar,0x420,0,0xffffffff)&&*(U*)(o+0x2c)==old_bitmap,14506);
 check(SendMessageA(bar,0x456,0,0)==0x00060007&&SendMessageA(bar,0x457,0,0x00080009)==0x00060007&&SendMessageA(bar,0x456,0,0)==0x00080009,14507);
 check(SendMessageA(bar,0x457,0,0x0006ffff)==0x00080009&&SendMessageA(bar,0x456,0,0)==0x00060009,14508);
 U list=ImageList_Create(16,15,1,2,2),icon=LoadIconA(0,(const char*)32512);check(list&&icon&&ImageList_ReplaceIcon(list,-1,icon)==0,14509);
 SendMessageA(bar,0x41e,20,0);check(!SendMessageA(bar,0x430,0,list)&&SendMessageA(bar,0x431,0,0)==list,14510);
 U buttons[15]={0,101,4,0,(U)"&New",8,102,0x104,0,0xffffffff,0,103,4,0,(U)"Open"};check(SendMessageA(bar,0x414,3,(U)buttons),14511);
 check(SendMessageA(bar,0x41f,0,0x00300050)&&SendMessageA(bar,0x42f,6,0),14512);int r[4];check(SendMessageA(bar,0x41d,0,(U)r)&&r[0]==6&&r[1]==2&&r[2]==86&&r[3]==50,14513);
 int point[2]={20,20};check(SendMessageA(bar,0x445,0,(U)point)==0,14514);point[0]=90;check(SendMessageA(bar,0x445,0,(U)point)==0xfffffffe,14515);point[0]=110;check(SendMessageA(bar,0x445,0,(U)point)==2,14516);
 U normal=hash(7,3,85,49);check(pixel(7,3)!=pixel(84,48),14517);check(SendMessageA(bar,0x411,101,6),14518);U pressed=hash(7,3,85,49);check(normal!=pressed,14519);
 check(SendMessageA(bar,0x411,101,0),14520);U disabled=hash(7,3,85,49);check(disabled!=normal&&disabled!=pressed,14521);check(SendMessageA(bar,0x411,101,4),14522);
 // Detaching the actual image list changes the rendered icon, not its label.
 check(SendMessageA(bar,0x430,0,0)==list&&SendMessageA(bar,0x41f,0,0x00300050),14523);U text_only=hash(7,3,85,49);check(text_only!=normal,14524);check(!SendMessageA(bar,0x430,0,list)&&SendMessageA(bar,0x41f,0,0x00300050),14525);
 // The toolbar stores the font and selects it only while measuring or drawing,
 // as the original does, so GDI does not refuse to delete it here. The fixture
 // keeps the font until the toolbar is gone; deleting it earlier would leave
 // the control with a dead handle, which is the application's error.
 U font=CreateFontA(20,0,0,0,400,0,0,0,0,0,0,0,0,"MS Sans Serif");check(font&&SendMessageA(bar,0x30,font,1)&&SendMessageA(bar,0x31,0,0)==font,14526);check(SendMessageA(bar,0x43c,0,0)&&SendMessageA(bar,0x43d,0,0)==0,14527);check(SendMessageA(bar,0x43c,1,0)&&SendMessageA(bar,0x43d,0,0)==1,14528);
 check(SendMessageA(bar,0x434,0,list)==0&&SendMessageA(bar,0x435,0,0)==list,14529);check(SendMessageA(bar,0x436,0,list)==0&&SendMessageA(bar,0x437,0,0)==list,14530);
 check(SendMessageA(bar,0x411,101,12)&&!SendMessageA(bar,0x41d,0,(U)r),14531);check(SendMessageA(bar,0x411,101,4),14532);check(SendMessageA(bar,0x428,0,0)==1,14533);
 check(DestroyWindow(bar)&&DeleteObject(font)&&ImageList_GetImageCount(list)==1&&ImageList_Destroy(list)&&DestroyWindow(parent),14534);
}
#ifdef TOOLBAR_PAINT_STANDALONE
void mainCRTStartup(void){run_toolbar_paint_test();ExitProcess(0);}
#endif
