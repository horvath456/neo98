// SPDX-License-Identifier: GPL-2.0-or-later
#define API __declspec(dllimport)
#define S __stdcall
typedef unsigned U;
struct Msg{U hwnd,message,wp,lp,time;int x,y;};
struct WndClass{U style,proc,cbcls,cbwin,instance,icon,cursor,brush,menu,name;};
API U S GetDesktopWindow(void);API U S GetLastError(void);API void S SetLastError(U);
API void S ExitProcess(U);API U S GetCurrentThreadId(void);API U S TlsAlloc(void);API U S TlsSetValue(U,U);API U S TlsGetValue(U);API U S TlsFree(U);
API U S CreateEventA(void*,U,U,const char*);API U S SetEvent(U);API U S WaitForSingleObject(U,U);API U S CloseHandle(U);API U S CreateThread(void*,U,U(S*)(void*),void*,U,U*);
API U S RegisterClassA(struct WndClass*);API U S UnregisterClassA(const char*,U);API U S CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);API U S DestroyWindow(U);
API U S SendMessageTimeoutA(U,U,U,U,U,U,U*);
API U S DefWindowProcA(U,U,U,U);API U S SendMessageA(U,U,U,U);API U S InSendMessage(void);API U S InSendMessageEx(void*);API U S ReplyMessage(U);API U S PeekMessageA(struct Msg*,U,U,U,U);API U S GetMessageA(struct Msg*,U,U,U);API U S DispatchMessageA(struct Msg*);API U S PostMessageA(U,U,U,U);API void S PostQuitMessage(U);API void S Sleep(U);API U S SetMessageExtraInfo(U);API U S GetMessageExtraInfo(void);
static volatile U owner,worker_id,worker_window,failed,phase,guard,timeout_phase;static U main_window,tls,started,release_wait,done,stage_event;
static U S wndproc(U h,U m,U w,U l){
 U expected=h==main_window?owner:worker_id,tag=h==main_window?0xaaa:0xbbb;
 if(m>=0x8001&&m<=0x8006){if(GetCurrentThreadId()!=expected||TlsGetValue(tls)!=tag)failed=29600;if(!InSendMessage()||InSendMessageEx(0)!=1)failed=29601;}
 if(m==0x8001){if(GetMessageExtraInfo()!=0x12345678)failed=29602;U result=SendMessageA(main_window,0x8002,w+1,l);if(result!=0x12345679||!InSendMessage())failed=29603;return result+1;}
 if(m==0x8002){if(GetMessageExtraInfo()!=0x12345678)failed=29604;return w;}
 if(m==0x8003){phase=1;if(!ReplyMessage(0x76543210)||!InSendMessage()||InSendMessageEx(0)!=9)failed=29605;SetEvent(stage_event);WaitForSingleObject(release_wait,~0u);phase=2;SetEvent(done);return 0xdeadcafe;}
 if(m==0x8004){++guard;return 17;}
 if(m==0x8005){DestroyWindow(h);PostQuitMessage(0);return 19;}
 if(m==0x8006){SetMessageExtraInfo(0x777);return 23;}
 if(m==0x8007){timeout_phase=1;Sleep(20);timeout_phase=2;return 0xdeadbeef;}
 if(m==0x8008)return 0;
 if(m==0x8010){PostQuitMessage(0);return 0;}
 return DefWindowProcA(h,m,w,l);
}
static U S worker(void*p){(void)p;worker_id=GetCurrentThreadId();TlsSetValue(tls,0xbbb);SetMessageExtraInfo(0x2468);worker_window=CreateWindowExA(0,"Neo98SendThread","",0,0,0,8,8,0,0,0x400000,0);if(!worker_window){failed=29610;SetEvent(started);return 1;}SetEvent(started);
 // Normal kernel waits must not dispatch a queued SendMessage.
 WaitForSingleObject(release_wait,~0u);
 struct Msg m;while(GetMessageA(&m,0,0,0))DispatchMessageA(&m);
 return 0;
}
static U S release_worker(void*p){(void)p;Sleep(5);if(guard)failed=29612;SetEvent(release_wait);return 0;}
U run_send_message_test(void){U desktop=GetDesktopWindow();SetLastError(0x98);SendMessageA(desktop,0,0,0);if(GetLastError()==1400)return 29633;owner=GetCurrentThreadId();tls=TlsAlloc();if(tls==~0u)return 29620;TlsSetValue(tls,0xaaa);SetMessageExtraInfo(0x12345678);if(InSendMessage()||InSendMessageEx(0)||ReplyMessage(9))return 29621;
 struct WndClass c={0};c.proc=(U)wndproc;c.instance=0x400000;c.name=(U)"Neo98SendThread";if(!RegisterClassA(&c))return 29622;
 main_window=CreateWindowExA(0,"Neo98SendThread","",0,0,0,8,8,0,0,0x400000,0);if(!main_window)return 29623;
 started=CreateEventA(0,0,0,0);release_wait=CreateEventA(0,0,0,0);done=CreateEventA(0,0,0,0);stage_event=CreateEventA(0,0,0,0);
 U t=CreateThread(0,0,worker,0,0,0);if(!t||WaitForSingleObject(started,5000)||failed)return failed?failed:29624;
 // More than the record capacity: every expired pending send must release
 // its slot and must never be delivered when the receiver starts pumping.
 U answer=0xfeedface;
 for(U i=0;i<70;++i){SetLastError(0x98);if(SendMessageTimeoutA(worker_window,0x8004,0,0,0,i?0:10,&answer)||answer!=0xfeedface||GetLastError()||guard)return 29634;}
 U timer=CreateThread(0,0,release_worker,0,0,0);if(!timer||SendMessageA(worker_window,0x8004,0,0)!=17||guard!=1)return 29625;
 if(SendMessageA(worker_window,0x8001,0x12345678,0)!=0x1234567a||failed)return failed?failed:29626;
 if(!SendMessageTimeoutA(worker_window,0x8008,0,0,0,500,&answer)||answer)return 29635;
 for(U block=0;block<2;++block){
  timeout_phase=0;answer=0xfeedface;
  if(SendMessageTimeoutA(worker_window,0x8007,0,0,block,5,&answer)||answer!=0xfeedface||timeout_phase!=1)return 29636;
  for(U tries=0;tries<1000&&timeout_phase!=2;++tries)Sleep(1);
  if(timeout_phase!=2)return 29637;
 }
 if(InSendMessage()||GetMessageExtraInfo()!=0x12345678||TlsGetValue(tls)!=0xaaa)return 29627;
 if(SendMessageA(worker_window,0x8003,0,0)!=0x76543210||phase!=1||failed)return failed?failed:29628;
 if(WaitForSingleObject(stage_event,5000))return 29629;
 SetEvent(release_wait);if(WaitForSingleObject(done,5000))return 29630;
 if(SendMessageA(worker_window,0x8005,0,0)!=19||failed)return failed?failed:29631;
 if(WaitForSingleObject(t,5000)||WaitForSingleObject(timer,5000))return 29632;
 CloseHandle(timer);CloseHandle(started);CloseHandle(release_wait);CloseHandle(done);CloseHandle(stage_event);CloseHandle(t);DestroyWindow(main_window);UnregisterClassA("Neo98SendThread",0x400000);TlsFree(tls);return failed;
}
#ifdef SEND_MESSAGE_STANDALONE
void mainCRTStartup(void){ExitProcess(run_send_message_test());}
#endif
