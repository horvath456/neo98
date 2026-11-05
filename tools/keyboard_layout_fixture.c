/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API void SetLastError(U);API U GetCurrentThreadId(void);
API U CreateThread(void*,U,U(__stdcall*)(void*),void*,U,U*);API U WaitForSingleObject(U,U);API U CloseHandle(U);
API U GetKeyboardLayout(U);API U GetKeyboardLayoutList(U,U*);API U GetKeyboardLayoutNameA(char*);API U GetKeyboardLayoutNameW(void*);API U LoadKeyboardLayoutW(void*,U);API U ActivateKeyboardLayout(U,U);API U LoadKeyboardLayoutA(const char*,U);API U UnloadKeyboardLayout(U);
API U RegisterClassA(const void*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);API U DestroyWindow(U);API U EnableWindow(U,U);API U DefWindowProcA(U,U,U,U);
static U initial,messages,worker_id;static void check(int ok,U code){if(!ok)ExitProcess(code);}
static U __stdcall proc(U h,U m,U w,U l){if(m==0x51){check(w==0&&l==initial&&GetKeyboardLayout(0)==initial,10320);++messages;return 0;}return DefWindowProcA(h,m,w,l);}
static U __stdcall worker(void*p){(void)p;worker_id=GetCurrentThreadId();check(GetKeyboardLayout(0)==initial&&GetKeyboardLayout(worker_id)==initial,10321);check(!ActivateKeyboardLayout(initial,0),10322);return 0;}
void run_keyboard_layout_test(void){
 initial=GetKeyboardLayout(0);check(initial==0x04070407||initial==0x04090409,10300);check(GetKeyboardLayout(GetCurrentThreadId())==initial&&GetKeyboardLayout(0x12340001)==initial&&!GetKeyboardLayout(0xffffffff),10301);
 char name[16];for(U i=0;i<16;++i)name[i]='!';check(GetKeyboardLayoutNameA(name),10302);const char*expected=initial==0x04070407?"00000407":"00000409";for(U i=0;i<9;++i)check(name[i]==expected[i],10303);check(name[9]=='!',10304);
 U layouts[4]={0x11223344,0x55667788,0xaabbccdd,0xeeff7788};check(GetKeyboardLayoutList(0,0)==1&&GetKeyboardLayoutList(13,0)==1,10305);check(GetKeyboardLayoutList(0,layouts)==0&&layouts[0]==0x11223344,10306);check(GetKeyboardLayoutList(4,layouts)==1&&layouts[0]==initial&&layouts[1]==0x55667788,10307);check(GetKeyboardLayoutList(0x10000,layouts)==0&&GetKeyboardLayoutList(0x10001,layouts)==1,10308);
 SetLastError(99);check(!GetKeyboardLayoutNameW((void*)1)&&GetLastError()==120,10309);SetLastError(99);check(!LoadKeyboardLayoutW((void*)1,0xffffffff)&&GetLastError()==120,10310);
 check(!ActivateKeyboardLayout(initial,0),10311);
 check(LoadKeyboardLayoutA(expected,0)==initial&&LoadKeyboardLayoutA(expected,8)==initial&&GetKeyboardLayoutList(0,0)==1,10323);
 SetLastError(99);check(!LoadKeyboardLayoutA("0000040c",0)&&GetLastError()==50,10324);check(!UnloadKeyboardLayout(initial)&&!UnloadKeyboardLayout(0x99990409),10325);
 U klass[10]={0,(U)proc,0,0,0,0,0,0,0,(U)"KeyboardLayoutTest"};check(RegisterClassA(klass),10312);U h=CreateWindowExA(0,"KeyboardLayoutTest","Keyboard layout",0x08cf0000,10,10,240,120,0,0,0,0);check(h&&!ActivateKeyboardLayout(initial,0),10313);EnableWindow(h,1);
 check(ActivateKeyboardLayout(initial,0)==initial&&messages==1,10314);check(ActivateKeyboardLayout(initial&65535,8)==initial&&messages==2,10315);check(ActivateKeyboardLayout(1,0)==initial&&ActivateKeyboardLayout(0,8)==initial&&messages==4,10316);check(!ActivateKeyboardLayout(0x99990409,0)&&messages==4,10317);
 check(LoadKeyboardLayoutA(expected,1)==initial&&messages==5&&GetKeyboardLayout(0)==initial,10326);
 U tid=0,t=CreateThread(0,0,worker,0,0,&tid);check(t&&tid&&WaitForSingleObject(t,1000)==0&&tid==worker_id&&CloseHandle(t),10318);check(!GetKeyboardLayout(tid)&&GetKeyboardLayout(0)==initial&&DestroyWindow(h),10319);
}
#ifdef KEYBOARD_LAYOUT_STANDALONE
void mainCRTStartup(void){run_keyboard_layout_test();ExitProcess(0);}
#endif
