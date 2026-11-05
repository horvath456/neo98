/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetCurrentThreadId(void);API U GetLastError(void);API void Sleep(U);
API U RegisterClassA(U*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);API U DestroyWindow(U);API U IsWindow(U);API U ShowWindow(U,U);
API U SetParent(U,U);API U GetParent(U);API U GetDesktopWindow(void);API U BringWindowToTop(U);
API U GetTopWindow(U);API U GetWindow(U,U);API U GetWindowRect(U,int*);API U ScreenToClient(U,int*);
API U GetWindowLongA(U,int);API U SetWindowLongA(U,int,U);API U SetActiveWindow(U);API U GetActiveWindow(void);
API U SetWindowPos(U,U,int,int,int,int,U);API U GetClientRect(U,int*);
API U SetWindowsHookExA(int,U,U,U);API U UnhookWindowsHookEx(U);API U CallNextHookEx(U,int,U,U);
static U a,b,child,sibling,hook,mode,depth,target,replacement,sequence[32],count,changing,changed,childactive,hide_count,show_count,sized,moved,size_w,size_h;
static void check(int ok,U code){if(!ok)ExitProcess(code);}
static U window(U style,U parent,int x,int y){return CreateWindowExA(0,"WindowParent98","window",style,x,y,150,90,parent,0,0,0);}
static void event(U code){if(count<32)sequence[count++]=code;}
static U __stdcall proc(U h,U m,U w,U l){
 if(h==target){
  if(m==0x46){++changing;event(1);U*p=(U*)l;check(p[0]==h,10500);
   if(mode==1){p[2]=44;p[3]=55;p[4]=120;p[5]=70;p[6]&=~3u;}
   if(mode==3&&!depth){depth=1;check(DestroyWindow(h),10501);replacement=window(0,0,6,7);depth=0;}
   if(mode==4&&!depth){depth=1;check(BringWindowToTop(b),10502);depth=0;check(p[0]==h,10503);}
   if(mode==12)Sleep(1);
  }
  if(m==0x47){++changed;event(3);U*p=(U*)l;check(p[0]==h,10504);}
  if(m==5){++sized;size_w=l&65535;size_h=(l>>16)&65535;}
  if(m==3)++moved;
  if(m==0x22){++childactive;event(4);}
  if(m==0x18){if(w)++show_count;else ++hide_count;
   if(!w&&mode==5&&!depth){depth=1;check(SetParent(h,b)==a,10505);depth=0;}
   if(!w&&mode==6&&!depth){depth=1;check(DestroyWindow(h),10506);replacement=window(0,0,11,12);depth=0;}
   if(!w&&mode==7&&!depth){depth=1;check(DestroyWindow(b),10507);depth=0;}
  }
 }
 return DefWindowProcA(h,m,w,l);
}
static U __stdcall cbt(int code,U h,U l){if(code==5&&h==target){event(2);if(mode==2)return 1;if(mode==8&&!depth){depth=1;check(DestroyWindow(h),10508);replacement=window(0,0,15,16);depth=0;}}return CallNextHookEx(hook,code,h,l);}
static void reset(U h,U m){target=h;mode=m;count=changing=changed=childactive=hide_count=show_count=sized=moved=size_w=size_h=0;}
static void dispose(U h){if(IsWindow(h))check(DestroyWindow(h),10509);}
void run_window_parent_test(void){
 U cls[10]={0,(U)proc,0,0,0,0,0,0,0,(U)"WindowParent98"};check(RegisterClassA(cls),10510);
 hook=SetWindowsHookExA(5,(U)cbt,0,GetCurrentThreadId());check(hook,10511);
 a=window(0,0,30,40);b=window(0,0,300,200);check(a&&b,10512);ShowWindow(a,8);ShowWindow(b,8);SetActiveWindow(b);
 reset(a,0);check(BringWindowToTop(a)&&GetTopWindow(0)==a&&GetActiveWindow()==a,10513);check(changing==1&&changed==1&&count>=3&&sequence[0]==1&&sequence[count-1]==3,10514);
 mode=0;SetActiveWindow(b);reset(a,2);check(BringWindowToTop(a)&&GetActiveWindow()==b&&changed==1,10515);
 reset(a,1);check(BringWindowToTop(a),10516);int r[4];check(GetWindowRect(a,r)&&r[0]==44&&r[1]==55&&r[2]-r[0]==120&&r[3]-r[1]==70,10517);
 reset(a,4);check(BringWindowToTop(a)&&GetActiveWindow()==a&&changed==1,10518);
 child=window(0x40000000,a,9,10);sibling=window(0x40000000,a,20,30);check(child&&sibling,10519);
 // Creation can leave the first child on top. Make this a real Z-order change.
 check(SetWindowPos(child,1,0,0,0,0,0x13)&&GetTopWindow(a)==sibling,10558);
 reset(child,0);check(BringWindowToTop(child)&&GetTopWindow(a)==child&&childactive==1&&changed==1,10520);
 reset(child,0);check(SetParent(child,b)==a&&GetParent(child)==b&&GetTopWindow(b)==child,10521);check(!(GetWindowLongA(child,-16)&0x10000000)&&!hide_count&&!show_count,10522);
 check(GetWindowRect(child,r)&&ScreenToClient(b,r)&&r[0]==9&&r[1]==10,10523);
 check(SetParent(child,b)==b&&GetParent(child)==b,10524);
 check(SetParent(child,0)==b&&GetParent(child)==GetDesktopWindow()&&(GetWindowLongA(child,-16)&0x44000000)==0x44000000,10525);
 check(SetParent(child,a)==GetDesktopWindow()&&GetParent(child)==a,10526);
 ShowWindow(child,8);reset(child,0);check(SetParent(child,b)==a&&hide_count==1&&show_count==1&&(GetWindowLongA(child,-16)&0x10000000),10527);
 check(SetParent(child,a)==b,10528);reset(child,5);check(!SetParent(child,b)&&GetParent(child)==b,10529);mode=0;
 check(!SetParent(a,sibling)&&GetLastError()==87,10530);
 check(!SetParent(child,0xfffffffdu)&&GetLastError()==120,10531);
 check(!SetParent(child,0x12345678)&&GetLastError()==1400,10532);
 check(!BringWindowToTop(0x12345678)&&GetLastError()==1400,10533);
 ShowWindow(child,8);reset(child,6);check(!SetParent(child,a)&&!IsWindow(child)&&IsWindow(replacement),10534);mode=0;dispose(replacement);
 child=window(0x50000000,a,3,4);reset(child,7);check(!SetParent(child,b)&&IsWindow(child)&&!IsWindow(b),10535);mode=0;dispose(child);
 b=window(0,0,300,200);target=0;ShowWindow(b,8);SetActiveWindow(b);U doomed=window(0,0,8,9);reset(doomed,3);check(BringWindowToTop(doomed)&&!IsWindow(doomed)&&IsWindow(replacement)&&!changed,10536);mode=0;dispose(replacement);
 doomed=window(0,0,8,9);reset(doomed,8);check(BringWindowToTop(doomed)&&!IsWindow(doomed)&&IsWindow(replacement)&&!changed,10537);mode=0;dispose(replacement);
 // A visible child can be destroyed by WINDOWPOSCHANGING during the initial hide.
 doomed=window(0x50000000,a,8,9);reset(doomed,3);check(!SetParent(doomed,b)&&!IsWindow(doomed)&&IsWindow(replacement),10544);mode=0;dispose(replacement);
 // A hidden child reaches the positioning callback after the parent changes.
 doomed=window(0x40000000,a,8,9);reset(doomed,3);check(SetParent(doomed,b)==a&&!IsWindow(doomed)&&IsWindow(replacement)&&!changed,10543);mode=0;dispose(replacement);
 reset(a,12);check(BringWindowToTop(a)&&changed==1,10538);mode=0;
 U owner=CreateWindowExA(8,"WindowParent98","topmost owner",0,4,5,80,70,0,0,0,0);U owned=window(0,owner,7,8);check(owner&&owned,10541);
 SetWindowLongA(owned,-16,0x40000000);check(SetParent(owned,b)==GetDesktopWindow()&&(GetWindowLongA(owned,-20)&8),10542);dispose(owned);dispose(owner);
 for(U i=0;i<210;++i){U h=window(0x40000000,a,2,3);check(h&&SetParent(h,b)==a&&SetParent(h,0)==b&&DestroyWindow(h),10539);}

 // The WM_WINDOWPOSCHANGED handler reads
 // only SWP_NOCLIENTMOVE and SWP_NOCLIENTSIZE. A SetWindowPos that keeps
 // position and size but changes the frame therefore still reports the new
 // client area, while a request that changes nothing reports nothing.
 // POPUP keeps the initial window frameless; overlapped windows gain a caption.
 U framed=window(0x84000000,0,40,50);check(framed,10545);int c0[4];check(GetClientRect(framed,c0),10546);
 SetWindowLongA(framed,-16,0x84800000);
 reset(framed,0);check(SetWindowPos(framed,0,0,0,0,0,0x37),10547);
 int c1[4];check(GetClientRect(framed,c1),10548);
 check(c1[2]<c0[2]&&c1[3]<c0[3],10549);
 check(changed==1&&sized==1&&size_w==(U)c1[2]&&size_h==(U)c1[3],10550);
 reset(framed,0);check(SetWindowPos(framed,0,0,0,0,0,0x17),10551);
 check(!changed&&!sized&&!moved,10552);
 dispose(framed);
 // CreateWindowEx accepts signed negative dimensions, creating an invisible
 // window. Only CW_USEDEFAULT requests replacement. WS_POPUP avoids
 // overlapped-window minimum tracking dimensions.
 U tiny=CreateWindowExA(0,"WindowParent98","negative",0x80000000,1,1,-2,-2,0,0,0,0);check(tiny&&IsWindow(tiny),10553);
 check(GetWindowRect(tiny,r)&&r[0]==1&&r[1]==1&&r[2]-r[0]==-2&&r[3]-r[1]==-2,10554);
 int tc[4];check(GetClientRect(tiny,tc)&&tc[2]<=0&&tc[3]<=0,10555);
 ShowWindow(tiny,5);check(IsWindow(tiny),10556);dispose(tiny);
 U wide=CreateWindowExA(0,"WindowParent98","child",0x40000000,0,0,-1,-1,a,0,0,0);check(wide&&IsWindow(wide)&&GetParent(wide)==a,10557);dispose(wide);
 target=0;dispose(a);dispose(b);check(UnhookWindowsHookEx(hook),10540);
}
#ifdef WINDOW_PARENT_STANDALONE
void mainCRTStartup(void){run_window_parent_test();ExitProcess(0);}
#endif
