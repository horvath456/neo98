/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U RegisterClassA(U*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);API U DestroyWindow(U);API U IsWindow(U);API U ShowWindow(U,U);API U GetClientRect(U,int*);API U GetWindowLongA(U,int);
API U SetScrollInfo(U,int,U*,U);API U GetScrollInfo(U,int,U*);API U SetScrollPos(U,int,int,U);API U GetScrollPos(U,int);
API U SetScrollRange(U,int,int,int,U);API U GetScrollRange(U,int,int*,int*);API U ShowScrollBar(U,int,U);API U EnableScrollBar(U,int,U);
static void check(int ok,U code){if(!ok)ExitProcess(code);}
static U control,events,bar_window;
static U __stdcall proc(U h,U m,U w,U l){
 if(h==control&&m==0xea){U*i=(U*)l;++events;check(w==0,9630);if(i[1]&1){i[2]=0xffff0000;i[3]=0x7fffffff;}if(i[1]&4)i[5]=0x12345678;return 0;}
 if(h==control&&m==0xe9){U*i=(U*)l;++events;check(i[0]==28,9631);if(i[1]==0x1004){check(i[5]==0x12345678&&w==1,9632);check(GetScrollPos(bar_window,0)==70000,9633);}return 0x87654321;}
 if(h==control&&m==0xe1){++events;return 0x99887766;}
 return DefWindowProcA(h,m,w,l);
}
void run_scroll_family_test(void){
 U cls[10]={0,(U)proc,0,0,0,0,0,0,0,(U)"ScrollPort"};check(RegisterClassA(cls),9600);
 U h=CreateWindowExA(0,"ScrollPort","Original USER scrollbars",0x10cf0000,35,40,320,240,0,0,0,0);check(h,9601);bar_window=h;
 U i[7]={28,0x17,11,12,13,14,15};check(!GetScrollInfo(h,0,i)&&i[2]==11&&i[6]==15,9602);
 int low=9,high=9;check(!GetScrollRange(h,0,&low,&high)&&!low&&!high,9603);
 check(!SetScrollPos(h,0,73,1)&&!GetScrollPos(h,0),9604);
 int oldrect[4],rect[4];check(GetClientRect(h,oldrect),9605);
 check(SetScrollRange(h,0,-70000,170000,1),9606);check(GetWindowLongA(h,-16)&0x100000,9607);
 check(GetClientRect(h,rect)&&rect[3]<oldrect[3]&&rect[2]==oldrect[2],9608);
 check(!SetScrollPos(h,0,70000,1)&&GetScrollPos(h,0)==70000,9609);
 check(GetScrollRange(h,0,&low,&high)&&low==-70000&&high==170000,9610);
 i[0]=24;i[1]=0x17;check(GetScrollInfo(h,0,i)&&i[5]==70000&&i[6]==70000,9611);
 i[0]=28;i[1]=7;i[2]=0;i[3]=99;i[4]=25;i[5]=100;check(SetScrollInfo(h,1,i,1)==75,9612);
 i[1]=0x17;check(GetScrollInfo(h,1,i)&&i[4]==25&&i[5]==75&&i[6]==75,9613);
 check(GetScrollPos(h,3)==70000,9614); /* query axis3 is horizontal */
 check(SetScrollPos(h,3,31,0)==75&&GetScrollPos(h,1)==31,9615); /* set axis3 is vertical */
 check(!SetScrollRange(h,3,0,1,0)&&!SetScrollRange(h,0x10000,0,1,0),9616);
 check(GetScrollPos(h,0x10000)==70000,9617); /* other wrappers truncate bar */
 check(EnableScrollBar(h,0,1)&&!EnableScrollBar(h,0,1)&&EnableScrollBar(h,0,2)&&!EnableScrollBar(h,0,3),9618);
 check(EnableScrollBar(h,3,0)&&!EnableScrollBar(h,3,0)&&!EnableScrollBar(h,0,4),9619);
 i[1]=15;i[2]=i[3]=i[4]=i[5]=0;SetScrollInfo(h,1,i,1);check(GetWindowLongA(h,-16)&0x200000,9620);
 i[1]=1;SetScrollInfo(h,1,i,1);/* unchanged data preserves original old visibility */
 check(ShowScrollBar(h,3,0)&&!(GetWindowLongA(h,-16)&0x300000),9621);
 check(ShowScrollBar(h,3,1)&&(GetWindowLongA(h,-16)&0x300000)==0x300000,9622);
 i[0]=20;i[1]=7;check(!SetScrollInfo(h,0,i,0)&&!GetScrollInfo(h,0,i),9623);
 i[0]=28;i[1]=0x8000;check(!GetScrollInfo(h,0,i)&&!SetScrollInfo(h,0,i,0),9624);
 check(!GetScrollInfo(h,0,(U*)0)&&!SetScrollInfo(h,0,(U*)1,0),9625);
 check(!ShowScrollBar(h,4,1)&&!EnableScrollBar(h,-1,0),9626);
 control=CreateWindowExA(0,"ScrollPort","SB_CTL provider",0,0,0,30,30,0,0,0,0);check(control,9627);
 check(GetScrollPos(control,2)==0x99887766,9628);i[0]=28;i[1]=0x17;check(GetScrollInfo(control,2,i)&&i[5]==0x12345678,9629);
 check(SetScrollPos(control,2,0x12345678,1)==0x87654321,9634);
 check(GetScrollRange(control,2,&low,&high)&&low==-65536&&high==0x7fffffff,9635);
 check(SetScrollRange(control,2,0,70000,1)&&events==5,9636);
 check(ShowScrollBar(control,2,1)&&ShowScrollBar(control,2,0),9637);
 check(DestroyWindow(control)&&DestroyWindow(h),9638);
 for(U n=0;n<220;++n){h=CreateWindowExA(0,"ScrollPort","lifetime",0,0,0,80,80,0,0,0,0);check(h,9639);i[0]=28;i[1]=0x17;i[2]=13;check(!GetScrollInfo(h,0,i)&&i[2]==13,9640);check(ShowScrollBar(h,3,1),9641);check(GetScrollInfo(h,0,i)&&i[2]==0&&i[3]==100&&i[4]==0&&i[5]==0,9642);check(DestroyWindow(h),9643);}
}
#ifdef SCROLL_FAMILY_STANDALONE
void mainCRTStartup(void){run_scroll_family_test();ExitProcess(0);}
#endif
