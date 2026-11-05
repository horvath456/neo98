/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API void Sleep(U);API U GetCurrentThreadId(void);
API U RegisterClassA(U*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);API U DestroyWindow(U);API U IsWindow(U);API U ShowWindow(U,U);
API U SetCapture(U);API U GetCapture(void);API U ReleaseCapture(void);API U SendMessageA(U,U,U,U);
API U SetWindowTextA(U,const char*);API U GetMessageA(U*,U,U,U);API U TranslateMessage(U*);API U DispatchMessageA(U*);API U PostMessageA(U,U,U,U);
API U CreatePopupMenu(void);API U AppendMenuA(U,U,U,const char*);API U TrackPopupMenu(U,U,int,int,int,U,U);API U EndMenu(void);API U DestroyMenu(U);
static U a,b,c,mode,depth,target,notices,last_new,menu,phase,downs,ups,moves,held_before,nested_result;
static void check(int ok,U code){if(!ok)ExitProcess(code);}
static U window(int x,int y){return CreateWindowExA(0,"MouseCapture98","capture",0,x,y,140,90,0,0,0,0);}
static U __stdcall proc(U h,U m,U w,U l){
 if(m==0x215){++notices;last_new=l;check(!w&&GetCapture()==l,11300);
  if(h==target&&!depth){depth=1;
   if(mode==1)nested_result=SetCapture(c);
   if(mode==2)check(ReleaseCapture(),11301);
   if(mode==3)check(DestroyWindow(h),11302);
   if(mode==4)Sleep(1);
   depth=0;
  }
 }
 if(m==0x116&&mode==5){check(GetCapture()==h,11303);check(!SetCapture(b)&&GetCapture()==h,11304);check(!ReleaseCapture()&&GetCapture()==h,11305);check(EndMenu(),11306);}
 if(m==0x116&&mode==6){check(GetCapture()==h,11307);check(SendMessageA(h,0x1f,0,0)==0&&!GetCapture(),11308);}
 if(m==0x116&&mode==7){check(DestroyWindow(h)&&!GetCapture(),11309);}
 if(phase==1&&h==a&&(m==0x200||m==0x201||m==0x202)){check(GetCapture()==a,11310);if(m==0x202)check((short)l>400&&(short)(l>>16)>300,11311);if(m==0x200)++moves;if(m==0x201)++downs;if(m==0x202){++ups;check(GetCapture()==a,11312);phase=2;SetWindowTextA(a,"capture mouse passed");}}
 if(m==0x8010){phase=3;return 0;}
 return DefWindowProcA(h,m,w,l);
}
void run_mouse_capture_test(void){
 U cls[10]={0,(U)proc,0,0,0,0,0,0,0,(U)"MouseCapture98"};check(RegisterClassA(cls),11313);a=window(20,30);b=window(300,200);c=window(400,100);check(a&&b&&c,11314);ShowWindow(a,8);ShowWindow(b,8);ShowWindow(c,8);
 check(!GetCapture()&&ReleaseCapture(),11315);check(!SetCapture(a)&&GetCapture()==a,11316);U before=notices;check(SetCapture(a)==a&&notices==before+1&&last_new==a,11317);check(SetCapture(b)==a&&GetCapture()==b&&last_new==b,11318);
 check(SetCapture(0)==b&&!GetCapture()&&!last_new,11319);check(!SetCapture(0)&&ReleaseCapture(),11320);
 check(!SetCapture(0x12345678)&&!GetCapture(),11321);
 SetCapture(a);target=a;mode=1;before=notices;check(SetCapture(b)==a&&nested_result==b&&GetCapture()==c&&notices==before+2,11322);mode=0;ReleaseCapture();
 SetCapture(a);mode=2;check(SetCapture(b)==a&&!GetCapture(),11323);mode=0;
 SetCapture(a);mode=4;check(SetCapture(b)==a&&GetCapture()==b,11324);mode=0;ReleaseCapture();
 SetCapture(a);before=notices;check(DestroyWindow(a)&&!GetCapture()&&notices==before,11325);a=window(20,30);ShowWindow(a,8);target=a;SetCapture(a);mode=3;check(SetCapture(b)==a&&!IsWindow(a)&&GetCapture()==b,11326);mode=0;ReleaseCapture();a=window(20,30);ShowWindow(a,8);
 SetCapture(a);check(!SendMessageA(b,0x1f,0,0)&&GetCapture()==a,11327);check(!SendMessageA(a,0x1f,0,0)&&!GetCapture(),11328);
 menu=CreatePopupMenu();check(menu&&AppendMenuA(menu,0,17,"capture item"),11329);
 mode=5;check(!TrackPopupMenu(menu,0x100,20,120,0,a,0)&&!GetCapture(),11330);
 mode=6;check(!TrackPopupMenu(menu,0x100,20,120,0,a,0)&&!GetCapture(),11331);
 U owner=window(50,50);mode=7;check(!TrackPopupMenu(menu,0x100,20,120,0,owner,0)&&!IsWindow(owner)&&!GetCapture(),11332);mode=0;
 check(DestroyMenu(menu),11333);
 for(U i=0;i<220;++i){U h=window(2,3);check(h&&!SetCapture(h)&&GetCapture()==h&&DestroyWindow(h)&&!GetCapture(),11334);}
#ifdef MOUSE_CAPTURE_HOST_INPUT
 phase=1;check(!SetCapture(a),11335);SetWindowTextA(a,"capture mouse ready");U msg[7];while(phase!=3&&GetMessageA(msg,0,0,0)>0){TranslateMessage(msg);DispatchMessageA(msg);}check(downs==1&&ups==1&&moves>=1&&GetCapture()==a,11336);ReleaseCapture();
#endif
 check(DestroyWindow(a)&&DestroyWindow(b)&&DestroyWindow(c),11337);
}
#ifdef MOUSE_CAPTURE_STANDALONE
void mainCRTStartup(void){run_mouse_capture_test();ExitProcess(0);}
#endif
