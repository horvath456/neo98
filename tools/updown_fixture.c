// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetModuleHandleA(const char*);API U InitCommonControlsEx(const U*);API U RegisterClassA(const U*);
API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);API U DefWindowProcA(U,U,U,U);API U DestroyWindow(U);API U SendMessageA(U,U,U,U);
API U SetWindowTextA(U,const char*);API U GetWindowTextA(U,char*,U);API U GetWindowLongA(U,int);API U SetFocus(U);API U GetMessageA(U*,U,U,U);API U TranslateMessage(U*);API U DispatchMessageA(U*);
static U owner,spin,edit,phase,notifications,scrolls,ends;
static void check(U yes,U code){if(!yes)ExitProcess(code);}
static U send(U m,U a,U b){return SendMessageA(spin,m,a,b);}
static U position(U h,U error){return SendMessageA(h,0x472,0,error);}
static void textis(const char*s){char b[64];GetWindowTextA(edit,b,64);for(U i=0;;++i){check(b[i]==s[i],29200);if(!s[i])break;}}
static U __stdcall procedure(U h,U m,U w,U l){
 if(m==0x4e&&((U*)l)[0]==spin){U*n=(U*)l;check(w==42&&n[1]==42&&n[2]==0xfffffd2e,29201);++notifications;
  if(phase==1){check(n[3]==12&&n[4]==1,29202);if(notifications==1){SetWindowTextA(owner,"updown veto");return 1;}check(notifications==2,29203);n[4]=2;}
  else if(phase==2)check(n[3]==14&&n[4]==0xffffffff,29204);
  return 0;
 }
 if(m==0x115&&l==spin){if((w&65535)==8){++ends;return 0;}check((w&65535)==4,29205);++scrolls;
  if(phase==1){check((w>>16)==14,29206);textis("14");phase=2;SetWindowTextA(owner,"updown mouse");}
  else if(phase==2){check((w>>16)==13,29207);textis("13");phase=3;SetWindowTextA(owner,"updown keyboard");}
  return 0;
 }
 return DefWindowProcA(h,m,w,l);
}
void mainCRTStartup(void){
 U instance=GetModuleHandleA(0),common[]={8,0x10};check(InitCommonControlsEx(common),29208);U cls[]={0,(U)procedure,0,0,instance,0,0,16,0,(U)"UpDownPort98"};check(RegisterClassA(cls),29209);
 owner=CreateWindowExA(0,"UpDownPort98","updown fixture",0x10cf0000,80,90,420,230,0,0,instance,0);check(owner,29210);
 edit=CreateWindowExA(0x200,"EDIT","",0x50010002,30,40,100,32,owner,41,instance,0);check(edit,29211);U oldproc=GetWindowLongA(edit,-4);
 spin=CreateWindowExA(0,"msctls_updown32","",0x500000b3,130,40,24,32,owner,42,instance,0);check(spin,29212);check(send(0x46a,0,0)==edit,29213);check(send(0x466,0,0)==0x640000,29214);textis("0");check(GetWindowLongA(edit,-4)!=oldproc,29215);
 U accel[8];for(U i=0;i<8;++i)accel[i]=0xa5a5a5a5;check(send(0x46c,1,(U)accel)==3&&accel[0]==0&&accel[1]==1&&accel[2]==0xa5a5a5a5,29216);U custom[]={0,1,2,3,5,9,8,20};check(send(0x46b,4,(U)custom)==1&&!send(0x46b,0,0),29217);check(send(0x46c,4,(U)accel)==4,29218);for(U i=0;i<8;++i)check(accel[i]==custom[i],29219);
 send(0x46f,0,59);check(send(0x471,0,12)==0,29220);textis("12");U bad=99;check(position(spin,(U)&bad)==12&&!bad,29221);
 SetWindowTextA(edit,"bogus");check(position(spin,(U)&bad)==12&&bad==1,29222);send(0x471,0,12);check(send(0x46d,16,0)==10&&send(0x46e,0,0)==16,29223);textis("0x000C");SetWindowTextA(edit,"x2A");check(position(spin,(U)&bad)==42&&!bad,29224);check(!send(0x46d,8,0)&&send(0x46e,0,0)==16,29225);send(0x46d,10,0);send(0x471,0,12);
 U bare=CreateWindowExA(0,"msctls_updown32","",0x40000040,220,40,50,24,owner,43,instance,0);check(bare,29226);SendMessageA(bare,0x46f,0,100000);check(SendMessageA(bare,0x471,0,70000)==0,29227);SendMessageA(bare,0x46f,0,10);check(position(bare,(U)&bad)==70000&&bad==1,29228);SendMessageA(bare,0x465,0,10);check(position(bare,0)==10,29229);U lower=0,upper=0;SendMessageA(bare,0x470,(U)&lower,(U)&upper);check(!lower&&upper==10,29230);check(DestroyWindow(bare),29231);
 check(SendMessageA(edit,0x87,0,0)&1,29232);phase=1;SetFocus(edit);SetWindowTextA(owner,"updown ready");U message[7];while(phase<3&&GetMessageA(message,0,0,0)){TranslateMessage(message);DispatchMessageA(message);}
 check(phase==3&&notifications==3&&scrolls==2&&ends==2,29233);
 U second=CreateWindowExA(0x200,"EDIT","",0x50010002,30,100,100,28,owner,44,instance,0);check(second,29234);check(send(0x469,second,0)==edit&&GetWindowLongA(edit,-4)==oldproc,29235);check(DestroyWindow(second)&&!send(0x46a,0,0),29236);check(send(0x469,edit,0)==0,29237);check(DestroyWindow(spin),29238);spin=0;check(DestroyWindow(owner),29239);ExitProcess(0);
}
