// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetModuleHandleA(const char*);
API U InitCommonControlsEx(const U*);API U RegisterClassA(const U*);
API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);API U DestroyWindow(U);API U SendMessageA(U,U,U,U);
API U SetWindowTextA(U,const char*);API U SetFocus(U);API U GetMessageA(U*,U,U,U);
API U TranslateMessage(U*);API U DispatchMessageA(U*);
struct Item {U mask,state,stateMask,text,capacity,image,data,extra;};
static U owner,tab,changing,changed,keys,phase;
static void check(U yes,U code){if(!yes)ExitProcess(code);}
static U send(U message,U wp,U lp){return SendMessageA(tab,message,wp,lp);}
static U __stdcall procedure(U hwnd,U message,U wp,U lp){
 if(message==0x4e){U*n=(U*)lp;if(n[0]!=tab)return 0;check(n[1]==42&&wp==42,24800);
  if(n[2]==0xfffffdd8){++changing;if(phase==1&&changing==1){check(send(0x130b,0,0)==0,24801);SetWindowTextA(owner,"tabs veto");return 1;}}
  if(n[2]==0xfffffdd9){++changed;if(phase==1&&changed==1){check(send(0x130b,0,0)==1,24802);SetWindowTextA(owner,"tabs mouse");}else if(phase==1&&changed==2){check(keys==1&&send(0x130b,0,0)==2,24803);SetWindowTextA(owner,"tabs keyboard");phase=2;}}
  if(n[2]==0xfffffdda){++keys;check(*(W*)((char*)n+12)==0x27,24804);}
  return 0;
 }return DefWindowProcA(hwnd,message,wp,lp);
}
static void insert(U index,const char*text,U expected){struct Item item={27,2,2,(U)text,0,0xffffffff,0x12345678,0xabcdef01};check(send(0x1307,index,(U)&item)==expected,24810);}
void mainCRTStartup(void){
 U instance=GetModuleHandleA(0),common[]={8,8};check(InitCommonControlsEx(common),24811);
 U cls[]={0,(U)procedure,0,0,instance,0,0,16,0,(U)"TabPort98"};check(RegisterClassA(cls),24812);
 owner=CreateWindowExA(0,"TabPort98","tab fixture",0x10cf0000,70,70,600,340,0,0,instance,0);check(owner,24813);
 tab=CreateWindowExA(0,"SysTabControl32","",0x50010000,12,12,550,230,owner,42,instance,0);check(tab,24814);
 check(send(0x1304,0,0)==0&&send(0x130b,0,0)==0xffffffff,24815);check(send(0x130e,8,0),24816);
 insert(0,"&General",0);check(send(0x130b,0,0)==0,24817);insert(99,"Advanced",1);insert(1,"Display",1);
 check(send(0x1304,0,0)==3&&!send(0x130e,4,0),24818);
 char text[32];for(U i=0;i<32;++i)text[i]=(char)0xa5;struct Item item={27,0,3,(U)text,32,0,0,0};
 check(send(0x1305,0,(U)&item)&&item.data==0x12345678&&item.extra==0xabcdef01&&item.state==1,24819);
 const char*expected="&General";for(U i=0;i<9;++i)check(text[i]==expected[i],24820);check((unsigned char)text[9]==0xa5,24821);
 W wide[32];item.mask=1;item.text=(U)wide;item.capacity=32;check(send(0x133c,2,(U)&item),24822);expected="Advanced";for(U i=0;i<9;++i)check(wide[i]==(W)expected[i],24823);
 static const W display[]={'D','i','s','p','l','a','y',0};item.text=(U)display;check(send(0x133d,1,(U)&item),24824);
 item.mask=0;check(send(0x1306,999,(U)&item),24825);
 item.mask=8;item.data=item.extra=0xa5a5a5a5;check(!send(0x133c,999,(U)&item)&&!item.data&&item.extra==0xa5a5a5a5,24826);
 check(send(0x130c,2,0)==0&&send(0x132f,0,0)==2&&!changing&&!changed,24827);
 check(send(0x1308,1,0)&&send(0x130b,0,0)==1,24828);check(send(0x1308,1,0)&&send(0x130b,0,0)==0xffffffff,24829);
 check(send(0x1309,0,0)&&!send(0x1304,0,0),24830);insert(0,"&General",0);insert(1,"Display",1);insert(2,"Advanced",2);
 int rect[]={0,0,400,200};send(0x1328,0,(U)rect);check(rect[0]>0&&rect[1]>10&&rect[2]<400&&rect[3]<200,24831);send(0x1328,1,(U)rect);check(rect[0]==0&&rect[1]==0&&rect[2]==400&&rect[3]==200,24832);
 check(send(0x132c,0,0)==1,24833);
 for(U i=0;i<3;++i){check(send(0x130a,i,(U)rect)&&rect[2]>rect[0]&&rect[3]>rect[1],24834);int hit[]={(rect[0]+rect[2])/2,(rect[1]+rect[3])/2,0};check(send(0x130d,0,(U)hit)==i&&(hit[2]==4||hit[2]==6),24835);
  // Hidden fixture markers expose actual TCM_GETITEMRECT results to the host
  // input driver without allowing it to call APIs on a suspended guest stack.
  check(CreateWindowExA(0,"STATIC","fixture tab hit",0x40000000,rect[0],rect[1],rect[2]-rect[0],rect[3]-rect[1],tab,800+i,instance,0),24836);
 }
 phase=1;SetFocus(tab);SetWindowTextA(owner,"tabs ready");U msg[7];while(phase==1&&GetMessageA(msg,0,0,0)){TranslateMessage(msg);DispatchMessageA(msg);}
 check(phase==2&&changing==3&&changed==2&&keys==1,24837);
 phase=3;send(0x1330,0,0);check(send(0x130b,0,0)==0&&changing==4&&changed==3,24838);
 check(DestroyWindow(owner),24839);ExitProcess(0);
}
