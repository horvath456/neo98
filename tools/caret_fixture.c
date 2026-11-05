/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API void Sleep(U);API U GetTickCount(void);API U GetLastError(void);API void SetLastError(U);
API U RegisterClassA(const void*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);API U DefWindowProcA(U,U,U,U);API U DestroyWindow(U);API U ShowWindow(U,int);API U SetFocus(U);API U SendMessageA(U,U,U,U);API U ValidateRect(U,const void*);API U GetDC(U);API U ReleaseDC(U,U);API U GetPixel(U,int,int);API U FillRect(U,const void*,U);
API U CreateCaret(U,U,int,int);API U DestroyCaret(void);API U ShowCaret(U);API U HideCaret(U);API U SetCaretPos(int,int);API U GetCaretPos(void*);API U SetCaretBlinkTime(U);API U GetCaretBlinkTime(void);API U PeekMessageA(void*,U,U,U,U);
API U GetObjectA(U,U,void*);API U CreateSolidBrush(U);API U DeleteObject(U);API U CreateBitmap(int,int,U,U,const void*);API U GetModuleHandleA(const char*);API U GetProcAddress(U,const char*);API U CreateThread(void*,U,void*,void*,U,U*);API U WaitForSingleObject(U,U);API U CloseHandle(U);
static void check(int ok,U code){if(!ok)ExitProcess(code);}static U brush,window,other,worker_window,worker_bitmap;
static U __stdcall procedure(U h,U m,U w,U l){return DefWindowProcA(h,m,w,l);}
static U make(const char*name,int x){U h=CreateWindowExA(0,name,"",0x80000000,x,10,80,60,0,0,0,0);check(h,12700);ShowWindow(h,8);int r[4]={0,0,80,60};U dc=GetDC(h);check(dc&&FillRect(dc,r,brush)&&ReleaseDC(h,dc)&&ValidateRect(h,0),12701);return h;}
static U pixel(int x,int y){U dc=GetDC(0);check(dc,12702);U p=GetPixel(dc,x,y);check(ReleaseDC(0,dc),12703);return p;}
static U __stdcall worker(void*p){(void)p;worker_window=make("CaretFixture",210);check(CreateCaret(worker_window,worker_bitmap,1,6)&&SetCaretPos(3,3)&&ShowCaret(worker_window),12740);return 0;}
void run_caret_test(void){
 brush=CreateSolidBrush(0xffffff);U c[10]={0,(U)procedure,0,0,0,0,0,brush,0,(U)"CaretFixture"};check(brush&&RegisterClassA(c),12704);window=make("CaretFixture",10);other=make("CaretFixture",110);
 int p[2]={123,456};check(GetCaretPos(p)&&!p[0]&&!p[1],12705);check(!DestroyCaret()&&!ShowCaret(window)&&!HideCaret(0)&&!SetCaretPos(1,2),12706);
 check(SetCaretBlinkTime(0x1ea60)&&GetCaretBlinkTime()==60000,12707);check(CreateCaret(window,0,2,8)&&SetCaretPos(6,7),12708);
 check(pixel(16,17)==0xffffff,12709);check(HideCaret(window)&&ShowCaret(0)&&pixel(16,17)==0xffffff,12710);check(ShowCaret(window)&&pixel(16,17)==0,12711);check(ShowCaret(window)&&pixel(16,17)==0,12712);
 check(!HideCaret(other)&&HideCaret(window)&&pixel(16,17)==0xffffff,12713);check(HideCaret(window)&&ShowCaret(window)&&pixel(16,17)==0xffffff&&ShowCaret(window)&&pixel(16,17)==0,12714);
 check(SetCaretPos(9,9)&&pixel(16,17)==0xffffff&&pixel(19,19)==0,12715);check(SetCaretPos(0x18000,0xffff)&&GetCaretPos(p)&&p[0]==-32768&&p[1]==-1,12716);check(SetCaretPos(6,7),12717);
 check(!CreateCaret(window,brush,1,1)&&HideCaret(window)&&ShowCaret(window),12718);
 check(CreateCaret(other,1,4,4)&&GetCaretPos(p)&&p[0]==6&&p[1]==7,12719);check(!HideCaret(window)&&ShowCaret(other),12720);check(pixel(116,17)!=pixel(117,17),12721);check(DestroyCaret()&&!DestroyCaret(),12722);
 unsigned char bits[4]={0x80,0,0x40,0};U bitmap=CreateBitmap(2,2,1,1,bits);U object[6];check(bitmap&&CreateCaret(window,bitmap,99,99)&&DeleteObject(bitmap)&&GetObjectA(bitmap,24,object)==24,12723);check(SetCaretPos(6,7)&&ShowCaret(window)&&pixel(16,17)==0&&pixel(17,17)==0xffffff&&pixel(17,18)==0,12724);check(DestroyCaret()&&DeleteObject(bitmap),12725);
 check(CreateCaret(window,0,0,0)&&SetCaretPos(6,7)&&ShowCaret(window)&&pixel(16,17)==0&&pixel(17,17)==0xffffff,12726);
 check(SetCaretBlinkTime(20),12727);U initial=pixel(16,17),start=GetTickCount();int changed=0;for(U i=0;i<100&&GetTickCount()-start<1000;++i){Sleep(5);if(pixel(16,17)!=initial){changed=1;break;}}check(changed,12728);U message[7];check(!PeekMessageA(message,0,0x113,0x113,1),12729);check(SetCaretBlinkTime(60000)&&DestroyCaret(),12730);
 /* Single-line EDIT uses its one-pixel inset, not the multiline border inset. */
 // A borderless Win98 EDIT starts its formatting area and caret at (0,0).
 U edit=CreateWindowExA(0,"EDIT","",0x50000000,5,25,65,20,window,44,0,0);int format[4]={-1,-1,-1,-1};
 check(edit&&SetFocus(edit)!=0xffffffff,12731);SendMessageA(edit,0xb2,0,(U)format);
 check(format[0]==0&&format[1]==0&&GetCaretPos(p)&&p[0]==format[0]&&p[1]==format[1],12731);
 SendMessageA(edit,0x102,'A',1);pixel(16,17);check(GetCaretPos(p)&&p[0]>format[0]&&p[1]==format[1],12732);SetFocus(window);check(!DestroyCaret(),12733);
 check(CreateCaret(window,0,1,4)&&ShowCaret(window),12734);worker_bitmap=CreateBitmap(2,2,1,1,bits);check(worker_bitmap,12741);U id=0,thread=CreateThread(0,0,(void*)worker,0,0,&id);check(thread&&WaitForSingleObject(thread,1000)==0&&CloseHandle(thread),12735);check(!DestroyCaret()&&DeleteObject(worker_bitmap)&&!GetObjectA(worker_bitmap,24,object),12736);
 check(CreateCaret(window,0,1,4)&&DestroyWindow(window)&&!ShowCaret(window)&&!DestroyCaret(),12737);check(DestroyWindow(other)&&DeleteObject(brush),12738);
 U module=GetModuleHandleA("USER32.DLL");typedef U(__stdcall*Blink)(void);Blink get=(Blink)GetProcAddress(module,(const char*)224);check(get&&get()==60000,12739);
}
#ifdef CARET_STANDALONE
void mainCRTStartup(void){run_caret_test();ExitProcess(0);}
#endif
