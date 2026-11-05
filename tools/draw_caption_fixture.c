// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API void SetLastError(U);API U GetModuleHandleA(const char*);API U GetProcAddress(U,const char*);
API U CreateCompatibleDC(U);API U CreateDIBSection(U,const void*,U,void**,U,U);API U SelectObject(U,U);API U DeleteObject(U);API U DeleteDC(U);API U GetStockObject(U);API U GetCurrentObject(U,U);API U SetBkMode(U,U);API U SetTextColor(U,U);API U SetBkColor(U,U);API U GetPixel(U,int,int);API U GetSysColor(U);
API U DrawCaption(U,U,const void*,U);API U DrawCaptionTempA(U,U,const void*,U,U,const char*,U);API U LoadIconA(U,const char*);
API U RegisterClassA(const void*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);API U DefWindowProcA(U,U,U,U);API U DestroyWindow(U);
struct Rect{int left,top,right,bottom;};struct Info{U size;int width,height;unsigned short planes,depth;U compression,bytes;int x,y;U used,important;};
static void check(U okay,U code){if(!okay)ExitProcess(code);}static U text_calls;
static U __stdcall window(U h,U m,U w,U l){if(m==13){char*p=(char*)l;const char*s="Callback title";U i=0;++text_calls;if(w){while(i+1<w&&s[i]){p[i]=s[i];++i;}p[i]=0;}return i;}return DefWindowProcA(h,m,w,l);}
void run_draw_caption_test(void){
 U dc=CreateCompatibleDC(0),font=GetStockObject(13);check(dc&&font,28800);struct Info info={40,160,-64,1,32,0,40960,0,0,0,0};void*bits=0;U bitmap=CreateDIBSection(dc,&info,0,&bits,0,0);check(bitmap&&bits,28801);U old=SelectObject(dc,bitmap),oldfont=SelectObject(dc,font);check(old&&oldfont,28802);
 SetBkMode(dc,2);SetTextColor(dc,0x123456);SetBkColor(dc,0x345678);
 struct Rect r={2,2,158,22};check(DrawCaptionTempA(0,dc,&r,font,0,"Caption",9),28803);check(GetPixel(dc,150,3)==GetSysColor(2),28804);
 check(r.left==2&&r.top==2&&r.right==158&&r.bottom==22,28805);check(GetCurrentObject(dc,6)==font,28806);check(SetBkMode(dc,2)==2,28807);check(SetTextColor(dc,0x123456)==0x123456&&SetBkColor(dc,0x345678)==0x345678,28808);
 U white=GetSysColor(9),glyphs=0;for(int x=4;x<55;++x)if(GetPixel(dc,x,12)==white)++glyphs;check(glyphs,28809);
 r=(struct Rect){2,24,35,44};check(!DrawCaptionTempA(0,dc,&r,font,0,"A very long caption",9),28810);check(GetPixel(dc,33,25)==GetSysColor(2),28811);
 r=(struct Rect){2,44,158,64};check(DrawCaptionTempA(0,dc,&r,font,0,"Button",0x19),28812);U p=GetPixel(dc,148,45),q=GetPixel(dc,149,45);check(p!=q&&(p==GetSysColor(15)||p==GetSysColor(20))&&(q==GetSysColor(15)||q==GetSysColor(20)),28813);
 r=(struct Rect){2,2,158,22};check(DrawCaptionTempA(0,dc,&r,font,0,"Gradient",0x29),28814);check(GetPixel(dc,3,3)!=GetPixel(dc,155,3),28815);
 U icon=LoadIconA(0,(const char*)32512);check(icon,28816);check(DrawCaptionTempA(0,dc,&r,font,icon,"Icon",0x201c),28817);check(r.left==2&&r.right==158,28818);
 typedef U(__stdcall*Caption)(U,U,const void*,U,U,const char*,U);Caption byordinal=(Caption)GetProcAddress(GetModuleHandleA("USER32.DLL"),(const char*)161);check(byordinal&&byordinal(0,dc,&r,font,0,"Ordinal",9),28819);
 typedef U(__stdcall*Wide)(U,U,U,U,U,U);Wide wide=(Wide)GetProcAddress(GetModuleHandleA("USER32.DLL"),"DrawCaptionTempW");SetLastError(98);check(wide&&!wide(0xdeadc0de,0xdeadc0de,0xdeadc0de,0xdeadc0de,0xdeadc0de,0xdeadc0de)&&GetLastError()==120,28820);
 SetLastError(0);check(!DrawCaptionTempA(0,dc,&r,font,0,"Invalid",0x40)&&GetLastError()==87,28821);
 U instance=GetModuleHandleA(0),klass[]={0,(U)window,0,0,instance,0,0,0,0,(U)"CaptionFixture"};check(RegisterClassA(klass),28822);
 U hwnd=CreateWindowExA(0,"CaptionFixture","Stored title",0,0,0,100,40,0,0,instance,0);check(hwnd,28823);U before=text_calls;check(DrawCaption(hwnd,dc,&r,9)&&text_calls>before,28824);
 before=text_calls;check(DrawCaptionTempA(hwnd,dc,&r,font,0,0,0x2009)&&text_calls==before,28825);check(DestroyWindow(hwnd),28826);
 SelectObject(dc,oldfont);SelectObject(dc,old);check(DeleteObject(bitmap)&&DeleteDC(dc),28827);
}
#ifdef DRAW_CAPTION_STANDALONE
void mainCRTStartup(void){run_draw_caption_test();ExitProcess(0);}
#endif
