/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U RegisterClassA(const void *);
API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);
API U DefWindowProcA(U,U,U,U);
API U SetFocus(U);
API U GetFocus(void);
API U GetActiveWindow(void);
API U GetForegroundWindow(void);
API void ExitProcess(U);
static U root,child,root_focus,child_focus;
static U __stdcall Proc(U h,U m,U w,U l) {
  if(m==7) {if(h==root) ++root_focus; else if(h==child) ++child_focus; return 0;}
  if(m==6) return 0; /* Keep default activation from choosing a focus itself. */
  return DefWindowProcA(h,m,w,l);
}
void mainCRTStartup(void) {
  struct {U style;void *proc;U cls,win,instance,icon,cursor,brush;const char *menu,*name;} c={0,Proc,0,0,0,0,0,0,0,"FocusForeground"};
  if(!RegisterClassA(&c)) ExitProcess(1);
  root=CreateWindowExA(0,c.name,"Hidden popup",0x80000000,0,0,100,100,0,0,0,0);
  child=CreateWindowExA(0,c.name,"Child",0x50000000,0,0,20,20,root,0,0,0);
  if(!root||!child) ExitProcess(2);
  SetFocus(child);
  if(GetFocus()!=child||GetActiveWindow()!=root||GetForegroundWindow()!=root) ExitProcess(3);
  if(root_focus||child_focus!=1) ExitProcess(4);
  ExitProcess(0);
}
