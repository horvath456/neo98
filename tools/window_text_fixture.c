/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetModuleHandleA(const char*);API const char*GetCommandLineA(void);API U RegisterClassA(void*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);API U DestroyWindow(U);API U SendMessageA(U,U,U,U);API U GetWindowTextA(U,char*,U);API U SetWindowTextA(U,const char*);API U GetWindowTextLengthA(U);API U GetDlgItemTextA(U,U,char*,U);API U SetDlgItemTextA(U,U,const char*);API U GetDlgItem(U,U);API U DefWindowProcA(U,U,U,U);API U SetWindowLongA(U,int,U);API U GetProcAddress(U,const char*);API U FindWindowA(const char*,const char*);API U PostMessageA(U,U,U,U);API U GetMessageA(void*,U,U,U);API U DispatchMessageA(void*);API U GetLastError(void);API void SetLastError(U);API U GetWindowTextW(U,void*,U);API U SetWindowTextW(U,const void*);API U GetWindowTextLengthW(U);API U GetDlgItemTextW(U,U,void*,U);API U SetDlgItemTextW(U,U,const void*);API U SetWindowPos(U,U,int,int,int,int,U);
static U gets,sets,baseline_gets,baseline_sets,reply=0,closing=0;
static int same(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return*a==*b;}static void check(int ok,U code){if(!ok)ExitProcess(code);}
static U put(char*out,const char*text,U cap){if(!cap)return 0;U n=0;while(n+1<cap&&text[n]){out[n]=text[n];++n;}out[n]=0;return n;}
static U __stdcall proc(U h,U m,U w,U l){if(m==0x8002)return 0x5a5a5a5a;if(m==0xc){++sets;return reply;}if(m==0xd){++gets;U n=put((char*)l,"callback",w);return reply?reply:n;}if(m==0xe){++gets;return reply?reply:8;}if(m==0x8001){check(closing&&gets==baseline_gets&&sets==baseline_sets,16700);ExitProcess(0);}return DefWindowProcA(h,m,w,l);}
static U create(U instance,const char*title,U parent,U id){return CreateWindowExA(0,"WindowTextProbe",title,parent?0x50000000:0x90000000,10,10,180,70,parent,id,instance,0);}
static void exercise(U instance,U window){
 char out[64];check(GetWindowTextA(window,out,sizeof out)==8&&same(out,"callback"),16701);check(GetWindowTextLengthA(window)==8,16702);check(DefWindowProcA(window,0xd,sizeof out,(U)out)==12&&same(out,"WindowText A"),16703);
 reply=0;check(SetWindowTextA(window,"ignored")==1&&sets==1,16704);check(SendMessageA(window,0xc,0,(U)"ignored")==0&&sets==2,16705);reply=0x80000000;check(!SetWindowTextA(window,"rejected")&&sets==3,16706);reply=0x12348001;check(GetWindowTextLengthA(window)==reply&&SendMessageA(window,0xe,0,0)==reply&&GetWindowTextA(window,out,sizeof out)==0xffff8001,16707);reply=0;
 out[0]='X';check(GetWindowTextA(window,out,0)==0&&out[0]==0,16708);check(GetWindowTextA(window,out,0x10004)==3&&same(out,"cal"),16709);out[0]='X';check(!GetWindowTextA(0,out,0)&&out[0]=='X',16710);check(!GetWindowTextA(0,out,1)&&!out[0]&&!GetWindowTextLengthA(0),16711);
 U child=create(instance,"child",window,0x12345678);check(child,16712);check(GetDlgItemTextA(window,0x12345678,out,sizeof out)==8&&same(out,"callback")&&SetDlgItemTextA(window,0x12345678,"ignored")==1,16713);out[0]='X';check(!GetDlgItemTextA(window,0xffffffff,out,1)&&!out[0]&&!SetDlgItemTextA(window,0xffffffff,"ignored"),16714);
 U duplicate=create(instance,"duplicate",window,0x12345678);check(duplicate&&SetWindowPos(duplicate,0,0,0,0,0,0x13)&&GetDlgItem(window,0x12345678)==duplicate,16715);
 U edit=CreateWindowExA(0,"EDIT","",0x50000004,0,0,170,35,window,71,instance,0);check(edit&&SetWindowTextA(edit,"a\r\nb")&&GetWindowTextLengthA(edit)==4&&GetWindowTextA(edit,out,sizeof out)==4&&same(out,"a\r\nb"),16716);
 U combo=CreateWindowExA(0,"COMBOBOX","",0x50000202,0,40,160,80,window,72,instance,0);check(combo&&SetWindowTextA(combo,"Gr\374\337e")&&GetWindowTextLengthA(combo)==5&&GetDlgItemTextA(window,72,out,sizeof out)==5&&same(out,"Gr\374\337e"),16717);
 /* the static text of SHELL32's warning for bad applications is 439 characters; DefWindowProc keeps it whole. */
 {static char longtext[440],back[512];for(U i=0;i<439;++i)longtext[i]=(char)('a'+i%26);longtext[439]=0;U text=CreateWindowExA(0,"STATIC","",0x50020000,0,130,160,80,window,73,instance,0);check(text&&SetDlgItemTextA(window,73,longtext)&&GetWindowTextLengthA(text)==439&&GetDlgItemTextA(window,73,back,sizeof back)==439&&same(back,longtext),16740);}
 U old=SetWindowLongA(edit,-4,(U)proc);check(old&&GetWindowTextA(edit,out,sizeof out)==8&&same(out,"callback"),16718);SetWindowLongA(edit,-4,old);
 U user=GetModuleHandleA("USER32.DLL");typedef U(__stdcall*LEN)(U);LEN length=(LEN)GetProcAddress(user,(const char*)342);check(length&&length(window)==8,16719);
 SetLastError(0);check(!GetWindowTextW(window,(void*)0xdeadbeef,32)&&GetLastError()==120,16720);SetLastError(0);check(!SetWindowTextW(window,(void*)0xdeadbeef)&&GetLastError()==120,16721);SetLastError(0);check(!GetWindowTextLengthW(window)&&GetLastError()==120,16722);SetLastError(0);check(!GetDlgItemTextW(window,71,(void*)0xdeadbeef,32)&&GetLastError()==120,16723);SetLastError(0);check(!SetDlgItemTextW(window,71,(void*)0xdeadbeef)&&GetLastError()==120,16724);
 typedef U(__stdcall*GET)(U,U,U);GET internal=(GET)GetProcAddress(user,(const char*)368);SetLastError(0);check(internal&&!internal(window,0xdeadbeef,32)&&GetLastError()==120,16725);
 check(DestroyWindow(combo)&&DestroyWindow(edit)&&DestroyWindow(child)&&DestroyWindow(duplicate),16726);
}
static U setup(void){U instance=GetModuleHandleA(0),wc[10]={0,(U)proc,0,0,instance,0,0,0,0,(U)"WindowTextProbe"};check(RegisterClassA(wc),16727);return instance;}
void run_window_text_test(void){U instance=setup(),h=create(instance,"WindowText A",0,0);check(h,16728);exercise(instance,h);check(DestroyWindow(h),16729);}
#ifdef WINDOW_TEXT_STANDALONE
void mainCRTStartup(void){U instance=setup();const char*cmd=GetCommandLineA();U n=0;while(cmd[n])++n;int sibling=n&&cmd[n-1]=='B';U h=create(instance,sibling?"WindowText B":"WindowText A",0,0);check(h,16730);
 if(sibling){U foreign=FindWindowA("WindowTextProbe","WindowText A");char out[64];check(foreign,16731);check(GetWindowTextA(h,out,sizeof out)==8&&same(out,"callback"),16732);check(GetWindowTextA(foreign,out,sizeof out)==12&&same(out,"WindowText A")&&GetWindowTextLengthA(foreign)==12,16733);SetLastError(0);check(!SetWindowTextA(foreign,"forbidden")&&GetLastError()==50,16734);out[0]='X';check(SendMessageA(foreign,0x8002,0,0)==0x5a5a5a5a&&out[0]=='X',16735);check(PostMessageA(foreign,0x8001,0,0)&&DestroyWindow(h),16736);ExitProcess(0);}
 exercise(instance,h);baseline_gets=gets;baseline_sets=sets;closing=1;U message[7];while(GetMessageA(message,0,0,0))DispatchMessageA(message);ExitProcess(16737);
}
#endif
