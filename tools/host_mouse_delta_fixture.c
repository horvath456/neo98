/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Reads WM_MOUSEMOVE relative to a cursor reset each timer tick, like DINPUT's
 * mouse emulation. The host test supplies positions queued before a reset. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U RegisterClassA(const void *);
API U CreateWindowExA(U,const char *,const char *,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);
API U GetMessageA(void *,U,U,U);
API U DispatchMessageA(const void *);
API U SetCursorPos(int,int);
API U SetTimer(U,U,U,U);
API U SetWindowTextA(U,const char *);
API U GetModuleHandleA(const char *);
API void ExitProcess(U);
static int dx,dy,holding=1;
static char *number(char *s,int n) {
    char digits[12]; int count=0;
    if(n<0){*s++='-';n=-n;}
    do{digits[count++]=(char)('0'+n%10);n/=10;}while(n);
    while(count)*s++=digits[--count];
    return s;
}
static U __stdcall proc(U h,U m,U w,U l) {
    if(m==0x113 && holding)SetCursorPos(400,300);
    if(m==0x200){dx+=(short)l-400;dy+=(short)(l>>16)-300;}
    if(m==0x100 && w=='Z'){dx=dy=0;SetWindowTextA(h,"delta ready");}
    if(m==0x100 && w=='P'){
        char text[40]="delta "; char *p=number(text+6,dx);
        *p++=',';p=number(p,dy);*p=0;SetWindowTextA(h,text);
    }
    if(m==0x100 && w=='H'){holding=0;SetWindowTextA(h,"delta free");}
    if(m==2)ExitProcess(0);
    return DefWindowProcA(h,m,w,l);
}
void mainCRTStartup(void){
    U c[10]={0},msg[8],h;
    c[1]=(U)proc;c[4]=GetModuleHandleA(0);c[9]=(U)"HOST_MOUSE_DELTA";
    if(!RegisterClassA(c))ExitProcess(2);
    h=CreateWindowExA(0,(const char *)c[9],"delta ready",0x90000000,0,0,800,600,0,0,c[4],0);
    if(!h)ExitProcess(3);
    SetTimer(h,1,20,0);
    while(GetMessageA(msg,0,0,0))DispatchMessageA(msg);
    ExitProcess(0);
}
