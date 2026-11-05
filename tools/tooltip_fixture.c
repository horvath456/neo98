// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetModuleHandleA(const char*);API U InitCommonControlsEx(const U*);API U RegisterClassA(const U*);
API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);API U DefWindowProcA(U,U,U,U);API U DestroyWindow(U);API U SendMessageA(U,U,U,U);
API U SetWindowTextA(U,const char*);API U SetFocus(U);API U GetMessageA(U*,U,U,U);API U TranslateMessage(U*);API U DispatchMessageA(U*);
struct Tool {U size,flags,hwnd,id;int left,top,right,bottom;U instance,text,data;};
static U owner,tip,second,shows,pops,requests,phase;
static void check(U yes,U error){if(!yes)ExitProcess(error);}
static U send(U m,U a,U b){return SendMessageA(tip,m,a,b);}
static U __stdcall procedure(U h,U m,U w,U l){
 if(m==0x55)return 1;
 if(m==0x4e&&((U*)l)[0]==tip){U*n=(U*)l;check(n[1]==w,30800);
  if(n[2]==0xfffffdee){check(n[1]==second&&n[46]==0xbeef,30801);++requests;W*out=(W*)(l+16);const char*s="Callback tooltip";for(U i=0;i<17;++i)out[i]=s[i];n[45]|=0x8000;return 0;}
  if(n[2]==0xfffffdf7){++shows;check((shows==1&&n[1]==10)||(shows==2&&n[1]==second),30802);SetWindowTextA(owner,shows==1?"tooltip first":"tooltip second");return 0;}
  if(n[2]==0xfffffdf6){++pops;return 0;}
 }
 if(m==0x100&&w==13&&shows==2){phase=2;return 0;}
 return DefWindowProcA(h,m,w,l);
}
void mainCRTStartup(void){
 U instance=GetModuleHandleA(0),common[]={8,4};check(InitCommonControlsEx(common),30803);U cls[]={0,(U)procedure,0,0,instance,0,0,16,0,(U)"TooltipPort98"};check(RegisterClassA(cls),30804);
 owner=CreateWindowExA(0,"TooltipPort98","tooltip fixture",0x10cf0000,80,80,480,260,0,0,instance,0);check(owner,30805);second=CreateWindowExA(0,"STATIC","Hover callback",0x50000000,230,40,160,36,owner,42,instance,0);check(second,30806);
 tip=CreateWindowExA(0,"tooltips_class32","",0x80000001,0,0,0,0,owner,0,instance,0);check(tip&&!send(0x40d,0,0),30807);
 struct Tool a={40,16,owner,10,20,40,200,90,instance,(U)"Plain &tip",0};check(send(0x404,0,(U)&a),30808);
 struct Tool b={44,0x51,owner,second,0,0,0,0,instance,0xffffffff,0xbeef};check(send(0x432,0,(U)&b)&&send(0x40d,0,0)==2,30809);
 char text[80];for(U i=0;i<80;++i)text[i]=(char)0xa5;struct Tool query=a;query.text=(U)text;check(send(0x408,0,(U)&query),30810);const char*plain="Plain &tip";for(U i=0;i<11;++i)check(text[i]==plain[i],30811);check((unsigned char)text[11]==0xa5,30812);
 send(0x40b,0,(U)&query);plain="Plain tip";for(U i=0;i<10;++i)check(text[i]==plain[i],30813);
 struct Tool extra={44,0,owner,99,0,0,5,5,instance,17,0xcafe};check(send(0x432,0,(U)&extra)&&send(0x40d,0,0)==3,30814);query=extra;query.size=44;check(send(0x435,0,(U)&query)&&query.text==17&&query.data==0xcafe,30815);send(0x433,0,(U)&extra);check(send(0x40d,0,0)==2,30816);
 send(0x403,0,100);check(send(0x415,3,0)==100&&send(0x415,1,0)==20&&send(0x415,2,0)==1000,30817);send(0x403,2,30000);check(send(0x415,2,0)==30000&&send(0x415,99,0)==0xffffffff,30818);
 check(send(0x418,0,180)==0xffffffff&&send(0x419,0,0)==180,30819);int margins[]={2,2,3,3},out[4]={};send(0x41a,0,(U)margins);send(0x41b,0,(U)out);for(U i=0;i<4;++i)check(out[i]==margins[i],30820);
 phase=1;SetFocus(owner);SetWindowTextA(owner,"tooltip ready");U message[7];while(phase==1&&GetMessageA(message,0,0,0)){TranslateMessage(message);DispatchMessageA(message);}
 check(shows==2&&pops==1&&requests==1,30821);W wide[80];query=b;query.text=(U)wide;check(send(0x43b,0,(U)&query)&&query.id==second&&query.data==0xbeef,30822);const char*expected="Callback tooltip";for(U i=0;i<17;++i)check(wide[i]==(W)expected[i],30823);send(0x433,0,(U)&b);check(pops==2&&!send(0x43b,0,0)&&send(0x40d,0,0)==1,30824);check(DestroyWindow(tip),30825);tip=0;check(DestroyWindow(owner),30826);ExitProcess(0);
}
