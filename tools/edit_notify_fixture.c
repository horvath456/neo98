/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API void Sleep(U);API U RegisterClassA(U*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);API U DestroyWindow(U);API U IsWindow(U);API U SendMessageA(U,U,U,U);API U GetWindowTextA(U,char*,U);
API U GetWindowLongA(U,int);API U GetMessageA(U*,U,U,U);API U TranslateMessage(U*);API U DispatchMessageA(U*);API void PostQuitMessage(U);
static U parent,edit,mode,calls,depth,in_send,replacement,quiet,word_edit,word_calls;
static void check(int okay,U code){if(!okay)ExitProcess(code);}
static int __stdcall word_break(char *text,int current,int length,int code){
 (void)text;(void)current;(void)length;(void)code;++word_calls;
 check(SendMessageA(word_edit,0xd1,0,0)==(U)word_break,10076);return 3;
}
static U make_edit(U p){return CreateWindowExA(0,"EDIT","",0x50000000,0,0,100,25,p,1152,0,0);}
static U updates;
static U __stdcall proc(U h,U m,U w,U l){
 /* EN_UPDATE comes first for the same change (ECNotifyParent before the
    text is shown); WinPac 2 REGISTER.EXE sums on it. */
 if(m==0x111&&(w>>16)==0x400){if(!quiet){++updates;check((w&65535)==1152&&l==edit&&(mode<1||mode>5||updates==calls+1),10090);if(mode==10||mode==11){check(!SendMessageA(edit,0xb8,0,0),10092);if(mode==11)SendMessageA(edit,0xb9,1,0);}}return 0;}
 if(m==0x111&&(w>>16)==0x300){
  char text[16];if(quiet)return 0;++calls;check((w&65535)==1152&&l==edit&&IsWindow(l),10000);
  check(GetWindowTextA(edit,text,16)>0&&text[0]=='a',10001);
  if(mode!=9)check(in_send,10002);
  if(mode==10||mode==11)check(SendMessageA(edit,0xb8,0,0)==(mode==11),10093);
  if(mode==1){Sleep(1);check(in_send,10003);}
  if(mode==2&&!depth){depth=1;SendMessageA(edit,0x102,'b',0);depth=0;check(GetWindowTextA(edit,text,16)==2&&text[1]=='b',10004);}
  if(mode==3||mode==5){check(DestroyWindow(edit),10005);replacement=make_edit(h);check(replacement&&replacement!=edit,10006);}
  if(mode==4){check(DestroyWindow(h),10007);}
  if(mode==9){Sleep(1);PostQuitMessage(0);}
  return 0;
 }
 return DefWindowProcA(h,m,w,l);
}
static void setup(void){U cls[10]={0,(U)proc,0,0,0,0,0,0,0,(U)"EditNotifyPort"};check(RegisterClassA(cls),10008);}
static void windows(void){parent=CreateWindowExA(0,"EditNotifyPort","EDIT notification",0,0,0,150,80,0,0,0,0);check(parent,10009);edit=make_edit(parent);check(edit,10010);}
void run_edit_notify_test(void){setup();for(mode=1;mode<=5;++mode){windows();calls=0;updates=0;in_send=1;
 if(mode==5)SendMessageA(edit,0xc2,1,(U)"a");else SendMessageA(edit,0x102,'a',0);
 in_send=0;check(calls==(mode==2?2u:1u),10011);check(updates==calls,10091);
 if(mode==3||mode==5){check(!IsWindow(edit)&&IsWindow(replacement),10012);char s[8];check(!GetWindowTextA(replacement,s,8),10013);}
 if(mode==4)check(!IsWindow(parent)&&!IsWindow(edit),10014);else check(DestroyWindow(parent),10015);
 }
 /* WM_SETTEXT resets EM_GETMODIFY before both synchronous notifications.
    A parent's EM_SETMODIFY during EN_UPDATE survives EN_CHANGE and return.
    Original USER,23A8,2405; Explorer's address icons. */
 for(mode=10;mode<=11;++mode){windows();calls=0;updates=0;in_send=1;
  SendMessageA(edit,0xb9,1,0);check(SendMessageA(edit,0xc,0,(U)"address")==1,10094);
  check(calls==1&&updates==1,10095);check(SendMessageA(edit,0xb8,0,0)==(mode==11),10096);
  in_send=0;check(DestroyWindow(parent),10097);
 }
 /* ES_READONLY. EM_SETREADONLY answers 1 and keeps the bit in the window
    style; no path that changes text runs while it is set, and copying still
    does. */
 mode=0;windows();calls=0;in_send=1;{char text[16];
 SendMessageA(edit,0x102,'a',0);check(calls==1,10020);
 SendMessageA(edit,0xb1,0,1);
 check(SendMessageA(edit,0xcf,1,0)==1,10021);
 check((GetWindowLongA(edit,-16)&0x800)!=0,10022);
 calls=0;
 SendMessageA(edit,0x102,'b',0);SendMessageA(edit,0x102,8,0);
 SendMessageA(edit,0x100,0x2e,0);SendMessageA(edit,0x303,0,0);
 SendMessageA(edit,0x300,0,0);SendMessageA(edit,0x302,0,0);
 SendMessageA(edit,0x304,0,0);SendMessageA(edit,0xc7,0,0);
 check(!calls,10023);
 check(GetWindowTextA(edit,text,16)==1&&text[0]=='a',10024);
 SendMessageA(edit,0xb1,0,1);check(SendMessageA(edit,0x301,0,0)==1,10025);
 check(SendMessageA(edit,0xcf,0,0)==1,10026);
 check(!(GetWindowLongA(edit,-16)&0x800),10027);
 SendMessageA(edit,0xb1,1,1);SendMessageA(edit,0x102,'b',0);
 check(calls==1&&GetWindowTextA(edit,text,16)==2&&text[1]=='b',10028);}
 in_send=0;check(DestroyWindow(parent),10029);
 /* ES_PASSWORD supplies '*' on a single-line edit. EM_SETPASSWORDCHAR changes
    only the display character; WM_GETTEXT continues to return the real text. */
 mode=0;windows();quiet=1;
 {U password=CreateWindowExA(0,"EDIT","secret",0x50000020,0,30,100,25,parent,1155,0,0);char text[16];
  check(password,10062);check(SendMessageA(password,0xd2,0,0)=='*',10063);
  check((GetWindowLongA(password,-16)&0x20)!=0,10071);
  check(GetWindowTextA(password,text,16)==6&&text[0]=='s'&&text[5]=='t',10064);
  check(SendMessageA(password,0xcc,'#',0)==0,10065);
  check(SendMessageA(password,0xd2,0,0)=='#'&&(GetWindowLongA(password,-16)&0x20),10066);
  check(GetWindowTextA(password,text,16)==6&&text[0]=='s'&&text[5]=='t',10067);
  SendMessageA(password,0xb1,0,6);
  check(!SendMessageA(password,0x301,0,0),10072);
  check(!SendMessageA(password,0x300,0,0),10073);
  check(GetWindowTextA(password,text,16)==6&&text[0]=='s'&&text[5]=='t',10074);
  check(SendMessageA(password,0xcc,0,0)==0&&!SendMessageA(password,0xd2,0,0),10068);
  check((GetWindowLongA(password,-16)&0x20)==0,10075);
  check(DestroyWindow(password),10069);}
 quiet=0;check(DestroyWindow(parent),10070);
 mode=0;windows();quiet=1;word_calls=0;
 word_edit=CreateWindowExA(0,"EDIT","aa-bb cc",0x50000004,0,30,20,70,parent,1156,0,0);
 check(word_edit,10077);check(SendMessageA(word_edit,0xd0,0,(U)word_break)==0,10078);
 check(SendMessageA(word_edit,0xd1,0,0)==(U)word_break,10079);
 check(SendMessageA(word_edit,0xba,0,0)>=2&&word_calls,10080);
 check(DestroyWindow(word_edit),10081);quiet=0;check(DestroyWindow(parent),10082);
 /* EM_LINELENGTH. The single-line body of USER.EXE answers the whole text
    length, the multiline worker answers the length of the line the
    character index sits on and stops before the line break. wParam FFFFFFFFh counts the characters the selection
    leaves over on the lines it touches. */
 mode=0;windows();in_send=1;quiet=1;
 check(SendMessageA(edit,0x0c,0,(U)"abcdef")==1,10030);
 check(SendMessageA(edit,0xc1,0,0)==6,10031);
 check(SendMessageA(edit,0xc1,4,0)==6,10032);
 check(SendMessageA(edit,0xc1,0xffffffff,0)==6,10033);
 {U multi=CreateWindowExA(0,"EDIT","",0x500000c4,0,0,400,100,parent,1153,0,0);
  check(multi,10034);
  check(SendMessageA(multi,0x0c,0,(U)"ab\r\ncdefg\r\nhi")==1,10035);
  check(SendMessageA(multi,0xba,0,0)==3,10036);
  check(SendMessageA(multi,0xc1,0,0)==2,10037);
  check(SendMessageA(multi,0xc1,4,0)==5,10038);
  check(SendMessageA(multi,0xc1,11,0)==2,10039);
  /* Not the whole text and not one fixed line: the three answers differ. */
  check(SendMessageA(multi,0xc1,9999,0)==2,10040);
  SendMessageA(multi,0xb1,0,1);
  check(SendMessageA(multi,0xc1,0xffffffff,0)==1,10041);
  SendMessageA(multi,0xb1,1,8);
  check(SendMessageA(multi,0xc1,0xffffffff,0)==2,10042);
  SendMessageA(multi,0xb1,4,4);
  check(SendMessageA(multi,0xc1,0xffffffff,0)==5,10043);
  check(DestroyWindow(multi),10044);}
 quiet=0;in_send=0;check(DestroyWindow(parent),10045);
 /* The text an edit control holds. USER.EXE keeps it in a movable block of a
    local heap, so the limit is the one EM_LIMITTEXT sets and not a fixed
    buffer; USER32 keeps the same contract with EM_SETLIMITTEXT. A multiline
    control takes a text of 30.000 characters whole, a limit of 100 cuts at
    100, and the negative probe is the character past the limit, which changes
    nothing and is not notified. */
 mode=0;windows();in_send=1;quiet=1;
 {U big=CreateWindowExA(0,"EDIT","",0x500000c4,0,0,400,100,parent,1154,0,0);
  check(big,10050);
  /* The default limit of a control of this port is the 64 KiB of the 16-bit
     contract, so a text below it goes in whole. */
  check(SendMessageA(big,0xd5,0,0)>=30000,10051);
  {static char large[30001];U n;for(n=0;n<30000;++n)large[n]=(char)('a'+(n%26));large[30000]=0;
   check(SendMessageA(big,0x0c,0,(U)large)==1,10052);
   check(SendMessageA(big,0x0e,0,0)==30000,10053);
   {static char back[30001];check(GetWindowTextA(big,back,30001)==30000,10054);
    check(back[0]=='a'&&back[29999]==large[29999]&&!back[30000],10055);}}
  /* EM_LIMITTEXT names a smaller limit; the text already there stays, and the
     next character is refused. */
  SendMessageA(big,0xc5,100,0);
  check(SendMessageA(big,0xd5,0,0)==100,10056);
  check(SendMessageA(big,0x0c,0,(U)"short")==1,10057);
  {U before=SendMessageA(big,0x0e,0,0);U k;
   SendMessageA(big,0xb1,before,before);
   for(k=0;k<200;++k)SendMessageA(big,0x102,'x',0);
   check(SendMessageA(big,0x0e,0,0)==100,10058);
   /* The negative probe: one more character past the limit changes nothing. */
   calls=0;SendMessageA(big,0x102,'y',0);
   check(SendMessageA(big,0x0e,0,0)==100,10059);}
  check(DestroyWindow(big),10060);}
 quiet=0;in_send=0;check(DestroyWindow(parent),10061);
 mode=0;for(U n=0;n<220;++n){windows();calls=0;in_send=1;SendMessageA(edit,0x102,8,0);check(!calls,10016);SendMessageA(edit,0x102,'a',0);check(calls==1,10017);in_send=0;check(DestroyWindow(parent),10018);}
}
#ifdef EDIT_NOTIFY_STANDALONE
void mainCRTStartup(void){run_edit_notify_test();ExitProcess(0);}
#endif
#ifdef EDIT_NOTIFY_HOST
void mainCRTStartup(void){setup();mode=9;windows();U message[7];while(GetMessageA(message,0,0,0)){TranslateMessage(message);DispatchMessageA(message);}check(calls==1,10019);ExitProcess(0);}
#endif
