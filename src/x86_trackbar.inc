// SPDX-License-Identifier: GPL-2.0-or-later
// Native provider for the ordinary trackbars used by SERIALUI's FIFO dialog.
#include "win98_trackbar.h"
namespace X86 {
namespace {
struct TrackGeometry{int origin,extent,thumb,height,top;bool vertical;};
TrackGeometry TrackBounds(unsigned style,const Gdi::RECT&r,unsigned metric){
 int length=int(metric)*4/3,thumb=(length/2)|1;bool vertical=style&2;
 int width=vertical?r.bottom:r.right,top=(!(style&16)&&(style&12)?8:0)+2;
 return {thumb+2,width-2*(thumb+2),thumb,length,top,vertical};
}
}
unsigned Application::TrackbarMessage(unsigned hwnd,unsigned message,unsigned wp,unsigned lp){
 // -> : calculate the guest RECT, including before WM_CREATE.
 if(message==0x83)return DefaultWindowMessage(hwnd,message,wp,lp);
 unsigned t=0;User::WindowLong(hwnd,0,t,false);
 if(message==1){t=Allocate(64);if(!t)return ~0u;unsigned v=t;User::WindowLong(hwnd,0,v,true);Write32(t+4,100);Write32(t+12,~0u);Write32(t+16,1);Write32(t+20,1);return 0;}
 if(!t)return User::DefaultMessage(hwnd,message,wp,lp);
 if(message==2){unsigned timer[]={hwnd,1};Api("KillTimer",timer);unsigned zero=0;User::WindowLong(hwnd,0,zero,true);Free(t);return 0;}
 auto get=[&](unsigned offset){return int(Read32(t+offset));};
 auto dirty=[&](){User::InvalidateRect(hwnd,nullptr,false);};
 auto set=[&](int p){Write32(t+8,unsigned(Win98Trackbar::Clamp(p,get(0),get(4))));dirty();};
 auto scroll=[&](unsigned code){unsigned position=(code==4||code==5)?Read32(t+8):0;Send(User::Parent(hwnd),User::Style(hwnd)&2?0x115:0x114,(position<<16)|code,hwnd);};
 Gdi::RECT r{};User::GetClientRect(hwnd,&r);auto g=TrackBounds(User::Style(hwnd),r,SystemMetric(3));
 auto pixel=[&](int value){return Win98Trackbar::Pixel(value,get(0),get(4),g.origin,g.extent);};
 if(message==0x400)return Read32(t+8);
 if(message==0x401)return Read32(t);
 if(message==0x402)return Read32(t+4);
 if(message==0x405){Write32(t+8,unsigned(Win98Trackbar::Clamp(int(lp),get(0),get(4))));if(wp)dirty();return 0;}
 if(message==0x406||message==0x407||message==0x408){
  if(message!=0x408)Write32(t,message==0x406?unsigned(int(short(lp))):lp);
  if(message!=0x407)Write32(t+4,message==0x406?unsigned(int(short(lp>>16))):lp);
  Write32(t+8,unsigned(Win98Trackbar::Clamp(get(8),get(0),get(4))));if(wp)dirty();return 0;
 }
 if(message==0x414){Write32(t+20,wp);if(wp)dirty();return 0;}
 if(message==0x415||message==0x416){int old=Win98Trackbar::Page(get(0),get(4),get(12));if(message==0x415)Write32(t+12,lp);return unsigned(old);}
 if(message==0x417||message==0x418){unsigned old=Read32(t+16);if(message==0x417)Write32(t+16,lp);return old;}
 if(message==0x410){if(User::Style(hwnd)&16)return 0;int n=int(unsigned(get(4))-unsigned(get(0))-1);if(n<0)n=0;return get(20)?unsigned(n/get(20)+2):2;}
 if(message==0x419||message==0x41a){
  int x=pixel(get(8)),center=g.top+g.height/2;Gdi::RECT box=message==0x419?Gdi::RECT{x-g.thumb/2,g.top,x-g.thumb/2+g.thumb,g.top+g.height}:Gdi::RECT{g.origin-g.thumb/2,center-2,g.origin+g.extent+g.thumb/2-1,center+2};
  if(g.vertical){Gdi::RECT swapped{box.top,box.left,box.bottom,box.right};box=swapped;}if(lp)Store(lp,&box,16);return 0;
 }
 if(message==0x87)return 1;
 if(message==0x100&&User::IsEnabled(hwnd)){
  int p=get(8),step=get(16),page=Win98Trackbar::Page(get(0),get(4),get(12));if(get(12)==-1&&!page)page=1;unsigned code=0;
  switch(wp){case 0x25:case 0x26:p=int(unsigned(p)-unsigned(step));break;case 0x27:case 0x28:p=int(unsigned(p)+unsigned(step));code=1;break;
   case 0x21:p=int(unsigned(p)-unsigned(page));code=2;break;case 0x22:p=int(unsigned(p)+unsigned(page));code=3;break;
   case 0x24:p=get(0);code=6;break;case 0x23:p=get(4);code=7;break;default:return 0;}
  set(p);scroll(code);return 0;
 }
 if(message==0x101&&wp>=0x21&&wp<=0x28&&User::IsEnabled(hwnd)){scroll(8);return 0;}
 if(message==0x201&&User::IsEnabled(hwnd)){
  SetFocus(hwnd);int at=g.vertical?short(lp>>16):short(lp),cross=g.vertical?short(lp):short(lp>>16),thumb=pixel(get(8));
  if(g.extent<=0||(User::Style(hwnd)&0x80)||cross<g.top||cross>=g.top+g.height)return 0;
  if(at<g.origin||at>=g.origin+g.extent){if(at<thumb-g.thumb/2||at>=thumb+g.thumb-g.thumb/2)return 0;}
  Api("SetCapture",&hwnd);
  if(at>=thumb-g.thumb/2&&at<thumb+g.thumb-g.thumb/2){Write32(t+24,1);Write32(t+28,unsigned(at-thumb));}
  else{int page=Win98Trackbar::Page(get(0),get(4),get(12));if(get(12)==-1&&!page)page=1;Write32(t+24,at<thumb?2:3);unsigned timer[]={hwnd,1,500,0};Api("SetTimer",timer);set(int(unsigned(get(8))+unsigned(at<thumb?-page:page)));scroll(at<thumb?2:3);}
  return 0;
 }
 if(message==0x113&&wp==1&&Read32(t+24)>=2){
  Gdi::POINT cursor{mouse_x,mouse_y};User::ScreenToClient(hwnd,&cursor);int at=g.vertical?cursor.y:cursor.x,cross=g.vertical?cursor.x:cursor.y,thumb=pixel(get(8));
  if(cross>=g.top&&cross<g.top+g.height&&at>=g.origin&&at<g.origin+g.extent&&(Read32(t+24)==2?at<thumb-g.thumb/2:at>=thumb+g.thumb-g.thumb/2)){
   int page=Win98Trackbar::Page(get(0),get(4),get(12));if(get(12)==-1&&!page)page=1;unsigned code=Read32(t+24);set(int(unsigned(get(8))+unsigned(code==2?-page:page)));scroll(code);
  }return 0;
 }
 if(message==0x200&&Read32(t+24)==1&&Api("GetCapture",nullptr)==hwnd){
  int at=(g.vertical?short(lp>>16):short(lp))-get(28)-g.origin;
  long long span=static_cast<long long>(get(4))-get(0);int p=g.extent>1?int(get(0)+(span*at+(g.extent-1)/2)/(g.extent-1)):get(0);
  set(p);scroll(5);return 0;
 }
 if((message==0x202||message==0x215||message==0x1f)&&Read32(t+24)){
  bool drag=Read32(t+24)==1;Write32(t+24,0);unsigned timer[]={hwnd,1};Api("KillTimer",timer);if(Api("GetCapture",nullptr)==hwnd)Api("ReleaseCapture",nullptr);if(drag)scroll(4);scroll(8);return 0;
 }
 if(message==0xf||message==0x318){User::Paint paint{};unsigned dc=message==0x318?wp:User::BeginPaint(hwnd,paint);if(dc){DrawTrackbar(hwnd,dc,r);User::DrawingChanged(dc);if(message==0xf)User::EndPaint(hwnd,paint);}return 0;}
 if(message==0x14)return 1;
 if(message==5||message==7||message==8||message==0xa){dirty();return 0;}
 if(message>=0x400&&message<0x500){Fail("Trackbar message outside the native FIFO control port");return 0;}
 return User::DefaultMessage(hwnd,message,wp,lp);
}
void Application::DrawTrackbar(unsigned hwnd,unsigned dc,const Gdi::RECT&bounds){
 unsigned t=0;if(!User::WindowLong(hwnd,0,t,false)||!t)return;auto g=TrackBounds(User::Style(hwnd),bounds,SystemMetric(3));
 auto color=[&](unsigned id){return Api("GetSysColor",&id);};
 auto orient=[&](Gdi::RECT r){return g.vertical?Gdi::RECT{r.top,r.left,r.bottom,r.right}:r;};
 auto fill=[&](Gdi::RECT r,unsigned c){r=orient(r);unsigned brush=Gdi::CreateSolidBrush(c);if(brush){Gdi::FillRect(dc,&r,brush);Gdi::DeleteObject(brush);}};
 int save=Gdi::SaveDC(dc);if(!save)return;Gdi::IntersectClipRect(dc,bounds.left,bounds.top,bounds.right,bounds.bottom);
 unsigned brush=Gdi::CreateSolidBrush(color(15));Gdi::FillRect(dc,&bounds,brush);Gdi::DeleteObject(brush);
 int center=g.top+g.height/2;Gdi::RECT channel=orient({g.origin-g.thumb/2,center-2,g.origin+g.extent+g.thumb/2-1,center+2});Win98Draw::DrawEdge(dc,&channel,0xa,0xf);
 if(!(User::Style(hwnd)&16)){
  int lo=int(Read32(t)),hi=int(Read32(t+4)),freq=int(Read32(t+20));
  auto tick=[&](int value){int x=Win98Trackbar::Pixel(value,lo,hi,g.origin,g.extent);if(User::Style(hwnd)&12)fill({x,g.top-6,x+1,g.top-2},color(18));if(!(User::Style(hwnd)&4)||(User::Style(hwnd)&8))fill({x,g.top+g.height+2,x+1,g.top+g.height+6},color(18));};
  tick(lo);tick(hi);if(freq>0&&hi>=lo&&static_cast<long long>(hi)-lo<4096)for(long long v=static_cast<long long>(lo)+freq;v<hi;v+=freq)tick(int(v));
 }
 if(!(User::Style(hwnd)&0x80)){int x=Win98Trackbar::Pixel(int(Read32(t+8)),int(Read32(t)),int(Read32(t+4)),g.origin,g.extent);Gdi::RECT thumb=orient({x-g.thumb/2,g.top,x-g.thumb/2+g.thumb,g.top+g.height});Win98Draw::ButtonFrame(dc,&thumb,16,0x1000);}
 if(User::GetFocus()==hwnd){Gdi::RECT focus=bounds;Gdi::DrawFocusRect(dc,&focus);}
 Gdi::RestoreDC(dc,save);
}
} // namespace X86
