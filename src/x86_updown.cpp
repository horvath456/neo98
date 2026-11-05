// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "win98_updown.h"
#include "user_draw.h"
#include <string.h>
namespace X86 {
namespace {constexpr unsigned BuddyProcedure=0x71000189,Bytes=0x850,Refs=0x804,Dead=0x808,OldProc=0x80c,Hook=0x810;const char BuddyKey[]="NEO98.UpDown.Buddy";}
struct Application::UpDownEnvironment {
 Application&app;unsigned hwnd;
 unsigned Read32(unsigned p){return app.Read32(p);}void Write32(unsigned p,unsigned v){app.Write32(p,v);}
 unsigned Allocate(unsigned n){unsigned a[]={64,n};return app.Api("LocalAlloc",a);}void Free(unsigned p){if(p)app.Api("LocalFree",&p);}
 unsigned Property(unsigned buddy,const char*operation,unsigned value=0){unsigned key=Allocate(sizeof BuddyKey);if(!key)return 0;app.Store(key,BuddyKey,sizeof BuddyKey);unsigned a[]={buddy,key,value};unsigned result=app.PropertyApi(operation,a);Free(key);return result;}
 bool Alive(unsigned t){unsigned current=0;return User::IsWindow(hwnd)&&User::WindowLong(hwnd,0,current,false)&&current==t&&!Read32(t+Dead)&&app.state!=Failed&&app.state!=Exited;}
 struct Guard {UpDownEnvironment&e;unsigned t;Guard(UpDownEnvironment&x,unsigned p):e(x),t(p){e.Write32(t+Refs,e.Read32(t+Refs)+1);}~Guard(){unsigned refs=e.Read32(t+Refs)-1;e.Write32(t+Refs,refs);if(!refs&&e.Read32(t+Dead)&&!e.Read32(t+Hook))e.Free(t);}};
 void Dirty(unsigned){User::InvalidateRect(hwnd,nullptr,false);}bool Enabled(unsigned){return User::IsEnabled(hwnd);}unsigned Now(){return app.Api("GetTickCount",nullptr);}
 unsigned Reallocate(unsigned t,unsigned n){if(n>0x800){app.last_error=8;return 0;}return t;}void Relocated(unsigned){} // Stable native reserve, same original variable-length record.
 void DivideFault(){app.Fail("UpDown acceleration reached the original signed divide exception");}
 bool ValidBuddy(unsigned t){unsigned buddy=Read32(t+0x24);if(buddy&&(!User::IsWindow(buddy)||User::WindowProcess(buddy)!=User::WindowProcess(hwnd))){Write32(t+0x24,0);buddy=0;}return buddy!=0;}
 unsigned ListPosition(unsigned t){return app.Send(Read32(t+0x24),0x188,0,0);}
 void BuddyText(unsigned t,unsigned short*out,unsigned capacity){unsigned scratch=Allocate(160);if(!scratch)return;unsigned a[]={Read32(t+0x24),scratch,36};app.Api("GetWindowTextA",a);if(Alive(t)){unsigned n[]={0,0,scratch,~0u,scratch+64,36};if(app.NlsApi(594,n))app.Read(scratch+64,out,capacity*2);}out[capacity-1]=0;Free(scratch);}
 unsigned short Thousands(){unsigned scratch=Allocate(16);if(!scratch)return ',';unsigned a[]={0x400,0xf,scratch,8};unsigned short result=',';if(app.Api("GetLocaleInfoA",a)){unsigned n[]={0,0,scratch,~0u,scratch+8,4};if(app.NlsApi(594,n))app.Read(scratch+8,&result,2);}Free(scratch);return result;}
 void WriteBuddy(unsigned t){
  if(!ValidBuddy(t)||!(Read32(t+8)&2))return;unsigned wanted=Read32(t+0x38),scratch=Allocate(320);if(!scratch)return;
  if(Read32(t+0x68)){unsigned current=Win98UpDown::GetPosition(*this,t,scratch);if(!Alive(t)){Free(scratch);return;}if(current==wanted&&!Read32(scratch)){Free(scratch);return;}}
  Write32(t+0x68,Read32(t+0x68)+1);Write32(t+0x38,wanted);unsigned buddy=Read32(t+0x24);
  if(Read32(t+0x3c)==2){app.Send(buddy,0x186,wanted,0);if(Alive(t))app.Send(User::Parent(buddy),0x111,(User::ControlID(buddy)&65535)|0x10000,buddy);Free(scratch);return;}
  char text[64]{},reverse[32];unsigned length=0,n=0,value=wanted;bool hex=Read32(t+0x2c)==16;
  if(hex){text[length++]='0';text[length++]='x';unsigned digits=int(Read32(t+0x30)|Read32(t+0x34))<65536?4:8;do{reverse[n++]="0123456789ABCDEF"[value&15];value>>=4;}while(value);while(n<digits)reverse[n++]='0';}
  else{if(int(value)<0){text[length++]='-';value=0-value;}do{reverse[n++]=char('0'+value%10);value/=10;}while(value);}
  while(n)text[length++]=reverse[--n];text[length]=0;app.Store(scratch,text,length+1);unsigned output=scratch;
  unsigned magnitude=int(wanted)<0?0-wanted:wanted;
  if(!hex&&magnitude>=1000&&!(Read32(t+8)&0x80)){
   // Windows 98 uses the original NUMBERFMT provider with zero fractional digits
   // and locale grouping/separator. The minus sign is outside that provider.
   unsigned a[]={0x400,0xf,scratch+96,16};app.Api("GetLocaleInfoA",a);a[1]=0x10;a[2]=scratch+112;app.Api("GetLocaleInfoA",a);char grouping[16]{};app.Read(scratch+112,grouping,16);unsigned group=0;for(unsigned i=0;i<15&&grouping[i]>='0'&&grouping[i]<='9';++i)group=group*10+grouping[i]-'0';
   Write32(scratch+128,0);unsigned format[]={0,0,group,scratch+128,scratch+96,0};app.Store(scratch+144,format,24);unsigned negative=int(wanted)<0;unsigned f[]={0x400,0,scratch+negative,scratch+144,scratch+192+negative,17};if(app.NumberFormatApi("GetNumberFormatA",f)){if(negative){char minus='-';app.Store(scratch+192,&minus,1);}output=scratch+192;}else{Free(scratch);app.Fail("UpDown locale number formatting failed");return;}
  }
  unsigned a[]={buddy,output};app.Api("SetWindowTextA",a);Free(scratch);
 }
 unsigned Notify(unsigned t,unsigned position,unsigned&delta){unsigned p=Allocate(20);if(!p)return 1;unsigned notice[]={hwnd,User::ControlID(hwnd),0xfffffd2e,position,delta};app.Store(p,notice,20);unsigned veto=app.Send(User::Parent(hwnd),0x4e,notice[1],p);delta=Read32(p+16);Free(p);return veto;}
 void Scroll(unsigned t,unsigned code){app.Send(User::Parent(hwnd),Read32(t+8)&0x40?0x114:0x115,(Read32(t+0x38)<<16)|code,hwnd);}
 void Detach(unsigned t){unsigned buddy=Read32(t+Hook);if(buddy&&User::IsWindow(buddy)){if(User::Procedure(buddy)==BuddyProcedure)User::SetProcedure(buddy,Read32(t+OldProc));Property(buddy,"RemovePropA");}Write32(t+Hook,0);}
 unsigned SetBuddy(unsigned t,unsigned buddy){unsigned old=Read32(t+0x24);if(!buddy&&(Read32(t+8)&0x10))buddy=User::GetWindow(hwnd,3);if(buddy&&(!User::IsWindow(buddy)||User::WindowProcess(buddy)!=User::WindowProcess(hwnd))){app.last_error=1400;return old;}
  if(buddy&&(Read32(t+8)&0x20)){unsigned existing=Property(buddy,"GetPropA");if(existing&&existing!=t){app.Fail("Multiple UpDown buddy subclass chains are not ported");return old;}}
  Detach(t);Write32(t+0x24,buddy);Write32(t+0x3c,0);if(!buddy)return old;
  char klass[32]{};User::GetClassName(buddy,klass,sizeof klass);if(!strcmp(klass,"EDIT")||!strcmp(klass,"Edit"))Write32(t+0x3c,1);else if(!strcmp(klass,"LISTBOX")||!strcmp(klass,"ListBox"))Write32(t+0x3c,2);
  if(Read32(t+8)&0x20){if(!Property(buddy,"SetPropA",t)){Write32(t+0x24,0);return old;}Write32(t+OldProc,User::SetProcedure(buddy,BuddyProcedure));Write32(t+Hook,buddy);}return old;
 }
};
unsigned Application::UpDownMessage(unsigned hwnd,unsigned message,unsigned wp,unsigned lp){
 // -> : NCCALCSIZE reaches DefWindowProc before state exists.
 if(message==0x83)return DefaultWindowMessage(hwnd,message,wp,lp);
 unsigned t=0;User::WindowLong(hwnd,0,t,false);UpDownEnvironment e{*this,hwnd};
 if(message==1){unsigned style=User::Style(hwnd);if((style&0xff0c)||(User::ExtendedStyle(hwnd)&0x406000)){Fail("UpDown alignment/unsupported style provider is not ported");return ~0u;}t=e.Allocate(Bytes);if(!t)return ~0u;unsigned v=t;User::WindowLong(hwnd,0,v,true);Write32(t,hwnd);Write32(t+4,User::Parent(hwnd));Write32(t+8,style);Write32(t+0x2c,10);Write32(t+0x34,100);Write32(t+0x48,3);Write32(t+0x50,1);Write32(t+0x54,2);Write32(t+0x58,5);Write32(t+0x5c,5);Write32(t+0x60,20);UpDownEnvironment::Guard guard(e,t);e.SetBuddy(t,0);e.WriteBuddy(t);return 0;}
 if(!t)return User::DefaultMessage(hwnd,message,wp,lp);UpDownEnvironment::Guard guard(e,t);
 if(message==2){Write32(t+Dead,1);unsigned timer[]={hwnd,1};Api("KillTimer",timer);if(Api("GetCapture",nullptr)==hwnd)Api("ReleaseCapture",nullptr);Write32(t+0x28,Read32(t+0x28)|0x20);unsigned zero=0;User::WindowLong(hwnd,0,zero,true);return 0;}
 if(message==0xc||message==0xd||message==0xe)return WindowTextDefault(hwnd,message,wp,lp);
 if(message==0x465){Write32(t+0x30,unsigned(int(short(lp))));Write32(t+0x34,unsigned(int(short(lp>>16))));Win98UpDown::Clamp(e,t);return 0;}
 if(message==0x466)return (Read32(t+0x34)<<16)|(Read32(t+0x30)&65535);
 if(message==0x46f){Write32(t+0x30,lp);Write32(t+0x34,wp);return 0;}
 if(message==0x470){if(lp)Write32(lp,Read32(t+0x30));if(wp)Write32(wp,Read32(t+0x34));return 0;}
 if(message==0x467||message==0x471)return Win98UpDown::SetPosition(e,t,message==0x467?unsigned(int(short(lp))):lp);
 if(message==0x468){unsigned error=e.Allocate(4);if(!error)return 0x10000;unsigned p=Win98UpDown::GetPosition(e,t,error),bad=Read32(error);e.Free(error);return (p&65535)|(bad<<16);}
 if(message==0x472)return Win98UpDown::GetPosition(e,t,lp);
 if(message==0x469)return e.SetBuddy(t,wp);
 if(message==0x46a)return Read32(t+0x24);
 if(message==0x46b){if(wp>240||(!lp&&wp)){last_error=87;return 0;}return Win98UpDown::SetAccel(e,t,wp,lp);}
 if(message==0x46c){if(wp&&!lp){last_error=87;return 0;}return Win98UpDown::GetAccel(e,t,wp,lp);}
 if(message==0x46d)return Win98UpDown::SetBase(e,t,wp);
 if(message==0x46e)return Read32(t+0x2c);
 if(message==0x2005){unsigned old=Read32(t+0x10)&1;Write32(t+0x10,(Read32(t+0x10)&~1u)|(wp?1:0));return old;}
 if(message==0x2006)return Read32(t+0x10)&1;
 if(message==0xa||message==5){e.Dirty(t);return 0;}
 if(message==0xf||message==0x318){User::Paint paint{};unsigned dc=message==0x318?wp:User::BeginPaint(hwnd,paint);if(dc){Gdi::RECT r{};User::GetClientRect(hwnd,&r);DrawUpDown(hwnd,dc,r);User::DrawingChanged(dc);if(message==0xf)User::EndPaint(hwnd,paint);}return 0;}
 if(message==0x14)return 1;
 if(message==0x201){if(!User::IsEnabled(hwnd)||(e.ValidBuddy(t)&&!User::IsEnabled(Read32(t+0x24))))return 0;Api("SetCapture",&hwnd);Win98UpDown::GetPosition(e,t,0);if(!e.Alive(t))return 0;if(Read32(t+0x3c))SetFocus(Read32(t+0x24));if(!e.Alive(t))return 0;Gdi::RECT r{};User::GetClientRect(hwnd,&r);unsigned half=Win98UpDown::Hit(Read32(t+8),r.right,r.bottom,short(lp),short(lp>>16));if(half){Write32(t+0x40,half==1);Win98UpDown::Press(e,t,half==2,half==1);unsigned timer[]={hwnd,1,530,0};Api("SetTimer",timer);Win98UpDown::Step(e,t);}return 0;}
 if(message==0x113&&wp==1){if(Api("GetCapture",nullptr)!=hwnd){UpDownMessage(hwnd,0x202,0,0);return 0;}unsigned timer[]={hwnd,1,100,0};Api("SetTimer",timer);Gdi::RECT r{};User::GetWindowRect(hwnd,&r);unsigned direction=Read32(t+0x40);int middle=Read32(t+8)&0x40?(r.left+r.right)/2:(r.top+r.bottom)/2;bool inside=mouse_x>=r.left&&mouse_x<r.right&&mouse_y>=r.top&&mouse_y<r.bottom;if(Read32(t+8)&0x40)inside=inside&&(direction?mouse_x<middle:mouse_x>middle);else inside=inside&&(direction?mouse_y>middle:mouse_y<middle);Win98UpDown::Press(e,t,inside&&!direction,inside&&direction);if(inside)Win98UpDown::Step(e,t);return 0;}
 if(message==0x202||message==0x1f||message==0x215){if(Read32(t+Dead))return 0;if((Api("GetCapture",nullptr)==hwnd||message==0x215)&&!Read32(t+0x818)){Write32(t+0x818,1);Win98UpDown::Press(e,t,false,false);unsigned timer[]={hwnd,1};Api("KillTimer",timer);if(Api("GetCapture",nullptr)==hwnd)Api("ReleaseCapture",nullptr);if(e.Alive(t)){if(Read32(t+0x3c)==1)Send(Read32(t+0x24),0xb1,0,~0u);if(e.Alive(t))e.Scroll(t,8);}Write32(t+0x818,0);}return 0;}
 if(message>=0x400&&message<0x500){Fail("UpDown message is not ported");return 0;}
 return User::DefaultMessage(hwnd,message,wp,lp);
}
unsigned Application::UpDownBuddyMessage(unsigned buddy,unsigned message,unsigned wp,unsigned lp){
 UpDownEnvironment e{*this,0};unsigned t=e.Property(buddy,"GetPropA");if(!t)return User::DefaultMessage(buddy,message,wp,lp);e.hwnd=Read32(t);UpDownEnvironment::Guard guard(e,t);unsigned original=Read32(t+OldProc);
 auto next=[&](){if(!original)return User::IsEdit(buddy)?Edit(buddy,message,wp,lp):DefaultWindowMessage(buddy,message,wp,lp);unsigned a[]={original,buddy,message,wp,lp};return Api("CallWindowProcA",a);};
 if(message==0x82){e.Detach(t);Write32(t+0x24,0);return next();}
 if(message==0x87)return next()|1;
 if((message==0x100||message==0x101)&&(wp==0x26||wp==0x28)&&!Read32(t+Dead)){
  if(Api("GetCapture",nullptr)!=e.hwnd){if(message==0x101)Win98UpDown::Press(e,t,false,false);else{if(!(lp&0x40000000))Win98UpDown::GetPosition(e,t,0);if(!e.Alive(t))return 0;unsigned down=wp==0x28;Write32(t+0x40,down);Win98UpDown::Press(e,t,!down,down);Win98UpDown::Step(e,t);}}
  return 0;
 }
 return next();
}
void Application::DrawUpDown(unsigned hwnd,unsigned dc,const Gdi::RECT&bounds){
 unsigned t=0;if(!User::WindowLong(hwnd,0,t,false)||!t)return;UpDownEnvironment e{*this,hwnd};unsigned style=Read32(t+8),bits=Read32(t+0x28);bool disabled=!User::IsEnabled(hwnd)||Read32(t+0x30)==Read32(t+0x34);
 auto color=[&](unsigned id){return Api("GetSysColor",&id);};auto fill=[&](Gdi::RECT r,unsigned c){unsigned brush=Gdi::CreateSolidBrush(c);if(brush){Gdi::FillRect(dc,&r,brush);Gdi::DeleteObject(brush);}};
 int saved=Gdi::SaveDC(dc);if(!saved)return;Gdi::IntersectClipRect(dc,bounds.left,bounds.top,bounds.right,bounds.bottom);fill(bounds,color(15));
 for(unsigned half=1;half<=2;++half){Gdi::RECT r=bounds;if(style&0x40){int middle=(r.left+r.right)/2;if(half==1)r.right=middle;else r.left=middle;}else{int middle=(r.top+r.bottom)/2;if(half==1)r.top=middle;else r.bottom=middle;}
  bool pressed=bits&(half==1?2:1);Gdi::RECT edge=r;Win98Draw::ButtonFrame(dc,&edge,(pressed?0x200:0)|16,0x1000);int cx=(r.left+r.right)/2+pressed,cy=(r.top+r.bottom)/2+pressed;unsigned ink=color(disabled?17:18);
  // ARM raster provider for DFCS_SCROLL arrows; original halves/pressed flags
  // feed the shared implemented USER bevel routine above.
  for(int i=0;i<4;++i)if(style&0x40){int x=half==1?cx-2+i:cx+2-i;fill({x,cy-i,x+1,cy+i+1},ink);}else{int y=half==1?cy+2-i:cy-2+i;fill({cx-i,y,cx+i+1,y+1},ink);}
 }
 Gdi::RestoreDC(dc,saved);
}
}

#include "x86_trackbar.inc"
