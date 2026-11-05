/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API void Sleep(U);API U GetTickCount(void);API U GetCurrentThreadId(void);API U CreateThread(U,U,U,U,U,U*);API U WaitForSingleObject(U,U);API U CloseHandle(U);
API U RegisterClassA(U*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);API U DefWindowProcA(U,U,U,U);API U DestroyWindow(U);API U ShowWindow(U,U);API U SetFocus(U);API U SetWindowTextA(U,const char*);
API U GetMessageTime(void);API U GetMessagePos(void);API U GetMessageExtraInfo(void);API U SetMessageExtraInfo(U);API U PeekMessageA(U*,U,U,U,U);API U GetMessageA(U*,U,U,U);API U PostMessageA(U,U,U,U);API void PostQuitMessage(U);API U SendMessageA(U,U,U,U);API U TranslateMessage(U*);API U DispatchMessageA(U*);
API U SetWindowsHookExA(U,U,U,U);API U UnhookWindowsHookEx(U);API U CallNextHookEx(U,U,U,U);API U SetTimer(U,U,U,U);API U KillTimer(U,U);
static U window,mode,nested,hook_calls,phase,keys,chars,moves,buttons;static volatile U thread_seen;
static void check(int ok,U code){if(!ok)ExitProcess(code);}
static U pack(U*m){return(m[5]&65535)|(m[6]<<16);}
static void matches(U*m,U code){check(GetMessageTime()==m[4]&&GetMessagePos()==pack(m),code);}
static U __stdcall hook(U code,U remove,U pointer){U*m=(U*)pointer;if(m[1]>=0x8000&&m[1]<=0x8003){matches(m,11900);check(!GetMessageExtraInfo(),11901);++hook_calls;if(mode==1&&m[1]==0x8000){mode=0;U inner[7];check(PeekMessageA(inner,window,0x8001,0x8001,1),11902);matches(inner,11903);nested=inner[4];SetMessageExtraInfo(0xabcdef01);}}return CallNextHookEx(0,code,remove,pointer);}
static U __stdcall thread(U p){check(!GetMessageTime()&&!GetMessagePos()&&!GetMessageExtraInfo(),11904);check(!SetMessageExtraInfo(p)&&GetMessageExtraInfo()==p,11905);thread_seen=1;Sleep(1);check(GetMessageExtraInfo()==p,11906);return 7;}
static U __stdcall proc(U h,U m,U w,U l){if(m==0x8002&&mode==2){U inner[7];check(PeekMessageA(inner,window,0x8003,0x8003,1),11907);nested=inner[4];SetMessageExtraInfo(0x11112222);}return DefWindowProcA(h,m,w,l);}
void run_message_metadata_test(void){
 U cls[10]={0,(U)proc,0,0,0,0,0,0,0,(U)"MessageMetadata98"};check(RegisterClassA(cls),11908);window=CreateWindowExA(0,"MessageMetadata98","metadata",0,30,40,260,180,0,0,0,0);check(window,11909);
 check(SetMessageExtraInfo(0x87654321)==0&&GetMessageExtraInfo()==0x87654321,11910);U id=0,t=CreateThread(0,0,(U)thread,0x13579bdf,0,&id);check(t&&id,11911);check(WaitForSingleObject(t,1000)==0&&thread_seen&&GetMessageExtraInfo()==0x87654321&&CloseHandle(t),11912);
 U h=SetWindowsHookExA(3,(U)hook,0,GetCurrentThreadId());check(h,11913);U low=GetTickCount();check(PostMessageA(window,0x8000,1,2),11914);Sleep(3);check(PostMessageA(window,0x8001,3,4),11915);U high=GetTickCount(),msg[7];mode=1;check(PeekMessageA(msg,window,0x8000,0x8000,0),11916);check(msg[4]-low<=high-low&&GetMessageTime()==nested&&GetMessageExtraInfo()==0xabcdef01&&hook_calls==2,11917);
 check(PeekMessageA(msg,window,0x8000,0x8000,1),11918);matches(msg,11919);check(!GetMessageExtraInfo(),11920);U before=GetMessageTime();SetMessageExtraInfo(77);check(!PeekMessageA(msg,window,0x8000,0x8000,1)&&GetMessageTime()==before&&GetMessageExtraInfo()==77,11921);
 PostMessageA(window,0x8002,0,0);Sleep(2);PostMessageA(window,0x8003,0,0);check(GetMessageA(msg,window,0x8002,0x8002),11922);mode=2;DispatchMessageA(msg);check(GetMessageTime()==nested&&GetMessageExtraInfo()==0x11112222,11923);mode=0;check(UnhookWindowsHookEx(h),11924);
 check(SetTimer(window,19,10,0),11925);Sleep(70);check(PeekMessageA(msg,window,0x113,0x113,0),11926);matches(msg,11927);check(!GetMessageExtraInfo(),11928);before=msg[4];Sleep(2);check(PeekMessageA(msg,window,0x113,0x113,1)&&msg[4]==before,11929);matches(msg,11930);check(KillTimer(window,19),11931);
#ifdef MESSAGE_METADATA_HOST_INPUT
 ShowWindow(window,8);SetFocus(window);SetWindowTextA(window,"metadata input ready");phase=1;
 while(phase&&GetMessageA(msg,0,0,0)>0){if(msg[1]==0x8010){phase=0;break;}if(msg[1]==0x100||msg[1]==0x101||msg[1]==0x102||msg[1]==0x200||msg[1]==0x201||msg[1]==0x202){matches(msg,11932);check(!GetMessageExtraInfo(),11933);if(msg[1]==0x100)++keys;if(msg[1]==0x102){check(msg[2]=='a',11934);++chars;}if(msg[1]==0x200)++moves;if(msg[1]==0x201||msg[1]==0x202)++buttons;}TranslateMessage(msg);DispatchMessageA(msg);if(keys&&chars&&moves&&buttons==2)SetWindowTextA(window,"metadata input passed");}
 check(keys==1&&chars==1&&moves>=1&&buttons==2,11935);
#endif
 before=GetMessageTime();U position=GetMessagePos();SetMessageExtraInfo(0xfedcba98);PostQuitMessage(37);check(GetMessageA(msg,0,0,0)==0&&msg[1]==0x12&&msg[2]==37,11936);check(GetMessageTime()==before&&GetMessagePos()==position&&GetMessageExtraInfo()==0xfedcba98,11937);check(DestroyWindow(window),11938);
}
#ifdef MESSAGE_METADATA_STANDALONE
void mainCRTStartup(void){run_message_metadata_test();ExitProcess(0);}
#endif
