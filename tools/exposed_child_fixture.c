// SPDX-License-Identifier: GPL-2.0-or-later
// A parent without WS_CLIPCHILDREN paints under a child that paints nothing
// for tools/test_exposed_child_host.py.
//
// The autorun window of Plus! 98 draws its whole picture itself; its two
// buttons answer WM_ERASEBKGND and WM_PAINT without drawing. Whatever
// uncovers such a child (a window above it that goes away, a change of the
// display mode) must make the parent paint there again. The parent here is
// red, the child draws nothing, a blue window covers both and goes; then the
// desktop changes to 1024x768. After each step the pixel in the middle of
// the child must be red. One line per step in C:\EXPOSED.TXT.
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U RegisterClassA(const void*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);
API U DefWindowProcA(U,U,U,U);API U BeginPaint(U,void*);API U EndPaint(U,const void*);API U FillRect(U,const int*,U);
API U PeekMessageA(void*,U,U,U,U);API U DispatchMessageA(const void*);API U DestroyWindow(U);API U GetDC(U);
API U ReleaseDC(U,U);API U GetWindowRect(U,int*);API U GetClientRect(U,int*);API int ChangeDisplaySettingsA(void*,U);
API U GetClassLongA(U,int);API U CreateSolidBrush(U);API U GetPixel(U,int,int);API U GetModuleHandleA(const char*);API void Sleep(U);
API U CreateFileA(const char*,U,U,void*,U,U,U);API U WriteFile(U,const void*,U,U*,void*);API U CloseHandle(U);
API U SetFilePointer(U,int,int*,U);API void ExitProcess(U);
static void note(const char*s){
 U h=CreateFileA("C:\\EXPOSED.TXT",0x40000000,3,0,4,0x80,0),n=0,done=0;
 if(h==~0u)return;while(s[n])++n;SetFilePointer(h,0,0,2);WriteFile(h,s,n,&done,0);CloseHandle(h);
}
static U red,blue;
static U __stdcall filled(U h,U m,U w,U l){
 if(m==0x14)return 1;
 if(m==0xf){int ps[16],r[4];U dc=BeginPaint(h,ps);GetClientRect(h,r);FillRect(dc,r,GetClassLongA(h,-20)?blue:red);EndPaint(h,ps);return 0;}
 return DefWindowProcA(h,m,w,l);
}
static U __stdcall silent(U h,U m,U w,U l){
 if(m==0x14)return 0;
 if(m==0xf){int ps[16];BeginPaint(h,ps);EndPaint(h,ps);return 0;}
 return DefWindowProcA(h,m,w,l);
}
static void pump(void){int message[8];for(U i=0;i<60;++i){while(PeekMessageA(message,0,0,0,1))DispatchMessageA(message);Sleep(10);}}
static U middle(U child){int r[4];U dc=GetDC(0),c;GetWindowRect(child,r);c=GetPixel(dc,(r[0]+r[2])/2,(r[1]+r[3])/2);ReleaseDC(0,dc);return c;}
static void step(const char*name,U child){note(name);note(middle(child)==0x0000ff?" red\r\n":" not painted\r\n");}
void mainCRTStartup(void){
 U instance=GetModuleHandleA(0),wc[10]={0};static unsigned char mode[156];
 red=CreateSolidBrush(0x0000ff);blue=CreateSolidBrush(0xff0000);
 wc[1]=(U)filled;wc[4]=instance;wc[9]=(U)"ExposedParent";RegisterClassA(wc);
 wc[9]=(U)"ExposedCover";wc[2]=4;RegisterClassA(wc);
 wc[1]=(U)silent;wc[2]=0;wc[9]=(U)"ExposedChild";RegisterClassA(wc);
 U parent=CreateWindowExA(0,"ExposedParent","",0x90000000,50,50,200,200,0,0,instance,0);
 U child=CreateWindowExA(0,"ExposedChild","",0x50000000,20,20,100,50,parent,0,instance,0);
 pump();step("start",child);
 U cover=CreateWindowExA(0,"ExposedCover","",0x90000000,60,60,150,100,0,0,instance,0);
 pump();note(middle(child)==0xff0000?"covered blue\r\n":"covered not blue\r\n");
 DestroyWindow(cover);pump();step("uncovered",child);
 *(unsigned short*)(mode+36)=156;*(U*)(mode+40)=0x180000;*(U*)(mode+108)=1024;*(U*)(mode+112)=768;
 note(ChangeDisplaySettingsA(mode,1)==0?"mode changed\r\n":"mode refused\r\n");
 pump();step("mode",child);
 note("done\r\n");ExitProcess(0);
}
