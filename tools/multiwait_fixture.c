// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API char*GetCommandLineA(void);API U CreateEventA(U,U,U,const char*);API U SetEvent(U);API U ResetEvent(U);API U CreateSemaphoreA(U,U,U,const char*);API U ReleaseSemaphore(U,U,U*);API U CreateMutexA(U,U,const char*);API U ReleaseMutex(U);API U CreateThread(U,U,U,U,U,U*);API void Sleep(U);API U CloseHandle(U);API U WaitForSingleObject(U,U);API U WaitForMultipleObjects(U,const U*,U,U);API U WaitForMultipleObjectsEx(U,const U*,U,U,U);API U MsgWaitForMultipleObjects(U,const U*,U,U,U);API U MsgWaitForMultipleObjectsEx(U,const U*,U,U,U);API U WaitMessage(void);API U PeekMessageA(U*,U,U,U,U);API U PostMessageA(U,U,U,U);API U GetLastError(void);API U GetModuleHandleA(const char*);API U RegisterClassA(U*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);API U DefWindowProcA(U,U,U,U);API U DestroyWindow(U);API U DuplicateHandle(U,U,U,U*,U,U,U);API U GetCurrentProcess(void);API U CreateProcessA(const char*,char*,U,U,U,U,const char*,const char*,U*,U*);API U GetExitCodeProcess(U,U*);API U SetTimer(U,U,U,U);API U KillTimer(U,U);
void*memset(void*p,int v,U n){char*q=p;while(n--)*q++=v;return p;}
static void ck(U yes,U code){if(!yes)ExitProcess(code);}
API U GetProcAddress(U,const char*);API U GetWindowTextA(U,char*,U);API U DispatchMessageA(U*);API U GetQueueStatus(U);API U GetInputState(void);API U SendMessageA(U,U,U,U);API U ShowWindow(U,U);API U SetFocus(U);API U SetWindowTextA(U,const char*);API void SetLastError(U);
static volatile U sent_count;
static U event1,event2,sem,window,original,duplicate;static volatile U phase;
static U __stdcall proc(U h,U m,U w,U l){if(m==0x8002){++sent_count;return 0x98;}return DefWindowProcA(h,m,w,l);}
static U __stdcall worker(U mode){Sleep(5);if(mode==1)ck(SetEvent(event2),17901);if(mode==2){ck(WaitForSingleObject(event1,0)==0,17902);ck(SetEvent(event1)&&ReleaseSemaphore(sem,1,0),17903);}if(mode==3)ck(PostMessageA(window,0x8001,3,0),17904);if(mode==4){ck(CloseHandle(original)&&SetEvent(duplicate),17905);}if(mode==5){phase=50;ck(SendMessageA(window,0x8002,0,0)==0x98,17952);}phase=mode;return 43;}
static U thread(U mode){U id;U h=CreateThread(0,0,(U)worker,mode,0,&id);ck(h,17906);return h;}
static void input_ready(void){char title[16];U tries=0;do{ck(++tries<1000,17986);Sleep(1);GetWindowTextA(window,title,sizeof title);}while(title[0]!='!');}
static void join(U h){ck(WaitForSingleObject(h,~0u)==0&&CloseHandle(h),17907);}
void mainCRTStartup(void){if(GetCommandLineA()[0]=='C'){Sleep(5);ExitProcess(42);}U msg[7],a[3],h,v;
 event1=CreateEventA(0,0,1,"WaitAlias");event2=CreateEventA(0,1,0,0);sem=CreateSemaphoreA(0,0,3,0);ck(event1&&event2&&sem,17908);
 a[0]=event1;a[1]=event2;ck(WaitForMultipleObjects(2,a,0,0)==0&&WaitForSingleObject(event1,0)==258,17909);
 ck(WaitForMultipleObjects(0,a,0,0)==~0u&&GetLastError()==151,17910);ck(WaitForMultipleObjects(65,a,0,0)==~0u&&GetLastError()==151,17911);
 a[0]=0xdeadbeef;ck(WaitForMultipleObjects(1,a,0,0)==~0u&&GetLastError()==6,17912);a[0]=event1;
 duplicate=CreateEventA(0,0,0,"WaitAlias");ck(duplicate&&duplicate!=event1,17913);a[1]=duplicate;ck(WaitForMultipleObjects(2,a,0,0)==~0u&&GetLastError()==87,17914);ck(CloseHandle(duplicate),17915);
 a[1]=event2;h=thread(1);ck(WaitForMultipleObjects(2,a,0,~0u)==1&&phase==1,17916);join(h);ck(WaitForSingleObject(event2,0)==0&&ResetEvent(event2),17917);
 ck(SetEvent(event1),17918);a[0]=event1;a[1]=sem;h=thread(2);ck(WaitForMultipleObjects(2,a,1,~0u)==0&&phase==2,17919);join(h);ck(WaitForSingleObject(event1,0)==258&&WaitForSingleObject(sem,0)==258,17920);
 a[0]=CreateMutexA(0,0,0);a[1]=CreateSemaphoreA(0,1,1,0);ck(a[0]&&a[1]&&WaitForMultipleObjectsEx(2,a,1,0,0)==0&&ReleaseMutex(a[0])&&WaitForSingleObject(a[1],0)==258,17921);ck(CloseHandle(a[0])&&CloseHandle(a[1]),17922);
 a[0]=event1;ck(WaitForMultipleObjectsEx(1,a,0,0,1)==258,17923);ck(WaitForMultipleObjects(1,a,0,2)==258,17924);
 original=CreateEventA(0,0,0,"WaitRetain");duplicate=CreateEventA(0,0,0,"WaitRetain");ck(original&&duplicate&&original!=duplicate,17925);a[0]=original;h=thread(4);ck(WaitForMultipleObjects(1,a,0,~0u)==0&&phase==4,17926);join(h);ck(CloseHandle(duplicate),17927);
 U instance=GetModuleHandleA(0),wc[10]={0,(U)proc,0,0,instance,0,0,0,0,(U)"WaitProbe"};ck(RegisterClassA(wc),17928);window=CreateWindowExA(0,"WaitProbe","",0,0,0,10,10,0,0,instance,0);ck(window,17929);while(PeekMessageA(msg,0,0,0,1)){}
 ck(PostMessageA(window,0x8001,1,0)&&MsgWaitForMultipleObjects(0,0,0,0,8)==0,17930);ck(PeekMessageA(msg,0,0,0,0)&&msg[1]==0x8001,17931);ck(MsgWaitForMultipleObjects(0,0,0,0,8)==258,17932);ck(MsgWaitForMultipleObjectsEx(0,0,0,8,4)==0,17933);ck(PeekMessageA(msg,0,0,0,1)&&MsgWaitForMultipleObjectsEx(0,0,0,8,4)==258,17934);
 ck(PostMessageA(window,0x8001,2,0)&&MsgWaitForMultipleObjects(0,0,0,0,1)==258,17935);ck(PeekMessageA(msg,0,0,0,1),17936);
 h=thread(3);ck(WaitMessage()==1&&phase==3,17937);join(h);ck(PeekMessageA(msg,0,0,0,1)&&msg[2]==3,17938);
 h=thread(3);a[0]=event2;ck(MsgWaitForMultipleObjects(1,a,0,~0u,8)==1,17939);join(h);ck(PeekMessageA(msg,0,0,0,1),17940);
 ck(MsgWaitForMultipleObjectsEx(0,0,0,8,8)==~0u,17941);ck(MsgWaitForMultipleObjectsEx(0,0,0,8,2)==258,17942);
 U timer=SetTimer(window,71,10,0);ck(timer&&MsgWaitForMultipleObjects(0,0,0,100,0x10)==0,17943);ck(PeekMessageA(msg,0,0,0,0)&&msg[1]==0x113,17944);ck(MsgWaitForMultipleObjects(0,0,0,0,0x10)==258&&MsgWaitForMultipleObjectsEx(0,0,0,0x10,4)==0,17945);ck(PeekMessageA(msg,0,0,0,1)&&KillTimer(window,timer),17946);

 U user32=GetModuleHandleA("USER32.DLL");
 ck(GetProcAddress(user32,"GetInputState")&&GetProcAddress(user32,"GetInputState")==GetProcAddress(user32,(const char*)263),17988);
 ck(GetProcAddress(user32,"GetQueueStatus")&&GetProcAddress(user32,"GetQueueStatus")==GetProcAddress(user32,(const char*)309),17989);
 /* Querying one category must not consume another category's new bits. */
 while(PeekMessageA(msg,0,0,0,1)){}
 ck(PostMessageA(window,0x8001,4,0),17953);
 SetLastError(0x98);ck(GetQueueStatus(0)==0&&GetLastError()==0x98,17954);
 ck(GetQueueStatus(1)==0,17955);
 ck(GetQueueStatus(8)==0x00080008,17956);
 ck(GetQueueStatus(8)==0x00080000,17957);
 ck(GetQueueStatus(0x100)==0x01000100,17958);
 ck(GetQueueStatus(0x100)==0x01000000,17959);
 ck(MsgWaitForMultipleObjects(0,0,0,0,8)==258&&MsgWaitForMultipleObjectsEx(0,0,0,8,4)==0,17960);
 ck(PeekMessageA(msg,0,0x8001,0x8001,1),17961);
 ck(GetQueueStatus(0x108)==0,17962);
 ck(PostMessageA(window,0x8001,5,0)&&!GetInputState(),17963);
 ck(GetQueueStatus(0x108)==0x01080000,17964);
 ck(PeekMessageA(msg,0,0,0,1),17965);
 /* Timer readiness also survives a query for a different category. */
 timer=SetTimer(window,72,10,0);ck(timer&&MsgWaitForMultipleObjects(0,0,0,100,0x10)==0,17966);
 ck(GetQueueStatus(8)==0,17967);
 ck(GetQueueStatus(0x10)==0x00100010&&GetQueueStatus(0x10)==0x00100000,17968);
 ck(PeekMessageA(msg,0,0x113,0x113,1)&&KillTimer(window,timer),17969);
 /* A query exposes pending sends without executing the target procedure. */
 phase=0;h=thread(5);for(U tries=0;phase!=50;++tries){ck(tries<1000,17987);Sleep(1);}ck(!sent_count,17970);
 ck(GetQueueStatus(8)==0&&!sent_count,17971);
 ck(GetQueueStatus(0x40)==0x00400040&&!sent_count,17972);
 ck(GetQueueStatus(0x40)==0x00400000&&!sent_count,17973);
 PeekMessageA(msg,0,0,0,0);ck(sent_count==1,17974);join(h);
 ck(GetQueueStatus(0x40)==0,17975);
 /* The native runner injects a genuine key after this title is published. */
 ShowWindow(window,5);SetFocus(window);while(PeekMessageA(msg,0,0,0,1)){DispatchMessageA(msg);}
 ck(SetWindowTextA(window,"queue-input-ready"),17976);input_ready();
 ck(GetInputState()==1&&GetInputState()==0,17977);
 ck(GetQueueStatus(1)==0x00010000,17978);
 ck(PeekMessageA(msg,0,0x100,0x100,1)&&msg[2]==0x41,17979);
 ck(GetQueueStatus(1)==0,17980);
 ck(SetWindowTextA(window,"queue-input-again"),17981);input_ready();
 ck(GetQueueStatus(0)==0&&GetQueueStatus(8)==0,17982);
 ck(GetQueueStatus(1)==0x00010001&&!GetInputState(),17983);
 ck(PeekMessageA(msg,0,0x101,0x101,1)&&msg[2]==0x41,17984);
 ck(SetWindowTextA(window,""),17985);
 U si[17]={68},pi[4]={0};char command[]="CHILD";ck(CreateProcessA("C:\\MULTIWAIT.EXE",command,0,0,0,0,0,0,si,pi),17947);a[0]=pi[0];a[1]=pi[1];ck(MsgWaitForMultipleObjects(2,a,0,~0u,0x40)<2,17948);ck(WaitForMultipleObjects(2,a,1,~0u)==0&&GetExitCodeProcess(pi[0],&v)&&v==42,17949);ck(CloseHandle(pi[0])&&CloseHandle(pi[1]),17950);
 ck(DestroyWindow(window)&&CloseHandle(event1)&&CloseHandle(event2)&&CloseHandle(sem),17951);ExitProcess(0);
}
