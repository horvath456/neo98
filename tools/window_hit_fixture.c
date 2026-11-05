/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;typedef struct{int x,y;}Point;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U RegisterClassA(U*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);API U DestroyWindow(U);API U IsWindow(U);API U ShowWindow(U,U);API U EnableWindow(U,U);
API U SetWindowPos(U,U,int,int,int,int,U);API U SetWindowLongA(U,int,U);API U GetWindowLongA(U,int);API U SetWindowTextA(U,const char*);
API U WindowFromPoint(Point);API U ChildWindowFromPoint(U,Point);API U ChildWindowFromPointEx(U,Point,U);API U RealChildWindowFromPoint(U,Point);
API U GetMessageA(U*,U,U,U);API U TranslateMessage(U*);API U DispatchMessageA(U*);
static U parent,back,front,nested,transparent,mode,depth,calls,reentered,replacement,phase,downs,moves;
static void check(int ok,U code){if(!ok)ExitProcess(code);}
static U window(U p,int x,int y,int w,int h){return CreateWindowExA(0,"PointSearch98","point",p?0x50000000:0x90000000,x,y,w,h,p,0,0,0);}
static U __stdcall proc(U h,U m,U w,U l){
 if(m==0x84){++calls;check(!w,12900);check((short)l==30&&(short)(l>>16)==40,12901);
  if(h==front&&!depth){depth=1;if(mode==1){check(WindowFromPoint((Point){30,40})==front,12902);++reentered;}if(mode==2||mode==3){check(DestroyWindow(h),12903);replacement=window(parent,100,100,10,10);check(replacement&&replacement!=h,12904);}depth=0;}
  return h==transparent||mode==3&&h==front?~0u:1;
 }
#ifdef WINDOW_HIT_HOST_INPUT
 if(phase==1&&(m==0x200||m==0x201)){
  /* Query inside the real mouse dispatch; no direct host invocation of guest callbacks. */
  check(WindowFromPoint((Point){30,40})==back,12905);if(m==0x200)++moves;if(m==0x201){++downs;phase=2;}
 }
#endif
 return DefWindowProcA(h,m,w,l);
}
void run_window_hit_test(void){
 U cls[10]={0,(U)proc,0,0,0,0,0,0,0,(U)"PointSearch98"};check(RegisterClassA(cls),12906);
 parent=window(0,20,30,200,150);back=window(parent,5,5,80,80);front=window(parent,5,5,80,80);nested=window(front,2,2,20,20);check(parent&&back&&front&&nested,12907);SetWindowPos(front,0,0,0,0,0,0x13);
 check(ChildWindowFromPoint(parent,(Point){10,10})==front&&ChildWindowFromPointEx(parent,(Point){10,10},7)==front,12908);
 check(WindowFromPoint((Point){30,40})==nested,12909);check(!WindowFromPoint((Point){-1,-1}),12910);
 check(!ChildWindowFromPoint(parent,(Point){200,0})&&!RealChildWindowFromPoint(parent,(Point){-1,0}),12911);
 transparent=nested;check(WindowFromPoint((Point){30,40})==front,12912);transparent=0;
 EnableWindow(front,0);check(ChildWindowFromPoint(parent,(Point){10,10})==front&&ChildWindowFromPointEx(parent,(Point){10,10},2)==back,12913);check(WindowFromPoint((Point){30,40})==back,12914);EnableWindow(front,1);
 ShowWindow(front,0);check(ChildWindowFromPoint(parent,(Point){10,10})==front&&ChildWindowFromPointEx(parent,(Point){10,10},1)==back&&RealChildWindowFromPoint(parent,(Point){10,10})==back,12915);ShowWindow(front,8);
 SetWindowLongA(front,-20,0x20);check(ChildWindowFromPointEx(parent,(Point){10,10},4)==back&&WindowFromPoint((Point){30,40})==nested,12916);SetWindowLongA(front,-20,0);
 check(!ChildWindowFromPointEx(parent,(Point){10,10},8)&&!RealChildWindowFromPoint(0,(Point){10,10}),12917);
 U group=CreateWindowExA(0,"BUTTON","group",0x50000007,5,5,80,80,parent,0,0,0);check(group,12918);SetWindowPos(group,0,0,0,0,0,0x13);
 check(ChildWindowFromPoint(parent,(Point){10,10})==group&&RealChildWindowFromPoint(parent,(Point){10,10})==front,12919);
 ShowWindow(front,0);ShowWindow(back,0);check(RealChildWindowFromPoint(parent,(Point){10,10})==group,12920);DestroyWindow(group);ShowWindow(front,8);ShowWindow(back,8);SetWindowPos(front,0,0,0,0,0,0x13);
 U label=CreateWindowExA(0,"STATIC","label",0x50000000,5,5,80,80,parent,0,0,0);check(label,12921);SetWindowPos(label,0,0,0,0,0,0x13);
 check(WindowFromPoint((Point){30,40})==nested,12922);SetWindowLongA(label,-16,0x50000100);check(WindowFromPoint((Point){30,40})==label,12923);DestroyWindow(label);
 DestroyWindow(nested);nested=0;mode=1;check(WindowFromPoint((Point){30,40})==front&&reentered==1,12924);mode=0;
 U before=calls;EnableWindow(parent,0);check(WindowFromPoint((Point){30,40})==parent&&calls==before,12925);EnableWindow(parent,1);
 mode=2;check(!WindowFromPoint((Point){30,40})&&!IsWindow(front),12926);mode=0;DestroyWindow(replacement);front=window(parent,5,5,80,80);SetWindowPos(front,0,0,0,0,0,0x13);
 mode=3;check(WindowFromPoint((Point){30,40})==back&&!IsWindow(front),12927);mode=0;DestroyWindow(replacement);
#ifdef WINDOW_HIT_HOST_INPUT
 phase=1;SetWindowTextA(parent,"point input ready");U msg[7];while(phase!=2&&GetMessageA(msg,0,0,0)>0){TranslateMessage(msg);DispatchMessageA(msg);}check(moves>=1&&downs==1,12928);
#endif
 check(DestroyWindow(parent),12929);
}
#ifdef WINDOW_HIT_STANDALONE
void mainCRTStartup(void){run_window_hit_test();ExitProcess(0);}
#endif
