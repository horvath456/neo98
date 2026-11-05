// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "win98_tooltip.h"
#include <string.h>
namespace X86 {
namespace {constexpr unsigned References=0xa4,Dead=0xa8,Hiding=0xac,Showing=0xb0,DC=0xb4,RelayWindow=0xb8,Bytes=0xc0;}
struct Application::TooltipEnvironment {
 Application&app;unsigned hwnd;
 unsigned Read32(unsigned p){return app.Read32(p);}void Write32(unsigned p,unsigned v){app.Write32(p,v);}
 unsigned Allocate(unsigned n){unsigned a[]={64,n};return app.Api("LocalAlloc",a);}void Free(unsigned p){if(p)app.Api("LocalFree",&p);}
 unsigned Resize(unsigned p,unsigned bytes){if(bytes>44*256){app.last_error=8;return 0;}if(!p)return Allocate(44*256);return p;}
 void Copy(unsigned to,unsigned from,unsigned n){unsigned char bytes[256];if(to==from)return;while(n){unsigned part=n>256?256:n;app.Read(from,bytes,part);app.Store(to,bytes,part);to+=part;from+=part;n-=part;}}
 bool Alive(unsigned t){unsigned current=0;return User::IsWindow(hwnd)&&User::WindowLong(hwnd,0,current,false)&&current==t&&!Read32(t+Dead)&&app.state!=Failed&&app.state!=Exited;}
 struct Guard{TooltipEnvironment&e;unsigned t;Guard(TooltipEnvironment&x,unsigned p):e(x),t(p){e.Write32(t+References,e.Read32(t+References)+1);}~Guard(){unsigned n=e.Read32(t+References)-1;e.Write32(t+References,n);if(!n&&e.Read32(t+Dead))e.Free(t);}};
 bool ValidWindow(unsigned h){return User::IsWindow(h)&&User::WindowProcess(h)==User::WindowProcess(hwnd);}
 unsigned ToWide(unsigned text){unsigned a[]={0,0,text,~0u,0,0};unsigned n=app.NlsApi(594,a);if(!n||n>8192){app.Fail("Tooltip text exceeds native conversion capacity");return 0;}unsigned out=Allocate(n*2);if(!out)return 0;a[4]=out;a[5]=n;if(!app.NlsApi(594,a)){Free(out);return 0;}return out;}
 bool SetString(unsigned field,unsigned text){unsigned a[]={field,text};return app.CommonControlApi(236,a)!=0;}
 unsigned WideLength(unsigned p){if(!p)return 0;for(unsigned n=0;n<8192;++n){unsigned short c=0;app.Read(p+n*2,&c,2);if(!c)return n;}app.Fail("Tooltip text exceeds native string capacity");return 0;}
 void CopyText(unsigned out,unsigned text,bool wide){if(wide)Copy(out,text,(WideLength(text)+1)*2);else{unsigned a[]={0,0,text,~0u,out,80,0,0};app.NlsApi(811,a);}}
 void Hook(unsigned,unsigned,bool){} // USER input provider consults live TTF_SUBCLASS tools; no replacement guest procedure.
 unsigned NotifyFormat(unsigned,unsigned item){return app.Send(Read32(item+8),0x55,hwnd,3);}
 unsigned DoubleClick(){return User::GetDoubleClickTime();}bool Buttons(){return User::PhysicalMouseButtons()!=0;}
 bool Foreground(unsigned target){unsigned active=User::GetForegroundWindow();while(User::Parent(target))target=User::Parent(target);return target==active;}
 unsigned Notice(unsigned item,unsigned code){if(!item)return 0;unsigned p=Allocate(12);if(!p)return 0;unsigned h=Read32(item+8),id=Read32(item+12),n[]={hwnd,id,code};app.Store(p,n,12);unsigned result=app.Send(h,0x4e,id,p);Free(p);return result;}
 void Timer(unsigned t,unsigned which){unsigned old=Read32(t+0x48);if(old){unsigned a[]={hwnd,old};app.Api("KillTimer",a);}unsigned kind=which==1?3:which==4?2:1;unsigned a[]={hwnd,which,Win98Tooltip::Delay(*this,t,kind),0};unsigned result=app.Api("SetTimer",a);if(which==4)Write32(t+0x54,result);else if(result&&which!=3){Write32(t+0x48,which);Write32(t+0x4c,app.mouse_x);Write32(t+0x50,app.mouse_y);}}
 void Hide(unsigned t){if(Read32(t+Hiding))return;Write32(t+Hiding,1);for(unsigned n=1;n<=4;++n){unsigned a[]={hwnd,n};app.Api("KillTimer",a);}Write32(t+0x48,0);Write32(t+0x54,0);if(User::IsVisible(hwnd)&&Read32(t+0x38))Notice(Read32(t+0x38),0xfffffdf6);if(Alive(t)){User::ShowWindow(hwnd,0);Write32(t+0x44,Read32(t+0x44)&0x3f);Write32(t+0x38,0);}Write32(t+Hiding,0);}
 void Pop(unsigned t){unsigned old=Read32(t+0x38);Hide(t);if(Alive(t)){Write32(t+0x44,Read32(t+0x44)|0x80);Write32(t+0x38,old);}}
 unsigned Text(unsigned t,unsigned item){
  if(!item)return 0;unsigned text=Read32(item+36),instance=Read32(item+32),temporary=0,notice=0;bool cache=false;unsigned key[2]={Read32(item+8),Read32(item+12)};
  if(text==~0u){bool wide=Read32(item+4)&64;notice=Allocate(192);if(!notice)return 0;unsigned flags=Read32(item+4),n[]={hwnd,key[1],wide?0xfffffdeeu:0xfffffdf8u,notice+16};app.Store(notice,n,16);unsigned tail=wide?176:96;Write32(notice+tail+4,flags);Write32(notice+tail+8,Read32(item+40));app.Send(key[0],0x4e,key[1],notice);
   if(!Alive(t)){Free(notice);return 0;}unsigned query=Allocate(44);if(!query){Free(notice);return 0;}Write32(query,44);Write32(query+8,key[0]);Write32(query+12,key[1]);item=Win98Tooltip::Find(*this,t,query);Free(query);if(!item){Free(notice);return 0;}
   text=Read32(notice+12);instance=Read32(notice+tail);cache=Read32(notice+tail+4)&0x8000;
   if(text==~0u){Free(notice);app.Fail("Tooltip callback returned recursive LPSTR_CALLBACK");return 0;}
   if(text&0xffff0000&&!wide){temporary=ToWide(text);text=temporary;}
   if(cache&&text){Win98Tooltip::Text(*this,item,text);if(!(text&0xffff0000))Write32(item+32,instance);}
  }
  if(!(text&0xffff0000)&&text){unsigned resource=Allocate(8192);if(resource){unsigned a[]={instance,text,resource,8192};if(app.Api("LoadStringA",a)){if(temporary)Free(temporary);temporary=ToWide(resource);text=temporary;}else text=0;Free(resource);}else text=0;}
  unsigned length=text?WideLength(text):0;
  if(!Read32(t+0x58)||Read32(t+0x5c)<length+1){unsigned newtext=Allocate((length+1)*2);if(!newtext){if(temporary)Free(temporary);if(notice)Free(notice);return 0;}Free(Read32(t+0x58));Write32(t+0x58,newtext);Write32(t+0x5c,length+1);}
  unsigned buffer=Read32(t+0x58);if(text)Copy(buffer,text,(length+1)*2);else{unsigned short zero=0;app.Store(buffer,&zero,2);}
  if(!(Read32(t+8)&2)&&text){unsigned out=0;for(unsigned i=0;i<length;++i){unsigned short c=0;app.Read(buffer+i*2,&c,2);if(c=='&'){unsigned short next=0;app.Read(buffer+(i+1)*2,&next,2);if(next=='&')++i;else continue;}app.Store(buffer+out++*2,&c,2);}unsigned short zero=0;app.Store(buffer+out*2,&zero,2);}
  if(temporary)Free(temporary);if(notice)Free(notice);return buffer;
 }
 unsigned AnsiBuffer(unsigned text){if(!text)return 0;unsigned a[]={0,0,text,~0u,0,0,0,0};unsigned bytes=app.NlsApi(811,a);if(!bytes||bytes>16384)return 0;unsigned out=Allocate(bytes);if(!out)return 0;a[4]=out;a[5]=bytes;if(!app.NlsApi(811,a)){Free(out);return 0;}return out;}
 bool Measure(unsigned t,unsigned text,Gdi::RECT&r){unsigned p=AnsiBuffer(text);if(!p)return false;unsigned rect=Allocate(16);if(!rect){Free(p);return false;}r={0,0,int(Read32(t+0x88)),0};bool wrap=Read32(t+0x88)!=~0u;app.Store(rect,&r,16);unsigned a[]={Read32(t+DC),p,~0u,rect,wrap?0xe50u:0xc20u};unsigned result=app.Api("DrawTextA",a);app.Read(rect,&r,16);Free(rect);Free(p);return result!=0;}
 void Refresh(unsigned t){
  if(!Alive(t)||Read32(t+Showing))return;Write32(t+Showing,1);unsigned item=Read32(t+0x38),text=Text(t,item);if(!Alive(t)){Write32(t+Showing,0);return;}item=Read32(t+0x38);if(!item||!text||!WideLength(text)){Pop(t);Write32(t+Showing,0);return;}
  Gdi::RECT measured{};if(!Measure(t,text,measured)){Write32(t+Showing,0);return;}int width=measured.right+4+int(Read32(t+0x8c))+int(Read32(t+0x94)),height=measured.bottom+2+int(Read32(t+0x90))+int(Read32(t+0x98));if(width<4)width=4;if(height<4)height=4;
  // DrawTooltip consumes a client rectangle. Add the actual nonclient frame
  // before sizing its HWND, using the shared implemented USER provider.
  Gdi::RECT outer={0,0,width,height};
  if(!User::AdjustWindowRect(&outer,User::Style(hwnd),0,User::ExtendedStyle(hwnd))){Write32(t+Showing,0);return;}
  width=outer.right-outer.left;height=outer.bottom-outer.top;
  int x=app.mouse_x,y=app.mouse_y+20;unsigned flags=Read32(item+4);if(Read32(t+0x44)&1){x=int(Read32(t+0x74));y=int(Read32(t+0x78));}if(flags&2){Gdi::RECT tool{};bool valid=false;if(flags&1)valid=User::GetWindowRect(Read32(item+12),&tool);else{app.Read(item+16,&tool,16);Gdi::POINT origin{};valid=User::ScreenToClient(Read32(item+8),&origin);tool.left-=origin.x;tool.right-=origin.x;tool.top-=origin.y;tool.bottom-=origin.y;}if(valid){x=(tool.left+tool.right-width)/2;y=tool.bottom;}}
  // Native single-monitor placement. Original text metrics/margins drive size;
  // balloon tails, cursor-mask geometry and per-monitor selection are followups.
  Gdi::RECT work{};unsigned rect=Allocate(16);if(rect){unsigned a[]={48,0,rect,0};if(app.Api("SystemParametersInfoA",a))app.Read(rect,&work,16);Free(rect);}if(work.right<=work.left||work.bottom<=work.top)work={0,0,800,600};if(x+width>work.right)x=work.right-width;if(x<work.left)x=work.left;if(y+height>work.bottom)y=app.mouse_y-height;if(y<work.top)y=work.top;
  unsigned pos[]={hwnd,0,unsigned(x),unsigned(y),unsigned(width),unsigned(height),0x14};app.Api("SetWindowPos",pos);if(!Alive(t)){Write32(t+Showing,0);return;}Notice(Read32(t+0x38),0xfffffdf7);if(!Alive(t)){Write32(t+Showing,0);return;}User::ShowWindow(hwnd,4);Write32(t+0x44,Read32(t+0x44)|64);User::InvalidateRect(hwnd,nullptr,true);if(!(Read32(t+0x44)&1)){Timer(t,3);Timer(t,4);}Write32(t+Showing,0);
 }
 unsigned CursorTool(unsigned t){unsigned target=Read32(t+RelayWindow);if(!ValidWindow(target)||!User::IsVisible(target))return 0;Gdi::POINT p={app.mouse_x,app.mouse_y};if(!User::ScreenToClient(target,&p))return 0;return Win98Tooltip::ToolAt(*this,t,target,p.x,p.y);}
};
unsigned Application::TooltipMessage(unsigned hwnd,unsigned message,unsigned wp,unsigned lp){
 // -> : DefWindowProc also calculates the pre-WM_CREATE RECT.
 if(message==0x83)return DefaultWindowMessage(hwnd,message,wp,lp);
 unsigned t=0;User::WindowLong(hwnd,0,t,false);TooltipEnvironment e{*this,hwnd};
 if(message==1){unsigned style=User::Style(hwnd);if((style&0xffcc)||(User::ExtendedStyle(hwnd)&0x406000)){Fail("Tooltip balloon/RTL/style provider is not ported");return ~0u;}t=e.Allocate(Bytes);if(!t)return ~0u;unsigned v=t;User::WindowLong(hwnd,0,v,true);Write32(t,hwnd);Write32(t+4,User::Owner(hwnd));Write32(t+8,style);Write32(t+0x44,16);Write32(t+0x88,~0u);Win98Tooltip::SetDelay(e,t,0,~0u);Write32(t+0x40,system);unsigned dc=Gdi::CreateCompatibleDC();Write32(t+DC,dc);if(!dc){v=0;User::WindowLong(hwnd,0,v,true);e.Free(t);return ~0u;}Gdi::SelectObject(dc,system);unsigned c=24;Write32(t+0x80,Api("GetSysColor",&c));c=23;Write32(t+0x84,Api("GetSysColor",&c));User::SetExtendedStyle(hwnd,User::ExtendedStyle(hwnd)|0x80);User::SetStyle(hwnd,(style&~0x40c00000u)|0x80800000u);return 0;}
 if(!t)return User::DefaultMessage(hwnd,message,wp,lp);TooltipEnvironment::Guard guard(e,t);
 if(message==2){e.Hide(t);Write32(t+Dead,1);unsigned zero=0;User::WindowLong(hwnd,0,zero,true);unsigned array=Read32(t+0x34),n=Read32(t+0x24);for(unsigned i=0;i<n;++i)Win98Tooltip::FreeTool(e,t,array+i*44);e.Free(array);e.Free(Read32(t+0x58));Gdi::DeleteDC(Read32(t+DC));return 0;}
 if(message==0x84)return ~0u; // HTTRANSPARENT; tooltip never steals the tool's mouse.
 if(message==0x55)return lp==3?2:0;
 if(message==0x30){unsigned f=wp?wp:system;if(!Gdi::SelectObject(Read32(t+DC),f)){last_error=87;return 0;}Write32(t+0x40,f);if(lp)e.Refresh(t);return 1;}
 if(message==0x31)return Read32(t+0x40);
 if(message==0xf||message==0x318){User::Paint p{};unsigned dc=message==0x318?wp:User::BeginPaint(hwnd,p);if(dc){Gdi::RECT r{};User::GetClientRect(hwnd,&r);DrawTooltip(hwnd,dc,r);User::DrawingChanged(dc);if(message==0xf)User::EndPaint(hwnd,p);}return 0;}
 if(message==0x14)return 0;
 if(message==0xd||message==0xe){unsigned text=e.Text(t,Read32(t+0x38)),a=e.AnsiBuffer(text);if(!a)return 0;char value[16384];unsigned length=0;for(;length+1<sizeof value;++length){Read(a+length,value+length,1);if(!value[length])break;}value[length]=0;if(message==0xd&&lp&&wp){unsigned copy=length<wp-1?length:wp-1;Store(lp,value,copy);unsigned char zero=0;Store(lp+copy,&zero,1);length=copy;}e.Free(a);return length;}
 if(message==0x401){Win98Tooltip::Activate(e,t,wp!=0);return 0;}
 if(message==0x403){Win98Tooltip::SetDelay(e,t,wp,lp);return 0;}
 if(message==0x415)return Win98Tooltip::Delay(e,t,wp);
 if(message==0x40d)return Read32(t+0x24);
 if(message==0x413||message==0x414){Write32(t+(message==0x413?0x80:0x84),wp);User::InvalidateRect(hwnd,nullptr,true);return 0;}
 if(message==0x416||message==0x417)return Read32(t+(message==0x416?0x80:0x84));
 if(message==0x418){unsigned old=Read32(t+0x88);Write32(t+0x88,lp);return old;}
 if(message==0x419)return Read32(t+0x88);
 if(message==0x41a||message==0x41b){if(lp){if(message==0x41a)e.Copy(t+0x8c,lp,16);else e.Copy(lp,t+0x8c,16);}return 0;}
 if(message==0x41c){if(Read32(t+0x44)&64)e.Hide(t);if(e.Alive(t))Write32(t+0x44,Read32(t+0x44)&~128u);return 0;}
 if(message==0x41d){if(!lp||lp==Read32(t+0x38))e.Refresh(t);return 0;}
 if(message==0x412){Write32(t+0x74,unsigned(int(short(lp))));Write32(t+0x78,unsigned(int(short(lp>>16))));if((Read32(t+0x44)&1)&&Read32(t+0x38))e.Refresh(t);return 0;}
 if(message==0x411)return Win98Tooltip::Track(e,t,wp!=0,lp);
 if(message==0x410){if(!lp)return 0;unsigned a[]={Read32(lp),Read32(lp+4)};return Api("WindowFromPoint",a);}
 if(message==0x407){if(lp){unsigned target=Read32(lp);Write32(t+RelayWindow,target);Win98Tooltip::Relay(e,t,target,Read32(lp+4),Read32(lp+8),Read32(lp+12));}return 0;}
 if(message==0x113){if(Read32(t+0x44)&1)return 0;unsigned item=e.CursorTool(t);if(wp==4){e.Pop(t);return 0;}if(wp==3){if(!item)e.Hide(t);return 0;}if(wp==1||wp==2){if(!(Read32(t+0x44)&16)||!item){e.Hide(t);return 0;}if(wp==2)Write32(t+0x44,Read32(t+0x44)&0x3f);Write32(t+0x38,item);e.Refresh(t);return 0;}}
 if(message==0x2005){unsigned old=Read32(t+0x10)&1;Write32(t+0x10,(Read32(t+0x10)&~1u)|(wp?1:0));return old;}
 if(message==0x2006)return Read32(t+0x10)&1;
 if(message==0x40a||message==0x437){if(!lp)return 0;unsigned item=Win98Tooltip::ToolAt(e,t,Read32(lp),int(Read32(lp+4)),int(Read32(lp+8)));if(!item||!e.Text(t,item))return 0;return Win98Tooltip::Get(e,item,lp+12,message==0x437);}
 if(message==0x41e){if(!lp)return 0;unsigned item=Win98Tooltip::Find(e,t,lp),text=e.Text(t,item);Gdi::RECT r{};if(!item||!text||!e.Measure(t,text,r))return 0;unsigned width=r.right+4+Read32(t+0x8c)+Read32(t+0x94),height=r.bottom+2+Read32(t+0x90)+Read32(t+0x98);return (width&65535)|(height<<16);}
 const bool add=message==0x404||message==0x432,del=message==0x405||message==0x433,rect=message==0x406||message==0x434,get=message==0x408||message==0x435,set=message==0x409||message==0x436,text=message==0x40b||message==0x438,update=message==0x40c||message==0x439,enumerate=message==0x40e||message==0x43a,current=message==0x40f||message==0x43b;
 if(add||del||rect||get||set||text||update||enumerate||current){bool wide=message>=0x432;if(current&&!lp)return Read32(t+0x38);if(!lp)return 0;unsigned query=lp,shadow=0,converted=0,result=0;
  if(add||set||update){if(Read32(lp+4)&~0x3f3u){Fail("Tooltip tool flags require an unported provider");return 0;}}
  if(!wide&&!enumerate&&!current){shadow=e.Allocate(44);if(!shadow)return 0;bool copy=add||set||update;if(!Win98Tooltip::Ansi(e,lp,shadow,copy)){e.Free(shadow);return 0;}if(copy&&(Read32(shadow+4)&0x200)&&Read32(shadow+36)!=~0u)converted=Read32(shadow+36);query=shadow;}
  if(add)result=Win98Tooltip::Add(e,t,query);else if(del)Win98Tooltip::Delete(e,t,query);else if(rect)Win98Tooltip::NewRect(e,t,query);else if(set)Win98Tooltip::Set(e,t,query);else if(update)Win98Tooltip::UpdateText(e,t,query);
  else{unsigned item=enumerate?(wp<Read32(t+0x24)?Read32(t+0x34)+wp*44:0):current?Read32(t+0x38):Win98Tooltip::Find(e,t,query);if(text){unsigned output=Read32(lp+36);if(output){unsigned short zero=0;Store(output,&zero,wide?2:1);unsigned value=e.Text(t,item);if(value)e.CopyText(output,value,wide);}}else result=Win98Tooltip::Get(e,item,lp,wide);}
  if(converted)e.Free(converted);if(shadow)e.Free(shadow);return result;
 }
 if(message>=0x400&&message<0x500){Fail("Tooltip message is not ported");return 0;}return User::DefaultMessage(hwnd,message,wp,lp);
}
void Application::RelayTooltipInput(unsigned target,unsigned message,unsigned wp,unsigned lp){
 if(message<0x200||message>0x209||User::CommonControl(target)==9)return;unsigned windows[User::WindowCapacity],count=0;if(!User::WindowSnapshot(0,windows,User::WindowCapacity,count))return;
 for(unsigned i=0;i<count;++i){unsigned tip=windows[i];if(User::WindowProcess(tip)!=process_id||User::CommonControl(tip)!=9)continue;unsigned t=0;if(!User::WindowLong(tip,0,t,false)||!t)continue;TooltipEnvironment e{*this,tip};TooltipEnvironment::Guard guard(e,t);unsigned n=Read32(t+0x24),array=Read32(t+0x34);bool relay=false;for(unsigned j=0;j<n;++j){unsigned item=array+j*44,flags=Read32(item+4);if((flags&16)&&(flags&1?Read32(item+12):Read32(item+8))==target){relay=true;break;}}if(relay){Write32(t+RelayWindow,target);Win98Tooltip::Relay(e,t,target,message,wp,lp);}}
}
void Application::DrawTooltip(unsigned hwnd,unsigned dc,const Gdi::RECT&bounds){
 unsigned t=0;if(!User::WindowLong(hwnd,0,t,false)||!t)return;TooltipEnvironment e{*this,hwnd};TooltipEnvironment::Guard guard(e,t);unsigned brush=Gdi::CreateSolidBrush(Read32(t+0x80));if(brush){Gdi::FillRect(dc,&bounds,brush);Gdi::DeleteObject(brush);}unsigned text=Read32(t+0x58),p=e.AnsiBuffer(text);if(!p)return;unsigned rect=e.Allocate(16);if(!rect){e.Free(p);return;}Gdi::RECT r={bounds.left+2+int(Read32(t+0x8c)),bounds.top+1+int(Read32(t+0x90)),bounds.right-2-int(Read32(t+0x94)),bounds.bottom-1-int(Read32(t+0x98))};Store(rect,&r,16);int saved=Gdi::SaveDC(dc);if(saved){Gdi::SelectObject(dc,Read32(t+0x40));Gdi::SetBkMode(dc,Gdi::TRANSPARENT);Gdi::SetTextColor(dc,Read32(t+0x84));unsigned a[]={dc,p,~0u,rect,Read32(t+0x88)==~0u?0x820u:0xa50u};Api("DrawTextA",a);Gdi::RestoreDC(dc,saved);}e.Free(rect);e.Free(p);
}
}
