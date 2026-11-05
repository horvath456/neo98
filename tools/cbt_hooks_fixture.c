/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U SetWindowsHookExA(int,U,U,U);API U UnhookWindowsHookEx(U);API U CallNextHookEx(U,int,U,U);
API U GetCurrentThreadId(void);API void ExitProcess(U);
API U RegisterClassA(U*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);API U CallWindowProcA(U,U,U,U,U);API U SetWindowLongA(U,int,U);
API U DestroyWindow(U);API U IsWindow(U);API U ShowWindow(U,U);API U SetFocus(U);API U GetFocus(void);
API U SetActiveWindow(U);API U GetActiveWindow(void);API U IsZoomed(U);API U GetWindowRect(U,int*);
static U hook,older,mode,depth,seen[10],oldproc,subcalls,nc,created,destroyed,ncdestroyed,nested,target,previous;
static U family[4],order[16],order_count;
static void check(int ok,U code){if(!ok)ExitProcess(code);}
static U window(void){return CreateWindowExA(0,"CbtPort", "CBT original",0,20,30,180,110,0,0,0,0);}
static U __stdcall proc(U h,U m,U w,U l){
 if(mode==11&&(m==2||m==0x82)){U i=0;while(i<4&&family[i]!=h)++i;check(i<4&&order_count<16,9440);order[order_count++]=i+(m==0x82?8:0);}
 if(m==0x81)++nc;if(m==1)++created;if(m==2)++destroyed;if(m==0x82)++ncdestroyed;
 return DefWindowProcA(h,m,w,l);
}
static U __stdcall subclass(U h,U m,U w,U l){++subcalls;return CallWindowProcA(oldproc,h,m,w,l);}
static U __stdcall tail(int code,U w,U l){if(code==3||code==4)check(IsWindow(w),9433);(void)l;++seen[code];return 0;}
static U __stdcall cbt(int code,U w,U l){
 check(code>=0&&code<=9,9400);
 if(code==3){
  U *info=(U*)l,*cs=(U*)info[0];check(IsWindow(w),9401);check(info[1]==0,9402);
  if(mode==1){check(!nc&&!created,9403);cs[7]=37;cs[6]=41;cs[5]=190;cs[4]=125;oldproc=SetWindowLongA(w,-4,(U)subclass);check(oldproc==(U)proc,9404);}
  if(mode==2)return 1;
  if(mode==3)return 0x10000; /* Original AX test ignores the high word. */
  if(mode==4&&!depth){depth=1;nested=window();check(nested,9405);depth=0;check(((U*)l)[0]==(U)cs&&cs[7]==20,9406);}
  if(mode==5){check(UnhookWindowsHookEx(hook),9407);return CallNextHookEx(hook,code,w,l);}
  if(mode==6){check(UnhookWindowsHookEx(older),9408);older=0;}
 }
 if(code==4){check(IsWindow(w),9409);if(mode==7)return 1;if(mode==11)check(w==family[0],9441);}
 if(code==5){U *info=(U*)l;check(info[0]==0,9410);if(mode==8){check(w==target&&info[1]==previous&&GetActiveWindow()==previous,9411);return 1;}}
 if(code==9&&mode==9){check(w==target&&l==previous&&GetFocus()==previous,9412);return 1;}
 if(code==1&&mode==10){check(w==target&&l==3,9413);return 1;}
 return CallNextHookEx(hook,code,w,l);
}
void run_cbt_hooks_test(void){
 U cls[10]={0,(U)proc,0,0,0,0,0,0,0,(U)"CbtPort"};check(RegisterClassA(cls),9414);
 older=SetWindowsHookExA(5,(U)tail,0,GetCurrentThreadId());hook=SetWindowsHookExA(5,(U)cbt,0,GetCurrentThreadId());check(older&&hook,9415);
 mode=1;U a=window();check(a&&nc==1&&created==1&&subcalls>=2&&seen[3]==1,9416);int r[4];check(GetWindowRect(a,r)&&r[0]==37&&r[1]==41&&r[2]-r[0]==190&&r[3]-r[1]==125,9417);
 mode=2;U before=ncdestroyed;check(!window()&&ncdestroyed==before+1&&created==1&&destroyed==0,9418);
 mode=3;U b=window();check(b&&created==2,9419);
 mode=4;U c=window();check(c&&nested&&created==4,9420);
 mode=7;before=destroyed;check(!DestroyWindow(c)&&IsWindow(c)&&destroyed==before,9421);
 mode=0;check(DestroyWindow(c)&&DestroyWindow(nested),9422);
 SetActiveWindow(a);SetFocus(a);previous=a;target=b;mode=8;SetActiveWindow(b);check(GetActiveWindow()==a,9423);
 mode=0;SetActiveWindow(b);check(GetActiveWindow()==b,9424);SetFocus(a);previous=GetFocus();target=b;mode=9;check(!SetFocus(b)&&GetFocus()==previous,9425);
 mode=0;SetFocus(b);check(GetFocus()==b,9426);
 target=b;mode=10;ShowWindow(b,3);check(!IsZoomed(b),9434);
 mode=0;ShowWindow(b,3);check(seen[1]&&IsZoomed(b),9427);
 mode=5;U d=window();check(d&&seen[3]>=4,9428);hook=SetWindowsHookExA(5,(U)cbt,0,GetCurrentThreadId());check(hook,9429);
 mode=6;before=seen[3];U e=window();check(e&&seen[3]==before&&!older,9430);
 mode=0;
 U v=CreateWindowExA(0,"CbtPort","default",0xc00000,(int)0x80000000,(int)0x80000000,(int)0x80000000,(int)0x80000000,0,0,0,0);
 check(v&&GetWindowRect(v,r)&&r[0]==64&&r[1]==55&&r[2]-r[0]==672,9435);check(DestroyWindow(v),9436);
 older=SetWindowsHookExA(5,(U)tail,0,GetCurrentThreadId());check(older&&UnhookWindowsHookEx(hook),9437);hook=SetWindowsHookExA(5,(U)cbt,0,GetCurrentThreadId());check(hook,9437);
 for(U i=0;i<260;++i){v=window();check(v&&DestroyWindow(v),9438);}
 check(UnhookWindowsHookEx(older),9439);
 family[0]=window();
 family[1]=CreateWindowExA(0,"CbtPort","child1",0x40000000,1,1,40,40,family[0],0,0,0);
 family[2]=CreateWindowExA(0,"CbtPort","grandchild",0x40000000,1,1,20,20,family[1],0,0,0);
 family[3]=CreateWindowExA(0,"CbtPort","child2",0x40000000,1,1,40,40,family[0],0,0,0);
 check(family[0]&&family[1]&&family[2]&&family[3],9442);mode=11;check(DestroyWindow(family[0])&&order_count==8,9443);
 {U expected[8]={0,1,2,3,10,9,11,8};for(U i=0;i<8;++i)check(order[i]==expected[i],9444);}
 mode=0;
 check(DestroyWindow(a)&&DestroyWindow(b)&&DestroyWindow(d)&&DestroyWindow(e),9431);check(UnhookWindowsHookEx(hook),9432);
}
#ifdef CBT_HOOKS_STANDALONE
void mainCRTStartup(void){run_cbt_hooks_test();ExitProcess(0);}
#endif
