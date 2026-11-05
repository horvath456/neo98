// SPDX-License-Identifier: GPL-2.0-or-later
// Cross-process pending-send lifetime tests: receiver exit, thread exit,
// window destruction and sender exit must not leave a live sender blocked.
// The parent uses role P; child roles identify each case.
#define API __declspec(dllimport)
#define S __stdcall
typedef unsigned U;
struct Msg{U hwnd,message,wp,lp,time;int x,y;};
struct WndClass{U style,proc,cbcls,cbwin,instance,icon,cursor,brush,menu,name;};
API void S ExitProcess(U);API void S ExitThread(U);API void S Sleep(U);
API char*S GetCommandLineA(void);API U S GetLastError(void);API void S SetLastError(U);
API U S CreateProcessA(const char*,char*,U,U,U,U,const char*,const char*,U*,U*);
API U S WaitForSingleObject(U,U);API U S GetExitCodeProcess(U,U*);API U S CloseHandle(U);
API U S CreateThread(void*,U,U(S*)(void*),void*,U,U*);
API U S CreateFileA(const char*,U,U,U,U,U,U);API U S WriteFile(U,const void*,U,U*,U);
API U S ReadFile(U,void*,U,U*,U);API U S GetFileAttributesA(const char*);API U S DeleteFileA(const char*);
API U S RegisterClassA(struct WndClass*);API U S CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);
API U S DestroyWindow(U);API U S DefWindowProcA(U,U,U,U);API U S SendMessageA(U,U,U,U);
API U S SendMessageTimeoutA(U,U,U,U,U,U,U*);
API U S PeekMessageA(struct Msg*,U,U,U,U);API U S GetMessageA(struct Msg*,U,U,U);
API U S DispatchMessageA(struct Msg*);API void S PostQuitMessage(U);API U S GetQueueStatus(U);

#define PROGRAM "C:\\SEND\\SENDLIFE.EXE"
#define CLASS "Neo98SendLifetime"
#define WM_ASK 0x9001
#define WM_SLOW 0x9002
#define WM_BACK 0x9003
static const char *const child_file="C:\\WINDOWS\\TEMP\\SL-CHILD.DAT";
static const char *const parent_file="C:\\WINDOWS\\TEMP\\SL-PARENT.DAT";
static const char *const go_file="C:\\WINDOWS\\TEMP\\SL-GO.DAT";

static void write_value(const char*name,U value){U n,h=CreateFileA(name,0x40000000,3,0,2,0,0);if(h==~0u)ExitProcess(29700);if(!WriteFile(h,&value,4,&n,0)||n!=4||!CloseHandle(h))ExitProcess(29701);}
static U read_value(const char*name){U n,v=0,h=CreateFileA(name,0x80000000,3,0,3,0,0);if(h==~0u)ExitProcess(29702);if(!ReadFile(h,&v,4,&n,0)||n!=4||!CloseHandle(h))ExitProcess(29703);return v;}
static U await_file(const char*name){for(U i=0;i<20000;++i){if(GetFileAttributesA(name)!=~0u)return 1;Sleep(1);}return 0;}
// Wait for a pending send without dispatching it. GetQueueStatus reports its
// flag in the high word.
static U await_send(void){for(U i=0;i<20000;++i){if(GetQueueStatus(0x40)&0x400000)return 1;Sleep(1);}return 0;}

static volatile U role,window,failed,delivered,second_window;
static U parent_window;

static U S wndproc(U h,U m,U w,U l){
 if(m==WM_ASK){
  if(role=='E')ExitProcess(9);           // Receiver exits from its own window procedure.
  if(role=='X'){DestroyWindow(h);PostQuitMessage(0);return 0x4321;} // Destroy the target and still reply.
  return 0x1234;
 }
 if(m==WM_SLOW)return 0x5678;
 if(m==WM_BACK){++delivered;return 0x8765;}
 if(m==0x11)return 0x1111;
 return DefWindowProcA(h,m,w,l);
}
static U S owner_thread(void*p){
 (void)p;
 second_window=CreateWindowExA(0,CLASS,"second",0,0,0,8,8,0,0,0x400000,0);
 if(!second_window){failed=29710;return 1;}
 write_value(child_file,second_window);
 if(!await_send())ExitProcess(29711);
 ExitThread(0);                           // The receiver's thread and message queue end.
 return 0;
}
static U S sending_thread(void*p){
 (void)p;
 // The sender exits while its record resides in the receiver's block. The
 // call cannot return before process termination.
 SendMessageA(parent_window,WM_BACK,0,0);
 return 0;
}

static U spawn(char letter,U*process){
 char command[]="C:\\SEND\\SENDLIFE.EXE ?";U si[17]={68},pi[4]={0};
 command[sizeof command-2]=letter;
 if(!CreateProcessA(PROGRAM,command,0,0,0,0,0,0,si,pi))return 0;
 *process=pi[0];if(!CloseHandle(pi[1]))return 0;
 return 1;
}
// Start the child, wait for its window, send, verify the result and await
// child exit.
static U one_case(char letter,U message,U expected,U timeout,U exit_code){
 U process=0,code=0;
 DeleteFileA(child_file);DeleteFileA(go_file);
 if(!spawn(letter,&process))return 29720;
 if(!await_file(child_file))return 29721;
 U target=read_value(child_file),answer=0xfeedface,result;
 SetLastError(0);
 if(timeout){
  result=SendMessageTimeoutA(target,message,0,0,0,timeout,&answer);
  if(result||answer!=0xfeedface)return 29722;
 }
 else{
  result=SendMessageA(target,message,0,0);
  if(result!=expected)return 0x40000000|(result&0xffff)|((U)letter<<16);
 }
 // Receiver-thread exit alone must release the sender even while the process
 // remains alive.
 if(letter=='T'&&(!GetExitCodeProcess(process,&code)||code!=259))return 29724;
 // Release the child only after receiving the reply. These children destroyed
 // their window or never pump; missing cancellation would deadlock both
 // sides.
 write_value(go_file,1);
 if(WaitForSingleObject(process,20000))return 29725;
 if(!GetExitCodeProcess(process,&code)||code!=exit_code)return 29726;
 if(!CloseHandle(process))return 29727;
 return 0;
}

static U parent(void){
 struct WndClass c={0};c.proc=(U)wndproc;c.instance=0x400000;c.name=(U)CLASS;
 if(!RegisterClassA(&c))return 29730;
 U own=CreateWindowExA(0,CLASS,"parent",0,0,0,8,8,0,0,0x400000,0);
 if(!own)return 29731;
 write_value(parent_file,own);
 U error;
 // The receiver exits while handling the send.
 if((error=one_case('E',WM_ASK,0,0,9)))return error;
 // The receiver never pumps before calling ExitProcess.
 if((error=one_case('Q',WM_ASK,0,0,11)))return error;
 // The receiver destroys the window targeted by the pending send.
 if((error=one_case('D',WM_ASK,0,0,0)))return error;
 // The same window returns one for message 0011h.
 if((error=one_case('1',0x11,1,0,0)))return error;
 // Only the window's thread exits.
 if((error=one_case('T',WM_ASK,0,0,0)))return error;
 // A receiver that never pumps must reach the SendMessageTimeout deadline.
 if((error=one_case('O',WM_SLOW,0,30,0)))return error;
 // A window destroyed by its own procedure still returns a reply.
 if((error=one_case('X',WM_ASK,0x4321,0,0)))return error;

 // The sender exits while its record remains in this process's block.
 U process=0,code=0;
 if(!spawn('S',&process))return 29740;
 if(WaitForSingleObject(process,20000))return 29741;
 if(!GetExitCodeProcess(process,&code)||code!=3)return 29742;
 if(!CloseHandle(process))return 29743;
 // Retrieve messages only now. Windows 98 still delivers a queued send after
 // sender exit, discarding its reply. Delivery must not access the freed
 // sender.
 struct Msg m;
 for(U i=0;i<50;++i){while(PeekMessageA(&m,0,0,0,1))DispatchMessageA(&m);Sleep(1);}
 if(delivered!=1)return 29744;
 if(failed)return failed;
 if(!DestroyWindow(own))return 29745;
 return 0;
}

void mainCRTStartup(void){
 const char*line=GetCommandLineA();
 while(*line&&*line!=' ')++line;
 role=*line==' '?(U)(unsigned char)line[1]:'P';
 if(role=='P')ExitProcess(parent());
 if(role=='S'){
  parent_window=read_value(parent_file);
  if(!CreateThread(0,0,sending_thread,0,0,0))ExitProcess(29750);
  Sleep(50);
  ExitProcess(3);
 }
 struct WndClass c={0};c.proc=(U)wndproc;c.instance=0x400000;c.name=(U)CLASS;
 if(!RegisterClassA(&c))ExitProcess(29751);
 if(role=='T'){
  if(!CreateThread(0,0,owner_thread,0,0,0))ExitProcess(29752);
  if(!await_file(go_file))ExitProcess(29753);
  ExitProcess(0);
 }
 window=CreateWindowExA(0,CLASS,"child",0,0,0,8,8,0,0,0x400000,0);
 if(!window)ExitProcess(29754);
 write_value(child_file,window);
 if(role=='E'||role=='X'){
  struct Msg m;
  while(GetMessageA(&m,0,0,0))DispatchMessageA(&m);
  ExitProcess(0);
 }
 if(role=='O'){
  if(!await_file(go_file))ExitProcess(29755);
  ExitProcess(0);
 }
 if(!await_send())ExitProcess(29756);
 if(role=='Q')ExitProcess(11);
 if(role=='D'||role=='1'){
  if(!DestroyWindow(window))ExitProcess(29757);
  if(!await_file(go_file))ExitProcess(29759);
  ExitProcess(0);
 }
 ExitProcess(29758);
}
