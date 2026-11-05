// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "win98_tab_multiline.h"
#include "user_draw.h"
#include "user_settings.h"
#include <string.h>
namespace X86 {
struct Application::TabEnvironment {
 Application&app;unsigned hwnd;
 unsigned Read32(unsigned p){return app.Read32(p);}void Write32(unsigned p,unsigned v){app.Write32(p,v);}
 unsigned Allocate(unsigned n){unsigned a[]={64,n};return app.Api("LocalAlloc",a);}void Free(unsigned p){if(p)app.Api("LocalFree",&p);}
 void Copy(unsigned to,unsigned from,unsigned n){unsigned char bytes[256];while(n){unsigned size=n>256?256:n;app.Read(from,bytes,size);app.Store(to,bytes,size);to+=size;from+=size;n-=size;}}
 unsigned Array(unsigned op,unsigned array,unsigned index,unsigned value){unsigned a[]={array,index,value};return app.CommonControlApi(op,a);}
 bool SetString(unsigned field,unsigned value){unsigned a[]={field,value};return app.CommonControlApi(236,a)!=0;}
 void CopyText(unsigned out,unsigned text,unsigned count){if(!count)return;unsigned i=0;for(;i+1<count;++i){unsigned short c=0;app.Read(text+i*2,&c,2);app.Store(out+i*2,&c,2);if(!c)return;}unsigned short zero=0;app.Store(out+i*2,&zero,2);}
 bool Alive(unsigned t){unsigned current=0;return User::IsWindow(hwnd)&&User::WindowLong(hwnd,0,current,false)&&current==t&&app.state!=Failed&&app.state!=Exited;}
 void Dirty(unsigned t,bool layout){if(layout){Write32(t+0x44,0x7fffffff);Write32(t+0x4c,0x7fffffff);}User::InvalidateRect(hwnd,nullptr,true);}
 unsigned Notify(unsigned t,unsigned code){unsigned p=Allocate(12);if(!p)return 1;unsigned info[]={hwnd,User::ControlID(hwnd),code};app.Store(p,info,12);unsigned result=app.Send(User::Parent(hwnd),0x4e,info[1],p);Free(p);return result;}
 void Selection(unsigned t,unsigned,unsigned selected){
  if(Read32(t+8)&0x200){app.LayoutTab(hwnd);if(selected!=~0u&&int(Read32(t+0x70))>0){unsigned item=Win98Tab::Item(*this,t,selected);if(item)Win98Tab::MoveRow(*this,t,Read32(item+0x28));}}
  else{Dirty(t,true);app.LayoutTab(hwnd);}User::InvalidateRect(hwnd,nullptr,true);
 }
 bool Text(unsigned item,char*out,unsigned capacity){
  out[0]=0;unsigned text=Read32(item+0x2c);if(!text)return true;
  unsigned temporary=Allocate(capacity);if(!temporary)return false;unsigned a[]={0,0,text,~0u,temporary,capacity,0,0};unsigned result=app.NlsApi(811,a);if(result)app.Read(temporary,out,capacity);Free(temporary);out[capacity-1]=0;
  // strips prefixes/tabs for short, horizontal strings. Long input
  // takes the original direct extent path. Native ACP conversion is shared.
  unsigned units=0;for(;;++units){unsigned short c=0;app.Read(text+units*2,&c,2);if(!c)break;if(units>=8191){app.Fail("Tab text exceeds native string provider");return false;}}
  if(units<128){unsigned n=0;for(unsigned i=0;out[i];++i){if(out[i]=='&'){if(out[i+1]=='&')++i;else continue;}if(out[i]=='\t')break;out[n++]=out[i];}out[n]=0;}
  return result!=0;
 }
 int TextWidth(unsigned t,unsigned item){char text[8192];if(!Text(item,text,sizeof text))return 0;Gdi::SIZE size{};Gdi::GetTextExtentPoint32A(Read32(t+0xa4),text,strlen(text),&size);return size.cx;}
};
void Application::LayoutTab(unsigned hwnd){
 unsigned t=0;if(!User::WindowLong(hwnd,0,t,false)||!t)return;TabEnvironment e{*this,hwnd};if(Read32(t+0x44)!=0x7fffffff)return;
 Gdi::RECT client{};Gdi::TEXTMETRICA metrics{};unsigned dc=Read32(t+0xa4);if(!User::GetClientRect(hwnd,&client)||!Gdi::GetTextMetricsA(dc,&metrics))return;
 Win98Tab::Metrics m{metrics.tmHeight,metrics.tmAveCharWidth,int(User::SystemSettings().Word(0x338)),int(User::SystemSettings().Word(0x33a))};
 if(Read32(t+8)&0x200){
  if((client.right<=2*m.border_x&&Win98Tab::Count(e,t))||!Win98Tab::MultilineLayout(e,t,m,client.right)){Fail("Multiline tab bounds require an unsupported original degenerate layout");return;}
  Write32(t+0xa8,0);return;
 }
 Win98Tab::Layout(e,t,m,client.right);unsigned count=Win98Tab::Count(e,t),selected=Read32(t+0x3c);int shift=0;
 // Native single-row scrolling provider: expose the actual selected item when
 // the row overflows. Original arrow child/scroll button remains a followup.
 if(selected<count){unsigned item=Win98Tab::Item(e,t,selected);int right=Read32(item+8);if(right>client.right-m.border_x)shift=right-client.right+m.border_x;}
 Write32(t+0xa8,shift);if(shift)for(unsigned i=0;i<count;++i){unsigned item=Win98Tab::Item(e,t,i);Write32(item,Read32(item)-shift);Write32(item+8,Read32(item+8)-shift);}
}
unsigned Application::TabMessage(unsigned hwnd,unsigned message,unsigned wp,unsigned lp){
 // -> : NCCALCSIZE needs DefWindowProc even before WM_CREATE.
 if(message==0x83)return DefaultWindowMessage(hwnd,message,wp,lp);
 unsigned t=0;User::WindowLong(hwnd,0,t,false);TabEnvironment e{*this,hwnd};
 if(message==1){
  const unsigned style=User::Style(hwnd);if(style&0x61ff){last_error=50;return ~0u;} // buttons/vertical/ownerdraw/tooltips/hottrack/forced alignment
  t=e.Allocate(0xb0);if(!t)return ~0u;unsigned value=t;User::WindowLong(hwnd,0,value,true);
  Write32(t,hwnd);Write32(t+4,User::Parent(hwnd));Write32(t+8,style&~0x30u);Write32(t+0x2c,0x10);Write32(t+0x30,4);
  const unsigned invalid_fields[]={0x3c,0x40,0x70,0x74,0xa0};
  for(unsigned offset:invalid_fields)Write32(t+offset,~0u);
  unsigned grow=4,array=CommonControlApi(328,&grow);Write32(t+0x28,array);unsigned dc=Gdi::CreateCompatibleDC();Write32(t+0xa4,dc);Write32(t+0x38,system);if(dc)Gdi::SelectObject(dc,system);
  Write32(t+0x60,3*User::SystemSettings().Word(0x338));Write32(t+0x64,3*User::SystemSettings().Word(0x33a)/2);Write32(t+0x68,dc?Gdi::DeviceCaps(dc,88):0);
  if(!array||!dc){if(array)CommonControlApi(329,&array);if(dc)Gdi::DeleteDC(dc);value=0;User::WindowLong(hwnd,0,value,true);e.Free(t);return ~0u;}
  User::SetStyle(hwnd,User::Style(hwnd)|0x04000000);e.Dirty(t,true);return 0;
 }
 if(!t)return User::DefaultMessage(hwnd,message,wp,lp);
 if(message==2){Win98Tab::DeleteAll(e,t);unsigned array=Read32(t+0x28);CommonControlApi(329,&array);Gdi::DeleteDC(Read32(t+0xa4));unsigned zero=0;User::WindowLong(hwnd,0,zero,true);e.Free(t);return 0;}
 if(message==0xc||message==0xd||message==0xe)return WindowTextDefault(hwnd,message,wp,lp);
 if(message==0x30){unsigned font=wp?wp:system;if(!Gdi::SelectObject(Read32(t+0xa4),font)){last_error=87;return 0;}Write32(t+0x38,font);e.Dirty(t,true);return 0;}
 if(message==0x31)return Read32(t+0x38);
 if(message==0x7d&&wp==~15u){unsigned style=Read32(lp+4);if(style&0x61ff){Fail("Tab style requires an unported renderer");return 0;}Write32(t+8,style);e.Dirty(t,true);return 0;}
 if(message==5){e.Dirty(t,true);return 0;}
 if(message==0xb){unsigned flags=Read32(t+0x2c);Write32(t+0x2c,wp?flags|0x10:flags&~0x10u);if(wp)e.Dirty(t,true);return 0;}
 if(message==7||message==8){User::InvalidateRect(hwnd,nullptr,false);return 0;}
 if(message==0x87)return 0x81;
 if(message==0xf){User::Paint paint{};unsigned dc=User::BeginPaint(hwnd,paint);if(dc){Gdi::RECT r{};User::GetClientRect(hwnd,&r);DrawTab(hwnd,dc,r);User::DrawingChanged(dc);User::EndPaint(hwnd,paint);}return 0;}
 if(message==0x14)return 1; // DrawTab covers the entire client background.
 if(message==0x1304)return Win98Tab::Count(e,t);
 if(message==0x130b)return Read32(t+0x3c);
 if(message==0x132f){unsigned focus=Read32(t+0x40);return focus==~0u?Read32(t+0x3c):focus;}
 if(message==0x130c)return Win98Tab::Select(e,t,wp,false);
 if(message==0x1330){Win98Tab::Select(e,t,wp,true);return 0;}
 if(message==0x130e){if(wp>1024){last_error=87;return 0;}return Win98Tab::Extra(e,t,wp);}
 if(message==0x1308)return Win98Tab::Delete(e,t,wp);
 if(message==0x1309)return Win98Tab::DeleteAll(e,t);
 if(message==0x1302)return 0;
 if(message==0x1303){if(lp)Fail("Tab image-list realization is not ported");return 0;}
 if(message==0x132b){Write32(t+0x60,int(short(lp)));Write32(t+0x64,int(short(lp>>16)));if(Read32(t+8)&0x200)e.Dirty(t,true);return 0;}
 if(message==0x1329){unsigned old=(Read32(t+0x6c)<<16)|(Read32(t+0x68)&65535);Write32(t+0x68,lp&65535);Write32(t+0x6c,lp>>16);e.Dirty(t,true);return old;}
 if(message==0x1331){unsigned old=Read32(t+0x48);if(int(lp)>=0){Write32(t+0x48,lp);Write32(t+0x9c,Read32(t+0x9c)|1);}else Write32(t+0x9c,Read32(t+0x9c)&~1u);e.Dirty(t,true);return old;}
 if(message==0x132c){LayoutTab(hwnd);return Read32(t+0x70)+1;}
 if(message==0x130a){LayoutTab(hwnd);unsigned item=Win98Tab::Item(e,t,wp);if(!lp)return 0;if(!item){unsigned zero[4]={0,0,0,Read32(t+0x4c)};Store(lp,zero,16);return 0;}e.Copy(lp,item,16);return 1;}
 if(message==0x1328){if(!lp)return ~0u;LayoutTab(hwnd);Win98Tab::Adjust(e,t,wp!=0,lp,User::SystemSettings().Word(0x338),User::SystemSettings().Word(0x33a));return 0;}
 if(message==0x1333){unsigned item=Win98Tab::Item(e,t,wp);if(!item)return 0;unsigned old=Read32(item+0x30),next=(old&~2u)|(unsigned((lp&65535)!=0)<<1);if(next==old)return 0;Write32(item+0x30,next);e.Dirty(t,false);return 1;}
 if(message==0x2005){unsigned previous=Read32(t+0x10)&1;Write32(t+0x10,(Read32(t+0x10)&~1u)|(wp?1u:0u));return previous;}
 if(message==0x2006)return Read32(t+0x10)&1;
 const bool get=message==0x1305||message==0x133c,set=message==0x1306||message==0x133d,insert=message==0x1307||message==0x133e;
 if(get||set||insert){
  if(!lp)return 0;const bool wide=message>=0x133c;if(wide)return get?Win98Tab::Get(e,t,wp,lp):set?Win98Tab::Set(e,t,wp,lp):Win98Tab::Insert(e,t,wp,lp);
  unsigned extra=Read32(t+0x30),record=e.Allocate(28+extra);if(!record)return get||set?0:~0u;e.Copy(record,lp,24+extra);unsigned mask=Read32(lp),text=0;
  if(mask&1){if(get){unsigned capacity=Read32(lp+16);if(capacity>8192){e.Free(record);last_error=87;return 0;}text=e.Allocate(capacity*2);}
   else if(Read32(lp+12)){unsigned a[]={0,0,Read32(lp+12),~0u,0,0};unsigned n=NlsApi(594,a);if(n&&n<=8192){text=e.Allocate(n*2);a[4]=text;a[5]=n;if(text)NlsApi(594,a);}else {e.Free(record);return 0;}}
   Write32(record+12,text);
  }
  unsigned result=get?Win98Tab::Get(e,t,wp,record):set?Win98Tab::Set(e,t,wp,record):Win98Tab::Insert(e,t,wp,record);
  if(get){e.Copy(lp,record,12);if(mask&1){if(!Read32(record+12))Write32(lp+12,0);else{unsigned a[]={0,0,Read32(record+12),~0u,Read32(lp+12),Read32(lp+16),0,0};NlsApi(811,a);}}else Write32(lp+16,Read32(record+16));if(mask&2)Write32(lp+20,Read32(record+20));if(mask&8)e.Copy(lp+24,record+24,extra);}
  if(text)e.Free(text);e.Free(record);return result;
 }
 auto hit=[&](int x,int y,unsigned*flags){LayoutTab(hwnd);unsigned count=Win98Tab::Count(e,t);for(unsigned i=0;i<count;++i){unsigned item=Win98Tab::Item(e,t,i);if(Read32(item+0x30)&4)continue;Gdi::RECT r{};Read(item,&r,16);if(x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom){if(flags){int relative=x-r.left,start=Read32(item+0x14);*flags=relative>start&&relative<start+int(Read32(item+0x1c))?4:6;}return i;}}if(flags)*flags=1;return ~0u;};
 if(message==0x130d){if(!lp)return ~0u;unsigned flags=0,index=hit(Read32(lp),Read32(lp+4),&flags);Write32(lp+8,flags);return index;}
 if(message==0x201){if(!User::IsEnabled(hwnd))return 0;unsigned index=hit(short(lp),short(lp>>16),nullptr);if(index!=~0u){Win98Tab::Select(e,t,index,true);if(e.Alive(t)&&!(Read32(t+8)&0x8000))SetFocus(hwnd);}return 0;}
 if(message==0x202){e.Notify(t,~1u);return 0;}
 if(message==0x100){if(!User::IsEnabled(hwnd))return 0;
  // emits packed NMTCKEYDOWN before considering the key. Its result
  // does not veto selection; TCN_SELCHANGING is the separate veto point.
  unsigned notice=e.Allocate(18);if(notice){unsigned info[]={hwnd,User::ControlID(hwnd),0xfffffdda};Store(notice,info,12);unsigned short vk=wp;Store(notice+12,&vk,2);Write32(notice+14,lp>>16);Send(User::Parent(hwnd),0x4e,info[1],notice);e.Free(notice);}if(!e.Alive(t))return 0;
  unsigned n=Win98Tab::Count(e,t),selected=Read32(t+0x3c);int next=selected;
  if((Read32(t+8)&0x200)&&(wp==0x26||wp==0x28)){LayoutTab(hwnd);Win98Tab::Select(e,t,selected==~0u?0:Win98Tab::RowKey(e,t,selected,wp),true);return 0;}
  if(wp==0x25||wp==0x27||wp==0x23||wp==0x24){if(wp==0x25)--next;else if(wp==0x27)++next;else next=wp==0x24?0:int(n)-1;if(next<0)next=0;Win98Tab::Select(e,t,next,true);return 0;}
 }
 if(message>=0x1300&&message<=0x13ff){Fail("Tab control message is not ported");return 0;}
 return User::DefaultMessage(hwnd,message,wp,lp);
}
void Application::DrawTab(unsigned hwnd,unsigned dc,const Gdi::RECT&bounds){
 unsigned t=0;if(!User::WindowLong(hwnd,0,t,false)||!t||!(Read32(t+0x2c)&0x10))return;TabEnvironment e{*this,hwnd};LayoutTab(hwnd);
 auto color=[&](unsigned id){return Api("GetSysColor",&id);};auto fill=[&](Gdi::RECT r,unsigned c){unsigned brush=Gdi::CreateSolidBrush(c);if(brush){Gdi::FillRect(dc,&r,brush);Gdi::DeleteObject(brush);}};
 unsigned face=color(15),white=color(20),shadow=color(16),dark=color(21);int saved=Gdi::SaveDC(dc);if(!saved)return;
 Gdi::IntersectClipRect(dc,bounds.left,bounds.top,bounds.right,bounds.bottom);fill(bounds,face);Gdi::SelectObject(dc,Read32(t+0x38));Gdi::SetBkMode(dc,Gdi::TRANSPARENT);
 int row=Read32(t+0x4c)*(Read32(t+0x70)+1)+User::SystemSettings().Word(0x33a);fill({bounds.left,row,bounds.right,row+1},white);
 unsigned count=Win98Tab::Count(e,t),selected=Read32(t+0x3c);
 for(unsigned pass=0;pass<2;++pass)for(unsigned i=0;i<count;++i){if((i==selected)!=(pass==1))continue;unsigned item=Win98Tab::Item(e,t,i);if(Read32(item+0x30)&4)continue;Gdi::RECT r{};Read(item,&r,16);if(r.right<=0||r.left>=bounds.right)continue;bool active=i==selected;Gdi::RECT drawn=r;if(active){drawn.left-=2;drawn.right+=2;drawn.top-=2;drawn.bottom+=1;}
  fill(drawn,face);fill({drawn.left,drawn.top+2,drawn.left+1,drawn.bottom},white);fill({drawn.left+2,drawn.top,drawn.right-2,drawn.top+1},white);fill({drawn.right-1,drawn.top+2,drawn.right,drawn.bottom},dark);fill({drawn.right-2,drawn.top+2,drawn.right-1,drawn.bottom},shadow);
  char text[8192];if(e.Text(item,text,sizeof text)){Gdi::SetTextColor(dc,User::IsEnabled(hwnd)?((Read32(item+0x30)&2)?color(13):color(18)):color(17));Gdi::TextOutA(dc,r.left+int(Read32(item+0x14)),r.top+int(Read32(item+0x18)),text,strlen(text));}
  if(active&&User::GetFocus()==hwnd){Gdi::RECT focus={r.left+3,r.top+2,r.right-3,r.bottom-2};for(int x=focus.left;x<focus.right;x+=2){fill({x,focus.top,x+1,focus.top+1},dark);fill({x,focus.bottom-1,x+1,focus.bottom},dark);}for(int y=focus.top;y<focus.bottom;y+=2){fill({focus.left,y,focus.left+1,y+1},dark);fill({focus.right-1,y,focus.right,y+1},dark);}}
 }
 Gdi::RestoreDC(dc,saved);
}
}
