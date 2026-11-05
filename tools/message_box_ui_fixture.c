// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetModuleHandleA(const char*);
API U RegisterClassA(const U*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);API U DestroyWindow(U);API U SetFocus(U);API U GetFocus(void);
API U IsWindow(U);API U IsWindowEnabled(U);API U GetParent(U);API U SetWindowTextA(U,const char*);
API U MessageBoxA(U,const char*,const char*,U);API U MessageBoxExA(U,const char*,const char*,U,U);
typedef struct {U size,owner,instance;const char*text,*caption;U style,icon,context,callback,language;} Parameters;
API U MessageBoxIndirectA(const Parameters*);
static U owner,sibling,phase,callback_count,owner_count;
static void check(U condition,U code){if(!condition)ExitProcess(code);}
static void help(const U*info,U callback){
 check(info&&info[0]==28&&info[1]==1&&info[2]==9,23100);
 check(IsWindow(info[3])&&GetParent(GetFocus())==info[3],23101);
 check(!IsWindowEnabled(owner),23102);
 check(info[5]<800&&info[6]<600,23103);
 if(callback){
  check(phase==2&&info[4]==0x12345678,23104);++callback_count;
  check(callback_count<=2,23105);
  SetWindowTextA(owner,callback_count==1?"callback help 1":"callback help 2");
 }else{
  check((phase==3&&info[4]==0xabcdef01)||(phase==4&&info[4]==0),23106);
  ++owner_count;SetWindowTextA(owner,phase==3?"owner help 1":"owner help 2");
 }
}
static void __stdcall callback(const U*info){help(info,1);}
static U __stdcall procedure(U h,U message,U wp,U lp){if(message==0x53){help((const U*)lp,0);return 0;}return DefWindowProcA(h,message,wp,lp);}
static void restored(U code){check(IsWindowEnabled(owner)&&IsWindowEnabled(sibling)&&GetFocus()==owner,code);}
void mainCRTStartup(void){
 U instance=GetModuleHandleA(0),klass[]={0,(U)procedure,0,0,instance,0,0,16,0,(U)"MessageBoxOwner98"};
 check(RegisterClassA(klass),23110);
 owner=CreateWindowExA(0,"MessageBoxOwner98","native owner",0x10cf0000,15,15,330,125,0,0,instance,0);
 sibling=CreateWindowExA(0,"MessageBoxOwner98","native sibling",0x10cf0000,400,20,330,120,0,0,instance,0);
 check(owner&&sibling,23111);SetFocus(owner);
 phase=1;check(MessageBoxA(owner,"Escape returns IDOK.","Native OK",0)==1,23112);restored(23113);
 Parameters p={40,owner,instance,
  "Native callback help.\nSecond line wraps across the message box because it contains enough words to need another visible line.\nThird line remains visible.",
  "Native callback",0x4103,0,0x12345678,(U)callback,0x409};
 phase=2;check(MessageBoxIndirectA(&p)==7,23114);check(callback_count==2&&!owner_count,23115);restored(23116);
 p.text="Help reaches the owner.";p.caption="Native owner help";p.style=0x4001;p.context=0xabcdef01;p.callback=0;
 phase=3;check(MessageBoxIndirectA(&p)==2,23117);check(owner_count==1&&callback_count==2,23118);restored(23119);
 p.text="Escape must keep this box open.";p.caption="Native no Cancel";p.style=4;p.context=0xbadbad;p.callback=(U)callback;
 phase=4;check(MessageBoxIndirectA(&p)==6,23120);check(owner_count==2&&callback_count==2,23121);restored(23122);
 phase=5;check(MessageBoxExA(0,"Both task windows are disabled.","Native task modal",0x2105,0x409)==2,23123);
 restored(23124);check(DestroyWindow(sibling)&&DestroyWindow(owner),23125);ExitProcess(0);
}
