// SPDX-License-Identifier: GPL-2.0-or-later
// Native guest-address/provider boundary for the original toolbar button object.
// Included after x86_common.cpp so owned strings use its established heap bridge.
#include "win98_toolbar.h"
#include "win98_toolbar_layout.h"
#include "win98_toolbar_paint.h"
#include "win98_toolbar_bitmap.h"
#include "win98_toolbar_input.h"
#include "user_draw.h"
#if defined(__APPLE__) || defined(__linux__)
#include <stdlib.h>
#else
#include <circle/alloc.h>
#endif
namespace X86 {
struct Application::ToolbarMemory {
 Application&app;Toolbar&owner;
 ToolbarMemory(Application&a,Toolbar&t):app(a),owner(t){}
 struct Scratch {
  Application&app;unsigned address,old;bool allocated;
  Scratch(ToolbarMemory&e,unsigned n):app(e.app),old(app.Register(Reg::Esp)){
   allocated=app.Win16Process();address=allocated?app.Allocate(n):(old-n-32)&~15u;
   if(!allocated)app.Register(Reg::Esp,address);
  }
  ~Scratch(){if(allocated){if(address)app.Free(address);}else app.Register(Reg::Esp,old);}
 };
 unsigned Read8(unsigned p){unsigned char v=0;app.Read(p,&v,1);return v;}
 unsigned Read16(unsigned p){unsigned short v=0;app.Read(p,&v,2);return v;}
 unsigned Read32(unsigned p){return app.Read32(p);}
 void Write8(unsigned p,unsigned v){unsigned char b=v;app.Store(p,&b,1);}
 void Write16(unsigned p,unsigned v){unsigned short b=v;app.Store(p,&b,2);}
 void Write32(unsigned p,unsigned v){app.Write32(p,v);}
 unsigned AllocateLocal(unsigned flags,unsigned n){unsigned a[]={flags,n};return app.Api("LocalAlloc",a);}
 unsigned ReallocateLocal(unsigned p,unsigned n,unsigned flags){unsigned a[]={p,n,flags};return app.Api("LocalReAlloc",a);}
 void FreeLocal(unsigned p){app.Api("LocalFree",&p);}
 unsigned SetString(unsigned field,unsigned text){unsigned a[]={field,text};return app.CommonControlApi(236,a);}
 void ConvertAtoW(unsigned source,unsigned out,unsigned count){unsigned a[]={0,0,source,0xffffffff,out,count};app.NlsApi(594,a);}
 void ConvertWtoA(unsigned source,unsigned out,unsigned count){unsigned a[]={0,0,source,0xffffffff,out,count,0,0};app.NlsApi(811,a);}
 void CopyText(unsigned out,unsigned text,unsigned count){if(!count)return;unsigned i=0;for(;i+1<count;++i){unsigned c=Read16(text+i*2);if(!c)break;Write16(out+i*2,c);}Write16(out+i*2,0);}
 unsigned AnsiToWide(unsigned,unsigned text){unsigned a[]={0,0,text,0xffffffff,0,0};unsigned n=app.NlsApi(594,a);if(!n||n>0x7fffffff)return 0;unsigned p=AllocateLocal(0,n*2);if(!p)return 0;a[4]=p;a[5]=n;if(!app.NlsApi(594,a)){FreeLocal(p);return 0;}return p;}
 void Invalidate(unsigned hwnd,unsigned,unsigned erase){User::InvalidateRect(hwnd,nullptr,erase!=0);}
 void InvalidateButton(unsigned t,unsigned,unsigned erase){Invalidate(Read32(t),0,erase);}
 // Automatic wrapping remains deferred; single-line measurement is connected below.
 void Layout(unsigned t){app.LayoutToolbar(owner);Win98Toolbar::Dirty(*this,t);}
 void Wrap(unsigned){owner.layout_pending=true;}
 void WrapWidth(unsigned,unsigned,unsigned,unsigned){owner.layout_pending=true;}
 void WrapRows(unsigned,unsigned,unsigned,unsigned){owner.layout_pending=true;}
 unsigned BorderX(){return 1;}
 unsigned Text(unsigned t,unsigned button){return Win98ToolbarPaint::Text(*this,t,button);}
 unsigned TextWidth(unsigned t,unsigned button,unsigned){char text[1024];if(!TextBytes(t,button,text,sizeof text))return 0;Gdi::SIZE size{};unsigned dc=app.ToolbarFontDC(Read32(t+0x4c));if(!dc)return 0;Gdi::GetTextExtentPoint32A(dc,text,strlen(text),&size);app.ToolbarFontRelease();return size.cx;}
 bool TextBytes(unsigned t,unsigned button,char*out,unsigned capacity){
  out[0]=0;unsigned text=Text(t,button);if(!text)return false;
  Scratch converted(*this,capacity);unsigned a[]={0,0,text,~0u,converted.address,capacity,0,0};
  if(!app.NlsApi(811,a))return false;app.Read(converted.address,out,capacity);out[capacity-1]=0;
  if(!(Read8(button+9)&32)){unsigned n=0;for(unsigned i=0;out[i];++i){if(out[i]=='&'){if(out[i+1]=='&')++i;else continue;}out[n++]=out[i];}out[n]=0;}
  return out[0]!=0;
 }
 unsigned ImagePresent(unsigned t,unsigned b){return Win98ToolbarPaint::SplitArrow(*this,t,b);}
 unsigned SetSize(unsigned t,unsigned w,unsigned h,unsigned flags){return Win98ToolbarLayout::SetSize(*this,t,w,h,flags);}
 bool Clipped(unsigned t,unsigned p){Gdi::RECT client{},rect{};User::GetClientRect(Read32(t),&client);app.Read(p,&rect,16);return rect.left<client.left||rect.top<client.top||rect.right>client.right||rect.bottom>client.bottom;}
 void BitmapInfo(unsigned bitmap,unsigned p){unsigned a[]={bitmap,24,p};app.GdiApi("GetObjectA",a);}
 unsigned CreateMono(unsigned width,unsigned height){unsigned a[]={width,height,1,1,0};return app.GdiApi("CreateBitmap",a);}
 void DeleteBitmap(unsigned bitmap){app.GdiApi("DeleteObject",&bitmap);}
 unsigned EnsureBacking(unsigned t,unsigned width,unsigned height){return Win98ToolbarLayout::EnsureBacking(*this,t,width,height);}
 unsigned BorderY(){return 1;}unsigned SeparatorWidth(){return 8;}
 unsigned ImageList(unsigned t,unsigned kind,unsigned index){return Win98ToolbarPaint::Images(*this,t,kind,index);}
 unsigned ImageCount(unsigned h){auto*l=app.FindImageList(h);return l?l->count:0;}
 void BitmapSize(unsigned t,unsigned w,unsigned h){Win98ToolbarPaint::BitmapSize(*this,t,w,h);}
 void Standard(unsigned&,unsigned&,unsigned&){app.last_error=50;}
 // Native bitmap/ImageList boundary. The original loader copies the strip into
 // its own image list; module resources are temporary, caller HBITMAPs survive.
 unsigned LoadStrip(unsigned t,unsigned group){
  unsigned module=Read32(group+4),resource=Read32(group+8),bitmap=resource;
  if(module){unsigned args[]={module,resource};bitmap=app.ImageApi("LoadBitmapA",args);}
  if(!bitmap)return 0;
  unsigned bm[6]{},w=Read32(t+0x68),h=Read32(t+0x6c),width=0,height=0;
  bool valid=Gdi::BitmapInfo(bitmap,bm);if(valid){width=bm[1];height=bm[2];}
  // A native provider must not publish phantom images after a failed load or
  // silently pad an incomplete strip. The pure original dimension decision is
  // retained for the differential oracle and checked against real dimensions.
  unsigned targetw=width,targeth=height;
  if(valid&&w&&h)Win98ToolbarBitmap::Dimensions(w,h,Read32(group),targetw,targeth);
  if(!valid||!w||!h||w>256||h>256||targetw>width||targeth>height||targetw/w>128){if(module)DeleteBitmap(bitmap);app.last_error=87;return 0;}
  unsigned list=ImageList(t,0,0),made=0;
  if(!list){unsigned args[]={w,h,1,4,4};list=app.ImageApi("ImageList_Create",args);made=list;}
  auto*images=app.FindImageList(list);unsigned n=targetw/w;
  if(!images||images->width!=w||images->height!=h||n>128-images->count){if(made)app.ImageApi("ImageList_Destroy",&made);if(module)DeleteBitmap(bitmap);app.last_error=87;return 0;}
  WinImage::Image*pending[128]{};unsigned dc=Gdi::CreateCompatibleDC(0),old=dc?Gdi::SelectObject(dc,bitmap):0;
  bool ok=old!=0;unsigned face_index=15,face=app.Api("GetSysColor",&face_index);
  for(unsigned i=0;ok&&i<n;++i){
   auto*image=new WinImage::Image;pending[i]=image;if(!image){ok=false;break;}
   image->color=static_cast<unsigned*>(malloc(w*h*4));image->mask=static_cast<unsigned*>(malloc(w*h*4));image->width=w;image->height=h;
   if(!image->color||!image->mask){ok=false;break;}
   for(unsigned y=0;ok&&y<h;++y)for(unsigned x=0;x<w;++x){unsigned color=Gdi::GetPixel(dc,i*w+x,y);if(color==~0u){ok=false;break;}
    // CreateMappedBitmap default six palette substitutions. GetPixel
    // returns COLORREF; WinImage stores native 0xRRGGBB pixels.
    unsigned index=Win98ToolbarBitmap::ColorIndex(color);
    if(module&&(bm[4]>>16)<=8&&index!=~0u)color=app.Api("GetSysColor",&index);
    unsigned k=y*w+x;image->mask[k]=color==face?~0u:0;image->color[k]=color==face?0:((color&255)<<16)|(color&0xff00)|(color>>16&255);
   }
  }
  if(old)Gdi::SelectObject(dc,old);if(dc)Gdi::DeleteDC(dc);if(module)DeleteBitmap(bitmap);
  if(!ok){for(auto*image:pending)delete image;if(made)app.ImageApi("ImageList_Destroy",&made);app.last_error=8;return 0;}
  for(unsigned i=0;i<n;++i)images->items[images->count++]=pending[i];
  if(made){Win98ToolbarPaint::SetImages(*this,t,0,0,list);if(ImageList(t,0,0)!=list){app.ImageApi("ImageList_Destroy",&made);return 0;}owner.strip_images=list;}
  User::InvalidateRect(owner.hwnd,nullptr,true);return 1;
 }
 void Accessibility(unsigned,unsigned,unsigned,unsigned){owner.accessibility_pending=true;}
 void Tooltip(unsigned hwnd,unsigned message,unsigned info){app.CallWindow(hwnd,message,0,info);}
 unsigned CancelCapture(unsigned t){return Win98ToolbarInput::Release(*this,t);}
 unsigned Hit(unsigned t,int x,int y){return Win98ToolbarPaint::Hit(*this,t,x,y);}
 unsigned Capture(){return User::ThreadCapture(User::WindowThread(owner.hwnd));} // the toolbar's queue
 void SetCapture(unsigned h){User::CaptureOperation(1,h);}
 void ReleaseCapture(){User::CaptureOperation(9,owner.hwnd);}
 bool Alive(unsigned h){return User::IsWindow(h);}
 unsigned Focus(){return User::GetFocus();}
 void Update(unsigned h){User::UpdateControl(h);User::Render();} // the original's UpdateWindow
 void TrackLeave(unsigned h,bool cancel){User::TrackMouseLeave(h,!cancel);}
 void Cursor(unsigned h,unsigned out){Gdi::POINT p{app.mouse_x,app.mouse_y};User::ScreenToClient(h,&p);app.Store(out,&p,8);}
 void Relay(unsigned t,unsigned message,unsigned keys,unsigned point){
  // Original tooltip creation/relay is outside this normal-button package.
  // No tooltip handle or successful tooltip creation is fabricated.
  if(Read32(t+0x40)){unsigned args[]={Read32(t+0x40),message,keys,point};app.last_error=50;(void)args;}
 }
 void Dropdown(unsigned,unsigned,unsigned){app.last_error=50;}
 bool AdjustRequested(unsigned t,unsigned keys){unsigned style=Read32(t+8);if((keys&4)&&!(style&0x400))return true;unsigned alt=0x12;return (app.Api("GetKeyState",&alt)&0xfffe)&&(style&0x400);}
 void Adjust(unsigned,unsigned){app.last_error=50;}
 void Unpress(unsigned t,unsigned command){unsigned i=Win98Toolbar::CommandIndex(*this,t,command);if(i!=~0u){unsigned b=Read32(t+0x30)+28*i;Win98Toolbar::SetState(*this,t,b,Read8(b+8)&~2u,i);}}
 void ChangeStyle(unsigned,unsigned){app.last_error=50;}
 void Reflow(unsigned){app.AutosizeToolbar(owner);}
 void Command(unsigned parent,unsigned command,unsigned h){app.CallWindow(parent,0x111,command,h);}
 unsigned ButtonNotify(unsigned t,unsigned command,unsigned code){Scratch note(*this,60);Win98ToolbarInput::Zero(*this,note.address,60);Write32(note.address+12,command);return Notify(t,code,note.address);}
 unsigned Parent(unsigned hwnd){return User::Parent(hwnd);}
 void ClientRect(unsigned hwnd,unsigned p){Gdi::RECT r={};User::GetClientRect(hwnd,&r);app.Store(p,&r,sizeof r);}
 unsigned Notify(unsigned t,unsigned code,unsigned note){Write32(note,Read32(t));Write32(note+4,User::ControlID(Read32(t)));Write32(note+8,code);if(unsigned parent=Read32(t+4))return app.CallWindow(parent,0x4e,Read32(note+4),note);return 0;}
};
// Native single-line FNT measurement provider. Original Width/Cache/SetSize
// remain the common geometry kernels; full COMCTL DrawText wrapping is absent.
void Application::LayoutToolbar(Toolbar&t){
 if(!t.object)return;ToolbarMemory e(*this,t);unsigned o=t.object,flags=Read32(o+0xec);
 if(flags&0x800){Write32(o+0xec,flags|0x2000);t.layout_pending=true;return;}
 unsigned width=Read32(o+0x68);Write32(o+0x88,0);
 const unsigned count=Read32(o+0x84),buttons=Read32(o+0x30);
 if(count>AllocationSize(buttons)/28){last_error=87;return;}
 if(Read32(o+0x60)&&(Read32(o+0x5c)||(flags&8))){
  Gdi::TEXTMETRICA metrics{};unsigned dc=ToolbarFontDC(Read32(o+0x4c));const bool measured=dc&&Gdi::GetTextMetricsA(dc,&metrics);ToolbarFontRelease();if(!measured)return;
  Write32(o+0x88,metrics.tmHeight);
  for(unsigned i=0;i<count;++i){unsigned b=buttons+i*28;if(e.Read8(b+8)&8)continue;
   e.Write16(b+10,0);unsigned text=e.TextWidth(o,b,0);if(text>width)width=text;
  }
 }
 unsigned minimum=Read32(o+0x78),maximum=Read32(o+0x7c),padding=Read32(o+0x94);
 if(minimum>width+padding)width=minimum-padding;
 if(maximum&&maximum<width+padding)width=maximum>padding?maximum-padding:0;
 if(e.SetSize(o,width,Win98ToolbarPaint::TextHeight(e,o,Read32(o+0x6c)),1))t.layout_pending=false;
}
void Application::AutosizeToolbar(Toolbar&t){
 if(!t.object)return;ToolbarMemory e(*this,t);unsigned o=t.object,style=Read32(o+8);
 // Existing window-position provider handles the common horizontal toolbar.
 // CCS_NORESIZE retains the application's exact dimensions.
 if(style&4)return;
 if((style&0x80)||(Read32(o+0x24)&(2|4))){last_error=50;return;}
 Gdi::RECT client{},parent{},window{};User::GetClientRect(t.hwnd,&client);User::GetWindowRect(t.hwnd,&window);
 unsigned owner=User::Parent(t.hwnd);if(!owner)return;
 User::GetClientRect(owner,&parent);Gdi::POINT point{window.left,window.top};User::ScreenToClient(owner,&point);
 int width=parent.right-parent.left,height=int(Win98ToolbarPaint::Rows(e,o)*Read32(o+0x74)+2*e.BorderY());
 int y=(style&3)==3?parent.bottom-height:(style&8)?point.y:0;
 unsigned args[]={t.hwnd,0,unsigned((style&8)?point.x:0),unsigned(y),unsigned(width),unsigned(height),0x14};
 PositionApi("SetWindowPos",args);Win98Toolbar::Dirty(e,o);
}
void Application::DrawToolbar(unsigned hwnd,Gdi::HDC dc,const Gdi::RECT&bounds){
 Toolbar*t=nullptr;for(auto&candidate:toolbars)if(candidate.hwnd==hwnd&&candidate.object){t=&candidate;break;}
 if(!t||!dc)return;ToolbarMemory e(*this,*t);unsigned o=t->object;
 if(Read32(o+0xec)&0x800)return;if(t->layout_pending)LayoutToolbar(*t);
 unsigned count=Read32(o+0x84),buttons=Read32(o+0x30);if(count>AllocationSize(buttons)/28){last_error=87;return;}
 auto color=[&](unsigned index){return Api("GetSysColor",&index);};
 const unsigned face=color(15),text_color=color(18),gray=color(17),highlight=color(20),shadow=color(16);
 auto fill=[&](const Gdi::RECT&r,unsigned color){unsigned brush=Gdi::CreateSolidBrush(color);if(brush){Gdi::FillRect(dc,&r,brush);Gdi::DeleteObject(brush);}};
 int saved=Gdi::SaveDC(dc);if(!saved)return;Gdi::IntersectClipRect(dc,bounds.left,bounds.top,bounds.right,bounds.bottom);
 fill(bounds,face);Gdi::SelectObject(dc,Read32(o+0x4c));Gdi::SetBkMode(dc,Gdi::TRANSPARENT);
 ToolbarMemory::Scratch rectangle(e,16);
 for(unsigned i=0;i<count;++i){
  if(!Win98ToolbarLayout::Rect(e,o,i,rectangle.address))continue;
  Gdi::RECT rect{};Read(rectangle.address,&rect,16);if(rect.right<=bounds.left||rect.left>=bounds.right||rect.bottom<=bounds.top||rect.top>=bounds.bottom)continue;
  unsigned b=buttons+i*28,state=e.Read8(b+8),style=e.Read8(b+9);int clipped=Gdi::SaveDC(dc);if(!clipped)break;Gdi::IntersectClipRect(dc,rect.left,rect.top,rect.right,rect.bottom);
  if(style&1){int x=(rect.left+rect.right)/2;fill({x,rect.top+2,x+1,rect.bottom-2},shadow);fill({x+1,rect.top+2,x+2,rect.bottom-2},highlight);Gdi::RestoreDC(dc,clipped);continue;}
  const bool pressed=state&2,checked=state&1,enabled=state&4,hot=Read32(o+0xcc)==i;
  if(checked)fill({rect.left+2,rect.top+2,rect.right-2,rect.bottom-2},color(22));
  if(!(Read32(o+8)&0x800)||pressed||checked||hot){auto edge=rect;Win98Draw::DrawEdge(dc,&edge,(pressed||checked)?10:5,15);}
  const int shift=(pressed||checked)?1:0;
  char text[1024];bool has_text=Read32(o+0x60)&&e.TextBytes(o,b,text,sizeof text);
  if((Read32(o+0x24)&8)&&(Read32(o+8)&0x1000)&&!(style&64))has_text=false;
  Gdi::SIZE size{};if(has_text)Gdi::GetTextExtentPoint32A(dc,text,strlen(text),&size);
  const unsigned image_value=Read32(b),group=image_value>>16,index=image_value&65535;
  ImageList*images=nullptr;unsigned special=enabled&&hot?1:!enabled?2:0;
  if(special)images=FindImageList(e.ImageList(o,special,group));if(!images)images=FindImageList(e.ImageList(o,0,group));
  WinImage::Image*image=images&&image_value!=~0u&&image_value!=~1u&&index<images->count?images->items[index]:nullptr;
  int image_x=rect.left+(rect.right-rect.left-int(image?image->width:0))/2+shift;
  int image_y=rect.top+(rect.bottom-rect.top-int(image?image->height:0)-(has_text?size.cy+1:0))/2+shift;
  int tx=rect.left+(rect.right-rect.left-size.cx)/2+shift,ty=image?image_y+image->height+1:rect.top+(rect.bottom-rect.top-size.cy)/2+shift;
  if(Read32(o+8)&0x1000){image_x=rect.left+int(Read32(o+0x94))/2+shift;image_y=rect.top+(rect.bottom-rect.top-int(image?image->height:0))/2+shift;tx=image?image_x+image->width+Read32(o+0x9c):rect.left+3+shift;ty=rect.top+(rect.bottom-rect.top-size.cy)/2+shift;}
  if(image){
   if(!enabled&&!FindImageList(e.ImageList(o,2,group))){WinImage::Image faded;if(faded.Copy(*image)){for(unsigned n=0;n<faded.width*faded.height;++n){unsigned p=faded.color[n],v=((p>>16&255)+(p>>8&255)+(p&255))/3;faded.color[n]=(p&0xff000000)|v*0x10101;}faded.Draw(dc,image_x,image_y);}}
   else image->Draw(dc,image_x,image_y);
  }
  if(has_text){Gdi::SetTextColor(dc,enabled?text_color:gray);Gdi::TextOutA(dc,tx,ty,text,strlen(text));}
  if(Win98ToolbarLayout::HasArrow(e,o,b)){int x=rect.right-7+shift,y=(rect.top+rect.bottom)/2-1+shift;for(int row=0;row<3;++row)fill({x+row,y+row,x+5-row,y+row+1},enabled?text_color:gray);}
  Gdi::RestoreDC(dc,clipped);
 }
 Gdi::RestoreDC(dc,saved);
}
void Application::ReleaseToolbar(Toolbar&t){
 if(!t.object)return;
 ToolbarMemory e(*this,t);unsigned p=Read32(t.object+0x30),n=Read32(t.object+0x84);
 if(n>AllocationSize(p)/28)n=AllocationSize(p)/28;
 for(unsigned i=0;i<n;++i){unsigned text=Read32(p+i*28+16);if(text!=0xffffffff&&(text&0xffff0000))e.SetString(p+i*28+16,0);}
 if(p)Free(p);if(unsigned strips=Read32(t.object+0x54))Free(strips);if(t.strip_images)ImageApi("ImageList_Destroy",&t.strip_images);if(unsigned groups=Read32(t.object+0xc4))Free(groups);if(unsigned bitmap=Read32(t.object+0x2c))e.DeleteBitmap(bitmap);Free(t.object);t={};
}
// The shared measurement DC is created on first use and selects the caller's
// font, which is also the original object's font field at +0x4C. Like the
// original, it holds that font for one measurement only.
unsigned Application::ToolbarFontDC(unsigned font){
 if(!toolbar_font_dc){toolbar_font_dc=Gdi::CreateCompatibleDC(0);if(!toolbar_font_dc)return 0;}
 if(!font)font=system;
 if(!Gdi::SelectObject(toolbar_font_dc,font))return 0;
 return toolbar_font_dc;
}
// Restore the system font so no caller font stays selected. A font the guest
// owns must remain deletable while its toolbar lives, as in original Windows.
void Application::ToolbarFontRelease(){if(toolbar_font_dc&&system)Gdi::SelectObject(toolbar_font_dc,system);}
void Application::FreeToolbars(){for(auto&t:toolbars)ReleaseToolbar(t);if(toolbar_font_dc){Gdi::DeleteDC(toolbar_font_dc);toolbar_font_dc=0;}}
unsigned Application::ToolbarMessage(unsigned hwnd,unsigned message,unsigned wp,unsigned lp){
 Toolbar*t=nullptr;for(auto&item:toolbars)if(item.hwnd==hwnd&&item.object){t=&item;break;}
 if(message==0x81){
  if(t)return 0;for(auto&item:toolbars)if(!item.object){t=&item;break;}
  if(!t){last_error=8;return 0;}
  unsigned args[]={0x40,0x108},object=Api("LocalAlloc",args);if(!object)return 0;
  t->object=object;t->hwnd=hwnd;t->layout_pending=true;ToolbarMemory e(*this,*t);
  if(!ToolbarFontDC(system)){ReleaseToolbar(*t);return 0;}ToolbarFontRelease();Write32(object+0x4c,system);
  // Shared initialization and scalar stores. Font,
  // default FNT selection is above; tooltip creation remains deferred.
  Write32(object,hwnd);Write32(object+4,Read32(lp+12));Write32(object+8,Read32(lp+32));Write32(object+0x18,Read32(lp+44));
  if(unsigned parent=Read32(object+4)){
   ++t->active;unsigned unicode=CallWindow(parent,0x55,hwnd,3)==2;Write32(object+0x10,unicode);e.Write16(object+0x20,CallWindow(parent,0x129,0,0));--t->active;
  }
  const unsigned unset_offsets[]={0xccu,0xd0u,0xd4u};
  const unsigned color_offsets[]={0xbcu,0xc0u,0xd8u};
  for(unsigned offset:unset_offsets)Write32(object+offset,0xffffffff);
  for(unsigned offset:color_offsets)Write32(object+offset,0xff000000);
  Write32(object+0x94,7);Write32(object+0x98,6);Write32(object+0x9c,2);Write32(object+0xa0,2);Write32(object+0x60,1);Write32(object+0xec,0x8040);
  unsigned style=Read32(object+8);if(style&0x800)style|=0x8000;if(!(style&3))style|=1;Write32(object+8,style);User::WindowLong(hwnd,-16,style,true);User::WindowLong(hwnd,0,object,true);
  // Original bitmap-size initialization reaches the common size negotiator
  // with 16x16 and padding enabled. Bitmap strips remain a separate provider.
  object=t->object;Write32(object+0x68,16);Write32(object+0x6c,16);
  if(!Win98ToolbarLayout::SetSize(e,object,16,16,1)){ReleaseToolbar(*t);return 0;}
  if(t->destroy_pending){ReleaseToolbar(*t);return 0;}return 1;
 }
 if(!t)return 0;
 if(message==0x82){if(t->active)t->destroy_pending=true;else ReleaseToolbar(*t);return 0;}
 if(message==0x200||message==0x201||message==0x202||message==0x203||message==0x215||message==0x2a3||message==0x900){
  ToolbarMemory e(*this,*t);unsigned object=t->object;
  // Hold the original object until all synchronous parent callbacks unwind.
  // Existing button-list mutation guards remain in force during callbacks.
  ++t->active;
  if(t->layout_pending)LayoutToolbar(*t);
  const unsigned index=Win98ToolbarPaint::Hit(e,object,short(lp),short(lp>>16));
  bool unsupported=false;
  if(int(index)>=0&&index<Read32(object+0x84))unsupported|=(e.Read8(Read32(object+0x30)+28*index+9)&0x88)!=0;
  if(unsupported&&(message==0x201||message==0x203)){last_error=50;}
  else if(message==0x200)Win98ToolbarInput::Move(e,object,hwnd,message,wp,lp);
  else if(message==0x201)Win98ToolbarInput::Down(e,object,hwnd,message,wp,lp);
  else if(message==0x202)Win98ToolbarInput::Up(e,object,hwnd,message,wp,lp);
  else if(message==0x203)Win98ToolbarInput::Press(e,object,lp,index); // Original.
  else if(message==0x215){if(Read32(object+0x1c)>=5)User::PostMessage(hwnd,0x900,0,0);}
  else if(message==0x900)Win98ToolbarInput::Cancel(e,object);
  else if(message==0x2a3)Win98ToolbarInput::Leave(e,object);
  --t->active;if(t->destroy_pending&&!t->active)ReleaseToolbar(*t);return 0;
 }
 if(message==1||message==2)return 0;
 if(message==5){t->layout_pending=true;return 0;}
 if(message==0x30){
  unsigned font=wp?wp:system;if(!ToolbarFontDC(font)){last_error=87;return 0;}ToolbarFontRelease();
  Write32(t->object+0x4c,font);LayoutToolbar(*t);if(lp)User::InvalidateRect(hwnd,nullptr,true);return 1;
 }
 if(message==0x31)return Read32(t->object+0x4c);
 if(message==0x455)return Read32(t->object+0x24);
 if(message==0x454){unsigned old=Read32(t->object+0x24),next=wp?(old&~wp)|(lp&wp):lp;
  if(t->active){last_error=170;return old;}if(next&6){last_error=50;return old;}
  ToolbarMemory e(*this,*t);Win98ToolbarInput::Extended(e,t->object,lp,wp);return old;
 }
 if(message==0xf){User::ValidateRect(hwnd,nullptr);return 0;}
 if(message==0x14)return 1;
 if(message==0x318){Gdi::RECT r{};User::GetClientRect(hwnd,&r);DrawToolbar(hwnd,wp,r);User::DrawingChanged(wp);return 0;}
 if(message==0x420||message==0x421||message==0x428||message==0x42f||
    (message>=0x430&&message<=0x431)||(message>=0x434&&message<=0x437)||
    message==0x43c||message==0x43d||message==0x445||message==0x456||message==0x457){
  if(t->active){last_error=170;return 0;}ToolbarMemory e(*this,*t);unsigned object=t->object;
  if(message==0x420){int w=short(lp),h=short(lp>>16);if(w<-1||h<-1||w>1024||h>1024){last_error=87;return 0;}return Win98ToolbarPaint::BitmapSize(e,object,unsigned(w),unsigned(h));}
  if(message==0x428)return Win98ToolbarPaint::Rows(e,object);
  if(message==0x456||message==0x457)return Win98ToolbarPaint::Padding(e,object,message==0x456?~0u:lp);
  if(message==0x42f){Write32(object+0x90,wp);Win98Toolbar::Dirty(e,object);e.Invalidate(hwnd,0,1);return 1;}
  if(message==0x43d)return Read32(object+0x60);
  if(message==0x43c){if(wp>1){last_error=50;return 0;}if(Read32(object+0x60)!=wp){Write32(object+0x60,wp);LayoutToolbar(*t);e.Invalidate(hwnd,0,1);}return 1;}
  if(message==0x445){if(!lp)return ~0u;int point[2];if(!Read(lp,point,8))return ~0u;return Win98ToolbarPaint::Hit(e,object,point[0],point[1]);}
  if(message==0x421){LayoutToolbar(*t);AutosizeToolbar(*t);return 0;}
  unsigned kind=message>=0x436?2:message>=0x434?1:0;
  if(message&1)return Win98ToolbarPaint::Images(e,object,kind,wp);
  if(lp&&!FindImageList(lp)){last_error=87;return 0;}
  unsigned old=Win98ToolbarPaint::SetImages(e,object,kind,wp,lp);
  if(!kind){Write32(object+0xec,Read32(object+0xec)|2);if(!Read32(object+0x64))Write32(object+0x64,20);if(!wp){
   unsigned w=0,h=0;if(auto*l=FindImageList(lp)){w=l->width;h=l->height;}
   if(!Win98ToolbarPaint::BitmapSize(e,object,w,h))return old;
  }}
  // Image-list ownership stays with the caller, as in the original toolbar.
  User::InvalidateRect(hwnd,nullptr,true);return old;
 }
 if(message==0x413){
  if(t->active||!lp){last_error=87;return ~0u;}unsigned add[2];if(!Read(lp,add,8))return ~0u;
  if(add[0]==~0u){last_error=50;return ~0u;} // Standard COMCTL bitmap resources need their own module provider.
  ToolbarMemory e(*this,*t);unsigned object=t->object;
  // Updating the original last-group count does not reload the strip. Bound
  // that metadata change by the actually installed pixels in the native list.
  unsigned total=0,groups=Read32(object+0x50),table=Read32(object+0x54);
  for(unsigned i=0;i<groups;++i){unsigned p=table+i*12,count=Read32(p);
   if(i+1==groups&&Read32(p+4)==add[0]&&Read32(p+8)==add[1]&&wp>count&&wp>e.ImageCount(e.ImageList(object,0,0))-total){last_error=87;return ~0u;}total+=count;}
  return Win98ToolbarBitmap::Add(e,object,wp,add[0],add[1]);
 }
 if(message==0x41d||message==0x433||message==0x41f||message==0x43a){
  unsigned object=t->object,count=Read32(object+0x84);if(count>AllocationSize(Read32(object+0x30))/28){last_error=87;return 0;}
  ToolbarMemory e(*this,*t);
  if(message==0x43a)return (Read32(object+0x70)&65535)|((Read32(object+0x74)&65535)<<16);
  if(message==0x41f){if(t->active){last_error=170;return 0;}return Win98ToolbarLayout::SetSize(e,object,unsigned(short(lp&65535)),unsigned(short(lp>>16)),0);}
  if(!lp)return 0;unsigned index=message==0x433?Win98Toolbar::CommandIndex(e,object,wp):wp;return Win98ToolbarLayout::Rect(e,object,index,lp);
 }
 if(Win98Toolbar::ObjectMessage(message)){
  const bool read_only=(message>=0x409&&message<=0x40e)||message==0x412||message==0x417||message==0x418||message==0x419||message==0x43f||message==0x441||message==0x44a;
  // Native allocation guards around original unchecked arithmetic/write paths.
  if(t->active&&!read_only){last_error=170;return 0;}
  unsigned object=t->object,count=Read32(object+0x84),size=Read32(object+0x64);
  if(count>AllocationSize(Read32(object+0x30))/28){last_error=87;return 0;}
  if(message==0x41e&&wp!=12&&wp!=20){last_error=87;return 0;}
  if(message==0x414||message==0x444||message==0x415||message==0x443){
   unsigned add=(message==0x414||message==0x444)?wp:1;
   if(!add)return 0; // Avoid original zero-size LocalFree/stale-array-pointer path.
   if((size!=12&&size!=20)||add>0x7fffffffu/28-count||lp>0xffffffffu-add*size){last_error=87;return 0;}
  }
  if((message==0x440||message==0x442)&&Read32(lp)==32&&(Read32(lp+4)&0x80000000)&&wp>=count){last_error=87;return 0;}
  if(message==0x442&&(Read32(lp+4)&2)){unsigned a[]={0,0,Read32(lp+24),0xffffffff,0,0};unsigned n=NlsApi(594,a);if(!n||n>256){last_error=87;return 0;}}
  ToolbarMemory e(*this,*t);++t->active;unsigned result=Win98Toolbar::Message(e,object,message,wp,lp);--t->active;
  if(t->destroy_pending&&!t->active)ReleaseToolbar(*t);return result;
 }
 // The base registry record is one command
 // identifier per button. Separators use -1 when visible and -2 when hidden.
 if(message==0x41a){
  if(!lp||t->active)return 0;unsigned p[3]{};if(!Read(lp,p,sizeof p)||!p[1]||!p[2])return 0;
  char path[512],value_name[260];snprintf(path,sizeof path,"%s",ReadString(p[1]));snprintf(value_name,sizeof value_name,"%s",ReadString(p[2]));
  unsigned key=0,node=0;bool created=false;unsigned error=registry.Open(p[0],path,wp!=0,key,created);if(error)return 0;
  error=registry.Key(key,node);if(error){registry.Close(key);return 0;}
  ToolbarMemory e(*this,*t);unsigned object=t->object,old_count=Read32(object+0x84),buttons=Read32(object+0x30);
  if(old_count>AllocationSize(buttons)/28){registry.Close(key);last_error=87;return 0;}
  ++t->active;
  if(wp){
   unsigned data=0,current=0,capacity=old_count*4;
   auto valid=[&](){return (!capacity&&!data&&!current)||(data&&current>=data&&current<=data+capacity&&data+capacity>=data&&capacity<=AllocationSize(data));};
   {ToolbarMemory::Scratch note(e,60);for(unsigned i=0;i<60;i+=4)e.Write32(note.address+i,0);
    e.Write32(note.address+20,capacity);e.Write32(note.address+24,~0u);e.Write32(note.address+28,old_count);e.Notify(object,0xfffffd2e,note.address);
    data=e.Read32(note.address+12);current=e.Read32(note.address+16);capacity=e.Read32(note.address+20);}
   // Win98 also supplies the Shell-only stream when a parent has no private data.
   if(!data&&capacity==old_count*4){data=capacity?e.AllocateLocal(0,capacity):0;current=data;}
   bool release_data=valid();if(!release_data)error=13;
   for(unsigned i=0;!error&&i<old_count&&!t->destroy_pending;++i){
    if(current>data+capacity||data+capacity-current<4){error=13;break;}unsigned button=buttons+i*28,command=e.Read32(button+4);
    e.Write32(current,command?command:((~e.Read8(button+8)>>3)|0xfffffffeu));current+=4;
    ToolbarMemory::Scratch note(e,60);for(unsigned j=0;j<60;j+=4)e.Write32(note.address+j,0);
    e.Write32(note.address+12,data);e.Write32(note.address+16,current);e.Write32(note.address+20,capacity);e.Write32(note.address+24,i);e.Write32(note.address+28,old_count);
    Win98Toolbar::ToPublic(e,object,button,note.address+32);e.Notify(object,0xfffffd2e,note.address);
    data=e.Read32(note.address+12);current=e.Read32(note.address+16);capacity=e.Read32(note.address+20);release_data=valid();if(!release_data)error=13;
   }
   if(!error&&!t->destroy_pending){unsigned char*copy=capacity?new unsigned char[capacity]:nullptr;if(capacity&&!Read(data,copy,capacity))error=13;
    if(!error)error=registry.store.Set(node,value_name,3,copy,capacity);delete[] copy;}
   if(release_data&&data)e.FreeLocal(data);
  }else{
   const Registry::Value*stored=nullptr;error=registry.store.Query(node,value_name,stored);unsigned size=!error?stored->size:0,type=!error?stored->type:0;
   unsigned char*saved=!error&&size?new unsigned char[size]:nullptr;if(!error&&size)memcpy(saved,stored->data,size);
   if(!error&&(type!=3||(size&3)))error=13;unsigned data=!error&&size?e.AllocateLocal(0,size):0;
   if(!error&&size&&!data)error=8;if(!error&&size)Store(data,saved,size);delete[] saved;
   unsigned current=data,count=size/4,record_bytes=4,ordered=0,restored=0;bool transferred=false;
   if(!error){ToolbarMemory::Scratch note(e,64);for(unsigned i=0;i<64;i+=4)e.Write32(note.address+i,0);
    e.Write32(note.address+12,data);e.Write32(note.address+16,current);e.Write32(note.address+20,size);e.Write32(note.address+24,~0u);e.Write32(note.address+28,count);e.Write32(note.address+32,record_bytes);
    if(e.Notify(object,0xfffffd2f,note.address))error=1223;current=e.Read32(note.address+16);count=e.Read32(note.address+28);record_bytes=e.Read32(note.address+32);
    if(!error&&(count>0x7fffffffu/28||record_bytes<4||current<data||current>data+size||data+size<data))error=13;
    if(!error&&count){ordered=Allocate(count*28);if(!ordered)error=8;}}
   for(unsigned i=0;!error&&i<count&&!t->destroy_pending;++i){
    if(current>data+size||data+size-current<4){error=13;break;}unsigned record=e.Read32(current);current+=4;unsigned private_start=current;
    ToolbarMemory::Scratch note(e,64);for(unsigned j=0;j<64;j+=4)e.Write32(note.address+j,0);
    e.Write32(note.address+12,data);e.Write32(note.address+16,current);e.Write32(note.address+20,size);e.Write32(note.address+24,i);e.Write32(note.address+28,count);e.Write32(note.address+32,record_bytes);
    if(record>=0xfffffffeu){e.Write32(note.address+40,0);e.Write8(note.address+44,record==0xfffffffeu?12:4);e.Write8(note.address+45,1);}
    else e.Write32(note.address+40,record);
    if(e.Notify(object,0xfffffd2f,note.address)){error=1223;break;}current=e.Read32(note.address+16);
    if(current<private_start||current>data+size||current-private_start!=record_bytes-4){error=13;break;}
    if(e.Read32(note.address+40)||(e.Read8(note.address+45)&1)){unsigned destination=ordered+restored*28;Win98Toolbar::FromPublic(e,object,destination,note.address+36);
     e.Write32(destination+16,(e.Read8(destination+9)&1)?0xffffffff:0);++restored;}
   }
   bool*available=restored?new bool[restored]{}:nullptr;
   if(!error){for(unsigned index=0;!error&&!t->destroy_pending;++index){ToolbarMemory::Scratch info(e,64);for(unsigned j=0;j<64;j+=4)e.Write32(info.address+j,0);e.Write32(info.address+12,index);
     if(!e.Notify(object,0xfffffd44,info.address))break;unsigned command=e.Read32(info.address+20);
     if(!command&&(e.Read8(info.address+25)&1))for(unsigned i=0;i<restored;++i)if(!available[i]&&(e.Read8(ordered+i*28+9)&1)){available[i]=true;break;}
     for(unsigned i=0;i<restored;++i)if(!available[i]&&command&&e.Read32(ordered+i*28+4)==command&&!(e.Read8(ordered+i*28+9)&1)){
       unsigned destination=ordered+i*28;Win98Toolbar::FromPublic(e,object,destination,info.address+16);unsigned text=e.Read32(destination+16);e.Write32(destination+16,0);
       if(text==0xffffffff||!(text&0xffff0000))e.Write32(destination+16,text);else {unsigned converted=e.AnsiToWide(e.Read32(object+0x14),text);if(!converted){error=8;break;}
        unsigned assigned=e.SetString(destination+16,converted);e.FreeLocal(converted);if(!assigned){error=8;break;}}available[i]=true;}
     if(index==0xffffffff){error=87;break;}}}
   if(!error){unsigned kept=0;for(unsigned i=0;i<restored;++i){unsigned source=ordered+i*28;if(available[i]){if(kept!=i)for(unsigned j=0;j<28;j+=4)e.Write32(ordered+kept*28+j,e.Read32(source+j));++kept;}
     else {unsigned text=e.Read32(source+16);if(text!=0xffffffff&&(text&0xffff0000))e.SetString(source+16,0);}}restored=kept;}
   delete[] available;
   if(!error&&!t->destroy_pending&&restored&&!Win98Toolbar::Reserve(e,object,restored))error=8;
   if(!error&&!t->destroy_pending){buttons=e.Read32(object+0x30);for(unsigned i=0;i<old_count;++i){unsigned text=e.Read32(buttons+i*28+16);if(text!=0xffffffff&&(text&0xffff0000))e.SetString(buttons+i*28+16,0);}
     if(!restored&&buttons){e.FreeLocal(buttons);e.Write32(object+0x30,0);buttons=0;}
     if(restored){unsigned char*copy=new unsigned char[restored*28];Read(ordered,copy,restored*28);Store(buttons,copy,restored*28);delete[] copy;}
     e.Write32(object+0x84,restored);transferred=true;Win98Toolbar::Dirty(e,object);e.Layout(object);e.Invalidate(e.Read32(object),0,1);}
   if(!transferred&&ordered)for(unsigned i=0;i<restored;++i){unsigned text=e.Read32(ordered+i*28+16);if(text!=0xffffffff&&(text&0xffff0000))e.SetString(ordered+i*28+16,0);}
   if(data)e.FreeLocal(data);if(ordered)Free(ordered);
  }
  --t->active;registry.Close(key);if(t->destroy_pending&&!t->active)ReleaseToolbar(*t);return error?0:1;
 }
 if(message>=0x400){char text[120];snprintf(text,sizeof text,"Toolbar message %04x is not ported yet",message);Fail(text);return 0;}
 return DefaultWindowMessage(hwnd,message,wp,lp);
}
}
