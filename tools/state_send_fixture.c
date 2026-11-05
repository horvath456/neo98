// SPDX-License-Identifier: GPL-2.0-or-later
// Real sender/receiver threads and processes with a send open at a save point.
#define API __declspec(dllimport)
#define S __stdcall
typedef unsigned U;
struct Msg {U hwnd,message,wp,lp,time;int x,y;};
struct WndClass {U style,proc,cbcls,cbwin,instance,icon,cursor,brush,menu,name;};
API void S ExitProcess(U); API void S ExitThread(U); API void S Sleep(U);
API char*S GetCommandLineA(void); API U S GetModuleHandleA(const char*);
API U S CreateThread(void*,U,U(S*)(void*),void*,U,U*);
API U S CreateProcessA(const char*,char*,U,U,U,U,const char*,const char*,U*,U*);
API U S WaitForInputIdle(U,U);
API U S GetLastError(void); API U S CloseHandle(U); API U S TerminateProcess(U,U);
API U S CreateFileA(const char*,U,U,U,U,U,U); API U S WriteFile(U,const void*,U,U*,U);
API U S ReadFile(U,void*,U,U*,U); API U S GetFileAttributesA(const char*);
API U S RegisterClassA(struct WndClass*);
API U S CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);
API U S FindWindowA(const char*,const char*);
API U S DefWindowProcA(U,U,U,U); API U S DestroyWindow(U);
API U S GetMessageA(struct Msg*,U,U,U); API U S DispatchMessageA(struct Msg*);
API U S SendMessageA(U,U,U,U); API U S SendMessageTimeoutA(U,U,U,U,U,U,U*);
API U S SetTimer(U,U,U,U); API U S KillTimer(U,U);
API U S MessageBoxA(U,const char*,const char*,U); API U S SetWindowTextA(U,const char*);
API U S SetFocus(U); API U S SetForegroundWindow(U);
API U S InSendMessageEx(void*); API U S SetMessageExtraInfo(U); API U S GetMessageExtraInfo(void);
API U wsprintfA(char*,const char*,...);
static volatile U main_window,target,entered,finished,back_entered,back_finished;
static U child,mode,send_flags,extra,back_result,copied,child_process;
static const char *const payload="copied-string";
#define ASK 0x9001
#define BACK 0x9002
static void text_file(const char*name,const char*text){
 U n=0,len=0,h=CreateFileA(name,0x40000000,3,0,2,0,0);while(text[len])++len;
 if(h==~0u||!WriteFile(h,text,len,&n,0)||n!=len||!CloseHandle(h))ExitProcess(29801);
}
static U equal(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static void receiver_report(void){char line[256];
 wsprintfA(line,"entered=%u\nfinished=%u\nback_entered=%u\nback_finished=%u\nback_result=%u\ncopied=%u\nsend_flags=%u\nextra=%u\n",entered,finished,back_entered,back_finished,back_result,copied,send_flags,extra);
 text_file("D:\\RECEIVER.TXT",line);
}
static U S proc(U h,U m,U w,U l){
 (void)w;
 if(m==0x113&&child&&mode=='A')ExitProcess(77);
 if(m==0x113&&w==7&&!child&&mode=='Z'){if(!TerminateProcess(child_process,77))ExitProcess(29812);return 0;}
 if(m==0x113&&w==2&&!child){
  if(mode=='X'&&!finished)return 0;
  if(child_process&&(mode=='C'||mode=='G'||mode=='S'||mode=='V')){if(!TerminateProcess(child_process,0))ExitProcess(29818);}
  if(child_process&&(mode=='C'||mode=='G'||mode=='S'||mode=='V'||mode=='Z'))CloseHandle(child_process);
  ExitProcess(0);
 }
 if(m==ASK||m==0x4a||(mode=='G'&&child&&m==0xd&&w==32&&l)||(mode=='S'&&child&&m==0xc&&l&&equal((const char*)l,payload))){
  ++entered;send_flags=InSendMessageEx(0);extra=GetMessageExtraInfo();
  const char*data=0;
  if(m==0x4a){U*copy=(U*)l;if(copy[0]!=0x12345678||copy[1]!=14)ExitProcess(29802);data=(const char*)copy[2];}
  if(m==0xc)data=(const char*)l;
  if(data&&!equal(data,payload))ExitProcess(29803);
  text_file("D:\\ENTERED.TXT","entered=1\n");
  if(mode=='A'&&!SetTimer(h,7,5000,0))ExitProcess(29811);
  if(mode=='N'||mode=='Z')back_result=SendMessageA(mode=='Z'?w:main_window,BACK,0,0);
  else MessageBoxA(h,"A received send is still open.","SendStateBox",0);
  if(data){if(!equal(data,payload))ExitProcess(29804);copied=1;}
  if(mode=='G'){if(w<14)ExitProcess(29820);for(U i=0;i<14;++i)((char*)l)[i]=payload[i];copied=1;}
  ++finished;receiver_report();return mode=='G'?13:0x1234;
 }
 if(m==BACK){++back_entered;if(mode=='Z'&&!SetTimer(h,7,5000,0))ExitProcess(29813);MessageBoxA(h,"Both threads are sending.","SendStateBox",0);++back_finished;return 0x5678;}
 if((m==0x100&&l!=0xffffffffu||m==0x113&&w==1)&&h==main_window&&!child){
  if(w=='S'||m==0x113){
   KillTimer(main_window,1);
   U answer=0xfeedface,result=0,complete=1,copy[3]={0x12345678,14,(U)payload};
   char received[32];for(U i=0;i<31;++i)received[i]='?';received[31]=0;
   SetMessageExtraInfo(0x12345678);
   if(mode=='T'||mode=='X'){
    complete=SendMessageTimeoutA(target,ASK,0,0,0,mode=='T'?50000:5000,&answer);
    result=answer;
   }else if(mode=='G'){
    result=SendMessageA(target,0xd,sizeof received,(U)received);
    if(!equal(received,payload)||received[14]!='?'||received[30]!='?')ExitProcess(29821);
   }else result=SendMessageA(target,mode=='C'?0x4a:mode=='S'?0xc:mode=='V'?0x100:ASK,mode=='Z'?main_window:mode=='V'?'M':0,mode=='C'?(U)copy:mode=='S'?(U)payload:0);
   char line[256];wsprintfA(line,"result=%u\ncomplete=%u\nentered=%u\nfinished=%u\nback_entered=%u\nback_finished=%u\nextra=%u\nerror=%u\n",result,complete,entered,finished,back_entered,back_finished,GetMessageExtraInfo(),GetLastError());
   text_file("D:\\ANSWER.TXT",line);SetWindowTextA(main_window,"SendStateDone");SetForegroundWindow(main_window);SetFocus(main_window);if(!SetTimer(main_window,2,500,0))ExitProcess(29819);
  }
  if(w=='Q')ExitProcess(0);
  return 0;
 }
 return DefWindowProcA(h,m,w,l);
}
static U make_window(const char*title,U visible){
 U h=CreateWindowExA(0,"SendStateClass",title,visible?0x10cf0000:0xcf0000,80,80,400,220,0,0,GetModuleHandleA(0),0);
 if(!h)ExitProcess(29805);return h;
}
static void loop(void){struct Msg msg;while(GetMessageA(&msg,0,0,0))DispatchMessageA(&msg);}
static U S receiver(void*arg){
 (void)arg;target=make_window("SendStateReceiver",0);
 if(mode=='I'){
  char command[]="D:\\SENDSTATE.EXE child I";U startup[17]={68},process[4]={0};
  if(!CreateProcessA("D:\\SENDSTATE.EXE",command,0,0,0,0,0,0,startup,process))ExitProcess(29816);
  WaitForInputIdle(process[0],60000);
  CloseHandle(process[0]);CloseHandle(process[1]);
 }else if(mode=='W'||mode=='E'){
  Sleep(5000);
  if(mode=='W'){DestroyWindow(target);Sleep(30000);}
  else ExitThread(0);
 }else loop();
 return 0;
}
void mainCRTStartup(void){
 char*cmd=GetCommandLineA();U length=0;while(cmd[length])++length;mode=length?cmd[length-1]:'B';
 for(U i=0;i+5<length;++i)if(cmd[i]==' '&&cmd[i+1]=='c'&&cmd[i+2]=='h'&&cmd[i+3]=='i'&&cmd[i+4]=='l'&&cmd[i+5]=='d')child=1;
 struct WndClass cls={0,(U)proc,0,0,GetModuleHandleA(0),0,0,0,0,(U)"SendStateClass"};
 if(!RegisterClassA(&cls))ExitProcess(29806);
 if(child){
  target=make_window("SendStateReceiver",0);U n,h=CreateFileA("D:\\TARGET.DAT",0x40000000,3,0,2,0,0);
  if(h==~0u||!WriteFile(h,(const void*)&target,4,&n,0)||n!=4||!CloseHandle(h))ExitProcess(29807);
  if(mode=='P'||mode=='I'){Sleep(mode=='I'?60000:5000);ExitProcess(0);}else loop();ExitProcess(0);
 }
 main_window=make_window("SendStateStarting",1);
 if(mode=='V'){
  char command[]="D:\\W16.EXE";U startup[17]={68},process[4]={0};
  if(!CreateProcessA("D:\\W16.EXE",command,0,0,0,0,0,0,startup,process))ExitProcess(29814);
  child_process=process[0];CloseHandle(process[1]);
  for(U i=0;i<2000&&!target;++i){target=FindWindowA(0,"State fixture 16");if(!target)Sleep(10);}
  if(!target)ExitProcess(29815);
 }else if(mode=='C'||mode=='G'||mode=='S'||mode=='P'||mode=='A'||mode=='Z'){
  char command[]="D:\\SENDSTATE.EXE child ?";U startup[17]={68},process[4]={0};command[sizeof(command)-2]=mode;
  if(!CreateProcessA("D:\\SENDSTATE.EXE",command,0,0,0,0,0,0,startup,process))ExitProcess(29808);
  child_process=process[0];if(mode!='Z'&&mode!='C'&&mode!='G'&&mode!='S')CloseHandle(process[0]);CloseHandle(process[1]);
  for(U i=0;i<2000&&GetFileAttributesA("D:\\TARGET.DAT")==~0u;++i)Sleep(10);
  U n,h=CreateFileA("D:\\TARGET.DAT",0x80000000,3,0,3,0,0);
  if(h==~0u||!ReadFile(h,(void*)&target,4,&n,0)||n!=4||!CloseHandle(h))ExitProcess(29809);
 }else{
  U id;if(!CreateThread(0,0,receiver,0,0,&id))ExitProcess(29810);
  while(!target)Sleep(10);
 }
 SetWindowTextA(main_window,"SendStateFixture");SetForegroundWindow(main_window);SetFocus(main_window);if(!SetTimer(main_window,1,700,0))ExitProcess(29817);loop();ExitProcess(0);
}
