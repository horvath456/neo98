/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API(t,n,...) __declspec(dllimport) t __stdcall n(__VA_ARGS__)
API(void,ExitProcess,U);API(U,RegisterClassA,const void*);API(U,GetModuleHandleA,const char*);API(U,CreateWindowExA,U,const char*,const char*,U,int,int,int,int,U,U,U,U);API(U,DestroyWindow,U);API(U,DefWindowProcA,U,U,U,U);API(U,SetShellWindow,U);API(U,GetShellWindow,void);API(U,GetWindow,U,U);API(U,IsWindow,U);API(U,SetWindowPos,U,U,int,int,int,int,U);API(U,SetWindowLongA,U,int,U);API(U,SetLastError,U);API(U,GetLastError,void);
static U mode,seen,wrong;
static U __stdcall proc(U h,U m,U w,U l){if(m==0x34){++seen;if(w!=4||l!=1||GetShellWindow()!=h||GetWindow(h,2))wrong=1;if(mode==1&&!SetShellWindow(0))wrong=2;if(mode==2&&!DestroyWindow(h))wrong=3;return 0;}return DefWindowProcA(h,m,w,l);}
#define CHECK(x,c) do{if(!(x))return c;}while(0)
static U make(U ex,U style,U parent){return CreateWindowExA(ex,"Neo98ShellRegistration","Shell",style,10,10,100,80,parent,0,GetModuleHandleA(0),0);}
int run_shell_registration_test(void){
 U klass[10]={0,(U)proc,0,0,0,0,0,0,0,(U)"Neo98ShellRegistration"};klass[4]=GetModuleHandleA(0);CHECK(RegisterClassA(klass),8700);CHECK(!GetShellWindow()&&SetShellWindow(0),8701);U a=make(0,0x80000000,0),b=make(0,0x80000000,0);CHECK(a&&b,8702);CHECK(SetShellWindow(a)&&GetShellWindow()==a&&seen==1&&!wrong,8703);CHECK(SetWindowPos(a,0,0,0,0,0,0x13)&&GetWindow(a,2)==0,8717);CHECK(SetWindowPos(a,~0u,0,0,0,0,0x17)&&GetWindow(a,2)==0,8718);CHECK(!SetShellWindow(a)&&!SetShellWindow(b)&&GetShellWindow()==a&&seen==1,8704);CHECK(SetShellWindow(0)&&!GetShellWindow(),8705);
 U child=make(0,0x40000000,a),owner=make(0,0x80000000,a),top=make(8,0x80000000,0);CHECK(child&&owner&&top,8706);CHECK(!SetShellWindow(child)&&!SetShellWindow(owner)&&!SetShellWindow(top)&&!GetShellWindow(),8707);CHECK(DestroyWindow(child)&&DestroyWindow(owner)&&DestroyWindow(top),8708);
 CHECK(SetShellWindow(b)&&GetShellWindow()==b&&DestroyWindow(b)&&!GetShellWindow(),8709);CHECK(!SetShellWindow(b),8710);U reused=make(0,0x80000000,0);CHECK(reused&&reused!=b&&SetShellWindow(reused)&&GetShellWindow()==reused,8711);CHECK(SetShellWindow(0),8712);mode=1;CHECK(SetShellWindow(a)&&!GetShellWindow()&&!wrong,8713);mode=2;CHECK(SetShellWindow(reused)&&!GetShellWindow()&&!IsWindow(reused)&&!wrong,8714);mode=0;CHECK(SetShellWindow(a)&&GetShellWindow()==a&&!wrong,8715);CHECK(DestroyWindow(a)&&!GetShellWindow(),8716);U altered=make(0,0x80000000,0);CHECK(altered&&SetShellWindow(altered),8719);SetWindowLongA(altered,-16,0x40000000);/* Native ownership fallback outside the exact original cleanup predicate. */CHECK(DestroyWindow(altered)&&!GetShellWindow()&&SetShellWindow(0),8720);return 0;
}
#ifdef SHELL_REGISTRATION_STANDALONE
void mainCRTStartup(void){ExitProcess(run_shell_registration_test());}
#endif
