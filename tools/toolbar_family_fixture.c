// SPDX-License-Identifier: GPL-2.0-or-later
#define API __declspec(dllimport) __stdcall
#define U unsigned
API void ExitProcess(U);API U GetModuleHandleA(const char*);API void InitCommonControls(void);
API U RegisterClassA(const U*);API U UnregisterClassA(const char*,U);API U GetLastError(void);
API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);API U DestroyWindow(U);API U DefWindowProcA(U,U,U,U);API U SendMessageA(U,U,U,U);
static U deleted,notifications,callback_error,destroy_in_notify;
static U __stdcall proc(U h,U m,U w,U l){
 if(m==0x4e){U*n=(U*)l;if(n[2]==0xfffffd35){++notifications;deleted=n[3];if(!SendMessageA(n[0],0x418,0,0)||n[5]!=deleted)callback_error=1;if(destroy_in_notify){if(SendMessageA(n[0],0x411,deleted,4)||GetLastError()!=170)callback_error=2;DestroyWindow(n[0]);}}return 0;}
 return DefWindowProcA(h,m,w,l);
}
#define CHECK(x,n) do{if(!(x))return n;}while(0)
int run_toolbar_family_test(void){
 InitCommonControls();U instance=GetModuleHandleA(0),f[10]={0,(U)proc,0,0,instance};f[9]=(U)"ToolbarObjectParent";CHECK(RegisterClassA(f),9500);
 U parent=CreateWindowExA(0,"ToolbarObjectParent","toolbar object",0,0,0,200,100,0,0,instance,0);CHECK(parent,9501);
 U h=CreateWindowExA(0,"ToolbarWindow32","",0x40000000,0,0,200,24,parent,51,instance,0);CHECK(h,9502);
 CHECK(!SendMessageA(h,0x418,0,0),9503);CHECK(!SendMessageA(h,0x41e,20,0),9504);
 U buttons[10]={0xffffffff,101,4,0x12345678,(U)"One",0,102,0x0104,0,0xffffffff};
 CHECK(SendMessageA(h,0x414,2,(U)buttons),9505);CHECK(SendMessageA(h,0x418,0,0)==2&&SendMessageA(h,0x419,101,0)==0,9506);
 U out[5]={0};CHECK(SendMessageA(h,0x417,0,(U)out)&&out[0]==0xffffffff&&out[1]==101&&out[2]==4&&out[3]==0x12345678,9507);CHECK(((unsigned short*)out[4])[0]=='O'&&((unsigned short*)out[4])[3]==0,9508);
 CHECK(SendMessageA(h,0x411,101,0x85)&&SendMessageA(h,0x412,101,0)==0x85&&SendMessageA(h,0x40e,101,0)==128,9509);
 unsigned short wide[]={'W','i','d','e',0};U info[8]={32,0x32,201,0,0,0xaabbccdd,(U)wide,0};CHECK(SendMessageA(h,0x440,101,(U)info),9510);
 CHECK(SendMessageA(h,0x419,101,0)==0xffffffff&&SendMessageA(h,0x419,201,0)==0,9511);
 unsigned short text[16]={0};info[1]=0x36;info[6]=(U)text;info[7]=16;CHECK(SendMessageA(h,0x43f,201,(U)info)==0&&info[2]==201&&info[5]==0xaabbccdd&&(info[4]&255)==0x85&&text[0]=='W'&&text[4]==0,9512);
 char narrow[16]={0};info[1]=2;info[6]=(U)narrow;info[7]=16;CHECK(SendMessageA(h,0x441,201,(U)info)==0&&narrow[0]=='W'&&narrow[4]==0&&info[1]==2,9530);
 info[6]=(U)"Ansi";CHECK(SendMessageA(h,0x442,201,(U)info),9531);info[6]=(U)text;CHECK(SendMessageA(h,0x43f,201,(U)info)==0&&text[0]=='A'&&text[4]==0,9532);
 info[1]=2;CHECK(SendMessageA(h,0x441,999,(U)info)==0xffffffff&&info[1]==0,9533);
 info[1]=0x80000020;info[2]=999;CHECK(!SendMessageA(h,0x440,800,(U)info)&&GetLastError()==87,9513);
 CHECK(!SendMessageA(h,0x414,0,(U)buttons)&&SendMessageA(h,0x418,0,0)==2,9514);
 CHECK(!SendMessageA(h,0x41e,12,0),9515);CHECK(SendMessageA(h,0x417,0,(U)out)&&out[3]==0xaabbccdd,9516);
 CHECK(SendMessageA(h,0x416,0,0)&&deleted==201&&notifications==1&&!callback_error,9517);CHECK(SendMessageA(h,0x418,0,0)==1&&SendMessageA(h,0x419,102,0)==0,9518);
 CHECK(SendMessageA(h,0x416,0,0)&&notifications==2&&SendMessageA(h,0x418,0,0)==0,9519);
 U next[5]={0xffffffff,303,4,77,(U)wide};for(U i=0;i<80;++i){CHECK(SendMessageA(h,0x443,0,(U)next),9520);CHECK(SendMessageA(h,0x417,0,(U)out)&&((unsigned short*)out[4])[0]=='W',9521);CHECK(SendMessageA(h,0x416,0,0),9522);}
 CHECK(DestroyWindow(h),9523);h=CreateWindowExA(0,"ToolbarWindow32","",0x40000000,0,0,200,24,parent,52,instance,0);CHECK(h,9534);SendMessageA(h,0x41e,20,0);CHECK(SendMessageA(h,0x443,0,(U)next),9535);destroy_in_notify=1;CHECK(SendMessageA(h,0x416,0,0)&&!callback_error,9536);CHECK(DestroyWindow(parent),9537);CHECK(UnregisterClassA("ToolbarObjectParent",instance),9524);return 0;
}
#ifdef TOOLBAR_FAMILY_STANDALONE
void mainCRTStartup(void){ExitProcess(run_toolbar_family_test());}
#endif
