// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_draw_caption.h"
namespace X86 {
unsigned Application::DrawCaptionApi(ApiName name,const unsigned*a){
 using namespace Win98Caption;bool temporary=API_IS(name,"DrawCaptionTempA");
 auto input=Public(a,temporary);Args args{input.window,input.dc,input.font,input.icon,input.flags&65535,nullptr};
 if(args.window&&!User::IsWindow(args.window)){last_error=1400;return 0;}
 if(!Gdi::ValidDC(args.dc)){last_error=6;return 0;}
 if(args.font&&Gdi::GetObjectType(args.font)!=6){last_error=6;return 0;}
 if(args.icon&&!FindIcon(args.icon)){last_error=6;return 0;}
 if(args.flags&0x7c0){last_error=87;return 0;}
 Gdi::RECT rectangle{};if(!Read(a[2],&rectangle,sizeof rectangle))return 0;
 Rect r{W(rectangle.left),W(rectangle.top),W(rectangle.right),W(rectangle.bottom)};
 char text[8192]={};if(temporary&&a[5]){const char*s=ReadString(a[5]);if(state==Failed)return 0;unsigned n=strlen(s);if(n>=sizeof text){last_error=50;return 0;}memcpy(text,s,n+1);args.text=text;}
 Win98Caption::State settings;for(U i=0;i<29;++i){U query[]={i};settings.colors[i]=Api("GetSysColor",query);settings.brushes[i]=Api("GetSysColorBrush",query);}
 settings.font=settings.smallfont=system;unsigned previous=Gdi::SelectObject(args.dc,system);Gdi::TEXTMETRICA tm{};bool metrics=Gdi::GetTextMetricsA(args.dc,&tm);if(previous)Gdi::SelectObject(args.dc,previous);if(!metrics)return 0;settings.height=settings.smallheight=tm.tmHeight;
 settings.iconwidth=SystemMetric(49);settings.iconheight=SystemMetric(50);settings.border=SystemMetric(5);settings.margin=SystemMetric(45);settings.cacheheight=SystemMetric(30);
 // Native pattern realization of USER's shared monochrome caption dither brush.
 // The original selects it with COLOR_BTNFACE/COLOR_BTNHIGHLIGHT already set.
 Gdi::BITMAPINFO info{{40,8,-8,1,32,0,256,0,0,0,0}};void*bits=nullptr;U bitmap=Gdi::CreateDIBSection(args.dc,&info,0,&bits);
 if(!bitmap||!bits){if(bitmap)Gdi::DeleteObject(bitmap);last_error=8;return 0;}
 auto*pattern=static_cast<U*>(bits);for(U y=0;y<8;++y)for(U x=0;x<8;++x){U color=settings.colors[((x^y)&1)?15:20];pattern[y*8+x]=((color&255)<<16)|(color&0xff00)|((color>>16)&255);}
 settings.dither=Gdi::CreatePatternBrush(bitmap);Gdi::DeleteObject(bitmap);if(!settings.dither){last_error=8;return 0;}
 struct E{
  Application&app;U dc;bool okay=true;
  void Limit(){app.last_error=50;okay=false;}
  bool ValidWindow(U h){return User::IsWindow(h);}bool SystemMenu(U h){return (User::Style(h)&0x80000)!=0;}
  bool EligibleIcon(U h){if(User::ExtendedStyle(h)&0x80)return false;if((User::Style(h)&0xc0000000)==0x40000000||(User::ExtendedStyle(h)&1))return User::ClassAccess(h,-34,0,2)||User::ClassAccess(h,-14,0,2);return true;}
  // Resolve caption icons from the window's WM_SETICON property, class icon,
  // class resource or WM_QUERYDRAGICON with a 100-ms timeout, then use the
  // generic fallback. Explorer uses this path for taskbar buttons when no
  // explicit icon or title is supplied.
  U SmallIcon(U h,bool){
   U icon=User::WindowIcon(h,false);
   if(!icon)icon=User::WindowIcon(h,true);
   if(!icon)icon=User::ClassAccess(h,-34,0,2);
   if(!icon)icon=User::ClassAccess(h,-14,0,2);
   if(!icon){icon=User::WindowDragIcon(h);if(!icon)User::WantDragIcon(h);}
   // USER's own icon, the Windows logo (resource 105, OIC_WINLOGO) at the
   // small icon size, loaded; not IDI_APPLICATION.
   if(!icon){U values[]={0,32517,1,app.SystemMetric(49),app.SystemMetric(50),0x8000};icon=app.ImageApi("LoadImageA",values);}
   return icon;}
  void Caption(U h,char*out,U capacity,bool direct){
   U scratch=app.Allocate(capacity);if(!scratch){okay=false;return;}char zero=0;app.Store(scratch,&zero,1);
   if(direct)app.WindowTextDefault(h,0xd,capacity,scratch);else{U values[]={h,scratch,capacity};app.WindowTextApi("GetWindowTextA",values);}
   if(app.state!=Failed&&!app.Read(scratch,out,capacity))okay=false;out[capacity]=0;app.Free(scratch);
  }
  U BkMode(U h,U v){return Gdi::SetBkMode(h,v);}U TextColor(U h,U v){return Gdi::SetTextColor(h,v);}U BkColor(U h,U v){return Gdi::SetBkColor(h,v);}U Select(U h,U v){return Gdi::SelectObject(h,v);}int Height(U h){Gdi::TEXTMETRICA t{};if(!Gdi::GetTextMetricsA(h,&t))okay=false;return W(t.tmHeight);}U Length(const char*s){return strlen(s);}
  int Extent(const char*s,U n){Gdi::SIZE size{};if(!Gdi::GetTextExtentPoint32A(dc,s,n,&size))okay=false;return W(size.cx);}
  bool CachedIcon(U,Rect,U,U){return false;}bool CachedGradient(U,Rect,U){return false;}
  void Fill(U h,Rect r,U b){Gdi::RECT q{r.left,r.top,r.right,r.bottom};if(!Gdi::FillRect(h,&q,b))okay=false;}
  void Stripe(U h,Rect r,U color){U brush=Gdi::CreateSolidBrush(color);if(!brush){okay=false;return;}Fill(h,r,brush);Gdi::DeleteObject(brush);}
  void DrawIcon(U h,int x,int y,U icon,int w,int height){U values[]={h,U(x),U(y),icon,U(w),U(height),0,0,3};if(!app.ImageApi("DrawIconEx",values))okay=false;}
  void Output(U h,int x,int y,Rect r,const char*s,U n){Gdi::RECT q{r.left,r.top,r.right,r.bottom};if(!Gdi::ExtTextOutA(h,x,y,4,&q,s?s:"",n))okay=false;}
  void Ellipsis(U h,const char*s,U n,Rect r,U flags,int margin){
   U scratch=app.Allocate(n+40);if(!scratch){okay=false;return;}Gdi::RECT q{r.left,r.top,r.right,r.bottom};U params[]={20,0,U(margin),0,0};
   app.Store(scratch,&q,16);app.Store(scratch+16,params,20);if(n)app.Store(scratch+36,s,n);char zero=0;app.Store(scratch+36+n,&zero,1);
   U values[]={h,scratch+36,n,scratch,flags,scratch+16};if(!app.DrawTextApi("DrawTextExA",values))okay=false;app.Free(scratch);
  }
 }e{*this,args.dc};
 U result=Draw(e,settings,args,r);Gdi::DeleteObject(settings.dither);User::DrawingChanged(args.dc);
 // USER32's thunk passes a private RECT16; caller's RECT32 is not written back.
 return e.okay&&state!=Failed?result:0;
}
}
