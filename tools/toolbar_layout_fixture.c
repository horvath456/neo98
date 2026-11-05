// SPDX-License-Identifier: GPL-2.0-or-later
#define U unsigned
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetModuleHandleA(const char*);API void InitCommonControls(void);
API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);API U DestroyWindow(U);API U SendMessageA(U,U,U,U);API U GetWindowLongA(U,int);API U GetObjectA(U,U,U);API U GetLastError(void);
#define CHECK(x,n) do{if(!(x))return n;}while(0)
int run_toolbar_layout_test(void){
 InitCommonControls();U instance=GetModuleHandleA(0),h=CreateWindowExA(0,"ToolbarWindow32","",0,0,0,200,24,0,0,instance,0);CHECK(h,9800);CHECK(SendMessageA(h,0x43a,0,0)==0x00160017,9801);
 SendMessageA(h,0x41e,20,0);U buttons[15]={0xffffffff,101,4,0,0xffffffff,8,102,0x0124,0,0xffffffff,0xffffffff,103,4,0,0xffffffff};CHECK(SendMessageA(h,0x444,3,(U)buttons),9802);
 int r[4]={0};CHECK(SendMessageA(h,0x41d,0,(U)r)&&r[0]==0&&r[1]==2&&r[2]==23&&r[3]==24,9803);
 CHECK(SendMessageA(h,0x433,102,(U)r)&&r[0]==0&&r[1]==2&&r[2]==8&&r[3]==29,9804);
 CHECK(SendMessageA(h,0x41d,2,(U)r)&&r[0]==0&&r[1]==29&&r[2]==23&&r[3]==51,9805);
 CHECK(!SendMessageA(h,0x433,999,(U)r),9806);CHECK(SendMessageA(h,0x411,103,12)&&!SendMessageA(h,0x41d,2,(U)r),9807);CHECK(SendMessageA(h,0x411,103,4),9808);
 CHECK(SendMessageA(h,0x41f,0,0x00280032)&&SendMessageA(h,0x43a,0,0)==0x00280032,9809);
 CHECK(SendMessageA(h,0x41d,0,(U)r)&&r[2]==50&&r[3]==42,9810);CHECK(SendMessageA(h,0x41d,2,(U)r)&&r[1]==47&&r[3]==87,9811);
 U object=GetWindowLongA(h,0),bitmap=*(U*)(object+0x2c),info[6]={0};CHECK(bitmap&&GetObjectA(bitmap,24,(U)info)==24&&info[1]==58&&info[2]==48,9812);
 CHECK(SendMessageA(h,0x41f,0,0x00160017)&&*(U*)(object+0x2c)==bitmap,9813);CHECK(DestroyWindow(h),9814);CHECK(!GetObjectA(bitmap,24,(U)info),9815);return 0;
}
#ifdef TOOLBAR_LAYOUT_STANDALONE
void mainCRTStartup(void){ExitProcess(run_toolbar_layout_test());}
#endif
