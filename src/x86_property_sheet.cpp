// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "dialog_template.h"
#include "win98_property_pages.h"
#include "win98_property_sheet_modal.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_locale.h"
#else
#include "../build/property-sheet-assets/templates.h"
#endif
#include <stdio.h>
#include <string.h>
namespace X86 {
namespace {
// COMCTL32 supplies a real dialog procedure for the sheet frame. USER's dialog
// dispatch returns the procedure result for WM_INITDIALOG without consulting
// DefDlgProc, so the controller must be the procedure, not a default handler.
constexpr unsigned PropertySheetToken=0x71000011;
struct SheetFrame {
 Application*app;SheetFrame*next;unsigned state,hwnd=0,tab=0,scratch=0,disabled_owner=0,previous_focus=0;
 bool released=false,closing=false,initializing=true;unsigned children[99]{};
 int page_width=0,page_height=0;static SheetFrame*head;
 SheetFrame(Application*a,unsigned s):app(a),next(head),state(s){head=this;}
 ~SheetFrame(){auto**p=&head;while(*p&&*p!=this)p=&(*p)->next;if(*p)*p=next;}
};
SheetFrame*SheetFrame::head=nullptr;
SheetFrame*FindSheet(Application*a,unsigned hwnd,unsigned initial=0){
 for(auto*f=SheetFrame::head;f;f=f->next)if(f->app==a&&((hwnd&&f->hwnd==hwnd)||(!f->hwnd&&initial&&f->state==initial)))return f;return nullptr;
}
}
struct Application::PropertyWork {
 Application*app;PropertyWork*next;SheetFrame*sheet;
 unsigned thread,index,kind,phase=0,value[16]{},size=0;bool kept=false;
 unsigned char*bytes=nullptr;
 static PropertyWork*head;
 PropertyWork(Application&a,SheetFrame*f,unsigned k):app(&a),next(head),sheet(f),thread(a.current_thread),index(a.threads[thread].host_frame_count),kind(k){head=this;}
 ~PropertyWork(){auto**p=&head;while(*p&&*p!=this)p=&(*p)->next;if(*p)*p=next;delete[]bytes;}
 static PropertyWork*Find(const Application&a,unsigned at,unsigned k,unsigned thread=~0u){if(thread==~0u)thread=a.current_thread;for(auto*w=head;w;w=w->next)if(w->app==&a&&w->thread==thread&&w->index==at&&w->kind==k)return w;return nullptr;}
};
Application::PropertyWork*Application::PropertyWork::head=nullptr;
struct Application::PropertySheetEnvironment {
 Application&app;SheetFrame*f=nullptr;bool recorded=false;
 struct WorkScope{PropertySheetEnvironment&e;PropertyWork*w;~WorkScope(){if(w){e.app.GiveTail(Application::TailNone);if(w->kept)e.app.PopHostFrame();delete w;}}};
 PropertyWork*Begin(unsigned kind){app.GiveTail(Application::TailNone);auto*w=new PropertyWork(app,f,kind);
  w->kept=recorded&&app.PushHostFrame(kind,f?f->state:0);return w;}
 void Phase(PropertyWork*w,unsigned phase){if(w){w->phase=phase;if(w->kept)app.threads[app.current_thread].host_frames[w->index].d=phase;app.GiveTail(w->kept?Application::TailRecord:Application::TailNone);}}
 unsigned Activate(unsigned direction);
 unsigned ContinueActivate(PropertyWork*,bool);
 unsigned ContinuePageWindow(PropertyWork*,bool);
 unsigned ContinueRelease(PropertyWork*,bool);
 unsigned ContinueNotify(PropertyWork*,bool);
 unsigned ContinueRemove(PropertyWork*,bool);
 unsigned ContinueSetCurrent(PropertyWork*,bool);
 unsigned Apply(unsigned command);
 unsigned ContinueApply(PropertyWork*,bool);
 unsigned Command(unsigned command);
 unsigned ContinueCommand(PropertyWork*,bool);
 unsigned QuerySiblings(unsigned,unsigned);
 unsigned ContinueQuery(PropertyWork*,bool);
 unsigned AddPage(unsigned);
 unsigned ContinueAdd(PropertyWork*,bool);
 unsigned RemoveRequest(unsigned,unsigned);
 unsigned ContinueRemoveRequest(PropertyWork*,bool);
 unsigned Read(unsigned p){return app.Read32(p);}void Write(unsigned p,unsigned v){app.Write32(p,v);}
 unsigned Word(unsigned p){unsigned short v=0;app.Read(p,&v,2);return v;}unsigned Byte(unsigned p){unsigned char v=0;app.Read(p,&v,1);return v;}
 void WriteWord(unsigned p,unsigned value){unsigned short v=value;app.Store(p,&v,2);}
 unsigned Allocate(unsigned n){unsigned a[]={64,n};return app.Api("LocalAlloc",a);}
 void Free(unsigned p){if(p)app.LocalMemoryApi("LocalFree",&p);}
 void Copy(unsigned out,unsigned in,unsigned n){unsigned char b[256];while(n){unsigned k=n>sizeof b?sizeof b:n;if(!app.Read(in,b,k)||!app.Store(out,b,k))return;out+=k;in+=k;n-=k;}}
 void Zero(unsigned out,unsigned n){unsigned char b[256]{};while(n){unsigned k=n>sizeof b?sizeof b:n;if(!app.Store(out,b,k))return;out+=k;n-=k;}}
 unsigned WideLength(unsigned p){unsigned n=0;while(n<8192&&Word(p+n*2))++n;if(n==8192){app.Fail("Property sheet string exceeds native provider");return 0;}return n;}
 unsigned Clone(unsigned p,unsigned encoding){if(encoding!=1){app.last_error=50;return 0;}return CloneA(p);}
 unsigned CloneA(unsigned p){unsigned a[]={0,1,p,~0u,0,0};unsigned n=app.NlsApi(594,a);if(!n)return 0;unsigned out=Allocate(n*2);if(out){a[4]=out;a[5]=n;if(!app.NlsApi(594,a)){Free(out);out=0;}}return out;}
 unsigned Narrow(unsigned p){if(p<65536)return p;unsigned a[]={0,0,p,~0u,0,0,0,0};unsigned n=app.NlsApi(811,a);if(!n)return 0;unsigned out=Allocate(n);if(out){a[4]=out;a[5]=n;if(!app.NlsApi(811,a)){Free(out);out=0;}}return out;}
 void CopyWide(unsigned out,unsigned in,unsigned cap){if(!cap)return;unsigned n=WideLength(in);if(n>=cap)n=cap-1;Copy(out,in,n*2);WriteWord(out+n*2,0);}
 int CompareWide(unsigned a,unsigned b){for(unsigned i=0;i<8192;++i){unsigned x=Word(a+i*2),y=Word(b+i*2);if(x!=y)return x<y?-1:1;if(!x)return 0;}return 1;}
 // Fixed LocalAlloc blocks belong to the process heap, not the host arena.
 unsigned LocalSize(unsigned p){return app.LocalMemoryApi("LocalSize",&p);}
 unsigned CreatePage(unsigned p,bool ansi,unsigned){if(recorded)app.GiveTail(Application::TailRecord);return app.PropertyPageApi(ansi?"CreatePropertySheetPageA":"CreatePropertySheetPageW",&p);}
 void DestroyPage(unsigned p){if(p>=24&&LocalSize(p-24))app.PropertyPageApi("DestroyPropertySheetPage",&p);}
 unsigned Callback(unsigned proc,unsigned reason,unsigned parameter,unsigned){unsigned a[]={0,reason,parameter};return app.Call(proc,a,3);}
 unsigned Present(unsigned state){
  auto&t=app.threads[app.current_thread];
  return app.ShowPropertySheet(state);
 }
 bool Alive(){return f&&!f->released&&!f->closing&&app.state!=Failed&&app.state!=Exited&&(!f->hwnd||User::IsWindow(f->hwnd));}
 unsigned FindDialog(unsigned instance,unsigned name,unsigned language){unsigned text=Narrow(name);if(name>=65536&&!text)return 0;unsigned a[]={instance,5,text,language};unsigned result=app.ResourceApi("FindResourceExA",a);if(!result){a[3]=0;result=app.ResourceApi("FindResourceExA",a);}if(text>=65536)Free(text);return result;}
 unsigned LoadResource(unsigned instance,unsigned resource){unsigned a[]={instance,resource};return app.ResourceApi("LoadResource",a);}
 unsigned LockResource(unsigned p){return p;}void FreeResource(unsigned){}
 unsigned LoadIcon(unsigned instance,unsigned name){unsigned text=Narrow(name),a[]={instance,text};unsigned icon=app.ImageApi("LoadIconA",a);if(text>=65536)Free(text);return icon;}
 void LoadString(unsigned instance,unsigned id,unsigned out,unsigned capacity,unsigned){
  unsigned temp=Allocate(capacity);if(!temp){WriteWord(out,0);return;}unsigned a[]={instance,id,temp,capacity};app.Api("LoadStringA",a);
  unsigned cv[]={0,0,temp,~0u,out,capacity};if(!app.NlsApi(594,cv))WriteWord(out,0);Free(temp);
 }
 void LoadStartString(unsigned instance,unsigned id,unsigned out,unsigned cap){LoadString(instance,id,out,cap,0);}
 unsigned LegacyInfo(unsigned,unsigned,unsigned,unsigned){app.last_error=50;return 0;}
 unsigned Selection(){return app.Send(f->tab,0x130b,0,0);}void Select(unsigned i){app.Send(f->tab,0x130c,i,0);}
 unsigned PageWindow(unsigned i){return i<99&&User::IsWindow(f->children[i])?f->children[i]:0;}
 unsigned TabPage(unsigned tab,unsigned index){unsigned p=Allocate(32);if(!p)return 0;Write(p,8);unsigned ok=app.Send(tab,0x133c,index,p),hwnd=ok?Read(p+24):0;Free(p);return hwnd;}
 unsigned Notify(unsigned target,unsigned source,unsigned code,unsigned parameter){
  if(!target){app.GiveTail(Application::TailNone);return 0;}unsigned p=Allocate(16);if(!p)return 1;
  unsigned info[]={source,User::ControlID(source),code,parameter};app.Store(p,info,16);unsigned zero=0;User::WindowLong(target,0,zero,true);
  auto*w=Begin(Application::HostPropertyNotify);WorkScope scope{*this,w};if(w){w->value[0]=p;w->value[1]=target;}
  Phase(w,1);app.Send(target,0x4e,info[1],p);unsigned result=0;if(User::IsWindow(target))User::WindowLong(target,0,result,false);Free(p);return result;
 }
 void SetChanged(unsigned index,bool changed){unsigned p=Allocate(32);if(!p)return;Write(p,8);if(app.Send(f->tab,0x133c,index,p)){unsigned value=Read(p+28);Write(p+28,changed?value|1:value&~1u);app.Send(f->tab,0x133d,index,p);}Free(p);}
 void EnableHelp(bool enabled){unsigned h=User::GetDlgItem(f->hwnd,9);if(h)User::EnableWindow(h,enabled);}
 void ShowPage(unsigned hwnd,bool visible){if(User::IsWindow(hwnd))User::ShowWindow(hwnd,visible?5:0);}
 void FocusPage(unsigned hwnd){if(!Alive())return;if(User::GetFocus()==f->tab)return;unsigned first=User::NextDialogItem(hwnd,0,false);app.SetFocus(first?first:f->tab);}
 bool Wizard(){return (Read(f->state+8)&0x1402020)!=0;}
 // USER32 EnableWindow, as COMCTL32 calls it: WM_ENABLE repaints the button.
 void Enable(unsigned h,bool on){if(!h)return;unsigned a[]={h,on?1u:0u};app.Api("EnableWindow",a);}
 // The template a page was made from, for the identifiers PSN_SETACTIVE,
 // PSN_WIZBACK and PSN_WIZNEXT answer with.
 unsigned PageTemplateName(unsigned page){unsigned name=app.Win16PageTemplateName(page);return name?name:Read(page+12);}
 // the wizard caption is the text of the page's tab item.
 void WizardTitle(unsigned index){unsigned item=Allocate(28+0xb2*2);if(!item)return;Zero(item,28+2);Write(item,1);Write(item+12,item+28);Write(item+16,0xb2);
  if(app.Send(f->tab,0x133c,index,item)&&Word(item+28)){unsigned text=Narrow(item+28);if(text){app.Send(f->hwnd,0xc,0,text);Free(text);}}Free(item);}
 // a wizard page takes the rectangle WM_INITDIALOG kept at +50h;
 // the tab and the header line stay hidden.
 void PositionPage(unsigned hwnd){if(Wizard()){unsigned header=User::GetDlgItem(f->hwnd,0x3027);if(header)User::ShowWindow(header,0);User::ShowWindow(f->tab,0);
   const unsigned position[]={hwnd,0,Read(f->state+0x50),Read(f->state+0x54),Read(f->state+0x58),Read(f->state+0x5c),0};app.PositionApi("SetWindowPos",position);return;}
  Gdi::RECT r{};if(!User::GetClientRect(f->tab,&r))return;app.Store(f->scratch,&r,16);app.Send(f->tab,0x1328,0,f->scratch);app.Read(f->scratch,&r,16);Gdi::POINT origin{0,0};User::ScreenToClient(f->tab,&origin);Gdi::POINT p{r.left-origin.x,r.top-origin.y};User::ScreenToClient(f->hwnd,&p);const unsigned position[]={hwnd,0,unsigned(p.x),unsigned(p.y),unsigned(r.right-r.left),unsigned(r.bottom-r.top),0};app.PositionApi("SetWindowPos",position);}
 void ActivateCurrent(unsigned state){if(recorded)Activate(1);else Win98PropertySheet::Activate(*this,state,1);}
 // the frame caption from the header's caption (+18h), a string
 // or a resource of the header's instance, as "Properties for %s" with
 // PSH_PROPTITLE (string 1042h of COMCTL32).
 bool SetCaption(){unsigned s=f->state,caption=Read(s+24),wide=caption;
  if(caption<65536){wide=f->scratch+512;LoadString(Read(s+16),caption,wide,128,0x409);}
  unsigned narrow=Narrow(wide);if(wide>=65536&&!narrow)return false;
  unsigned text=narrow;
  if(Read(s+8)&1){unsigned format=f->scratch+1024;app.Store(format,OriginalPropertySheetAssets::CaptionFormat,sizeof OriginalPropertySheetAssets::CaptionFormat);
   if(WideLength(format)+WideLength(wide)+2<178){unsigned aformat=Narrow(format);Write(f->scratch+2044,narrow);app.Printf(f->scratch+2048,aformat,f->scratch+2044);Free(aformat);text=f->scratch+2048;}}
  if(text)app.Send(f->hwnd,0xc,0,text);if(narrow>=65536)Free(narrow);return true;}
 // PSM_SETTITLEA/W: the new title replaces the header's caption (a
 // copy the sheet owns, flag 10h of +60h), PSH_PROPTITLE comes from dwStyle,
 // and the caption is set again.
 void SetTitle(unsigned style,unsigned title,bool wide){if(!title)return;unsigned s=f->state;
  unsigned copy=title;if(title>=65536){copy=wide?0:CloneA(title);if(wide){unsigned n=WideLength(title);copy=Allocate((n+1)*2);if(copy)Copy(copy,title,(n+1)*2);}if(!copy)return;}
  if((Read(s+0x60)&0x10)&&Read(s+24)>=65536)Free(Read(s+24));
  Write(s+0x60,Read(s+0x60)|0x10);Write(s+24,copy);Write(s+8,(Read(s+8)&~1u)|(style&1));SetCaption();}
 // PSN_KILLACTIVE may veto; a tab that refuses the index keeps the
 // old selection, and the current page is activated again either way.
 unsigned SetCurrent(unsigned index,unsigned direction){if(index>=100)return 0;
  if(recorded){auto*w=Begin(Application::HostPropertyCommand);w->value[15]=1;w->value[0]=index;w->value[1]=direction;return ContinueSetCurrent(w,false);}
  if(Win98PropertySheet::KillActive(*this,f->state)||!Alive())return 0;unsigned old=Selection(),kept=1;
  if(app.Send(f->tab,0x130c,index,0)==~0u){kept=0;app.Send(f->tab,0x130c,old,0);}Win98PropertySheet::Activate(*this,f->state,direction);return kept;}
 // Back and Next. The page answers PSN_WIZBACK/PSN_WIZNEXT with 0
 // for the neighbour, -1 to stay, or the template of the page to go to.
 void WizardStep(bool next){unsigned s=f->state,direction=next?1u:~0u;
  unsigned answer=Win98PropertySheet::Notify(*this,s,Read(s+0x40),next?~206u:~205u,0);if(!Alive()||answer==~0u)return;
  unsigned index=Win98PropertySheet::NextPage(*this,s,Read(s+0x40),answer,direction);if(index!=~0u)SetCurrent(index,direction);}
 // every wizard button back to BS_PUSHBUTTON before DM_SETDEFID.
 void PlainButtons(){static const unsigned ids[]={0x3023,0x3024,0x3025,2,9};for(unsigned id:ids)if(unsigned b=User::GetDlgItem(f->hwnd,id))app.Send(b,0xf4,0,1);}
 bool Within(unsigned child){for(unsigned w=child,depth=0;w&&depth<64;++depth){w=User::Parent(w);if(w==f->hwnd)return true;}return false;}
 // PSM_SETWIZBUTTONS.
 void SetWizardButtons(unsigned flags){unsigned hwnd=f->hwnd,s=f->state,focus=User::GetFocus();app.Send(hwnd,0x400,0,0);
  unsigned back=User::GetDlgItem(hwnd,0x3023),next=User::GetDlgItem(hwnd,0x3024),finish=User::GetDlgItem(hwnd,0x3025);
  Enable(back,flags&1);Enable(next,(flags>>1)&1);
  unsigned hide=finish,show=next;if(flags&0xc){hide=next;show=finish;Enable(finish,(flags>>2)&1);}
  if(!(Read(s+8)&0x10)){if(hide)User::ShowWindow(hide,0);if(show)User::ShowWindow(show,5);}
  bool refocus=focus&&(app.Send(focus,0x87,0,0)&0x30);
  if(!refocus)refocus=!focus||!User::IsVisible(focus)||!User::IsEnabled(focus);
  static const unsigned order[]={0x3024,0x3025,0x3023,2,0x3021};unsigned i=0,button=0;
  for(;i<4;++i){button=User::GetDlgItem(hwnd,order[i]);if(button&&(User::Style(button)&0x10000000)&&User::IsEnabled(button))break;}
  Write(s+0x44,order[i]);if(refocus)app.SetFocus(button?button:hwnd);
  PlainButtons();app.Send(hwnd,0x401,order[i],0);}
 // PSM_SETFINISHTEXT: Finish replaces Back (and Next without
 // PSH_WIZARDHASFINISH), takes the text and becomes the default button.
 void SetFinishText(unsigned text,bool wide){unsigned hwnd=f->hwnd,s=f->state,finish=User::GetDlgItem(hwnd,0x3025),focus=User::GetFocus();bool moved=false;
  if(!(Read(s+8)&0x10)){unsigned next=User::GetDlgItem(hwnd,0x3024);if(next==focus)moved=true;if(next)User::ShowWindow(next,0);}
  unsigned back=User::GetDlgItem(hwnd,0x3023);if(back==focus)moved=true;if(back)User::ShowWindow(back,0);
  if(text&&finish){unsigned narrow=wide?Narrow(text):text;if(narrow){app.Send(finish,0xc,0,narrow);if(wide&&narrow!=text)Free(narrow);}}
  if(finish){User::ShowWindow(finish,5);Enable(finish,true);}PlainButtons();app.Send(hwnd,0x401,0x3025,0);Write(s+0x44,0x3025);
  if(moved&&finish)app.SetFocus(finish);}
 void Applied(){unsigned button=User::GetDlgItem(f->hwnd,0x3021);if(button)User::EnableWindow(button,false);app.Send(f->hwnd,0x401,1,0);Write(f->state+0x44,1);}
 void Release(){if(f->released)return;auto*w=Begin(Application::HostPropertyRelease);ContinueRelease(w,false);}
 void RemovePage(unsigned index){if(index>=Read(f->state+0x1c))return;auto*w=Begin(Application::HostPropertyRemove);w->value[0]=index;ContinueRemove(w,false);}
 bool SyncAnsi(unsigned page){unsigned shadow=Read(page-4);if(!shadow)return true;Win98PropertyPages::FreeFields(*this,page);unsigned size=Read(shadow);Copy(page,shadow,size>48?48:size);return Win98PropertyPages::CloneFields(*this,page,1);}
 unsigned CreatePageWindow(unsigned index){
  if(recorded){auto*w=Begin(Application::HostPropertyPageWindow);if(w){w->value[0]=index;return ContinuePageWindow(w,false);}}
  unsigned page=Read(Read(f->state+0x24)+index*4);if(!Win98PropertyPages::Callback(*this,page,2)||!Alive())return 0;
  if(!LocalSize(page-24)||!SyncAnsi(page))return 0;unsigned flags=Read(page+4),data=0,size=0,resource=0;
  if(flags&0x8000){app.last_error=50;return 0;}
  if(flags&1){data=Read(page+12);while(size<65536&&app.virtual_memory.Access(data,size+1,false)){unsigned next=((data+size)&~4095u)+4096-data;if(next>65536)next=65536;if(!app.virtual_memory.Access(data,next,false))break;size=next;}}
  else{resource=FindDialog(Read(page+8),Read(page+12),0x409);if(resource){unsigned a[]={Read(page+8),resource};size=app.ResourceApi("SizeofResource",a);data=LoadResource(a[0],resource);}}
  if(!data||!size||size>65536)return 0;unsigned char*bytes=new unsigned char[size];if(!bytes)return 0;
  unsigned staging=Allocate(size);if(!staging){delete[]bytes;return 0;}Copy(staging,data,size);unsigned saved=0;Win98PropertySheet::PageStyle(*this,staging,saved);app.Read(staging,bytes,size);
  unsigned shadow=Read(page-4),parameter=shadow?shadow:((Read(page+4)&0x200)?page-8:page);
  unsigned hwnd=app.CreateDialogTemplate(bytes,size,f->hwnd,Read(page+24),parameter,false,Read(page+8));Free(staging);delete[]bytes;
  if(!Alive()){if(hwnd&&User::IsWindow(hwnd))app.Api("DestroyWindow",&hwnd);return 0;}if(shadow&&!SyncAnsi(page))return 0;
  if(hwnd){f->children[index]=hwnd;unsigned item=Allocate(32);if(item){Write(item,8);app.Send(f->tab,0x133c,index,item);Write(item+24,hwnd);app.Send(f->tab,0x133d,index,item);Free(item);}}
  return hwnd;
 }
};
unsigned Application::PropertySheetEnvironment::ContinueNotify(PropertyWork*w,bool resumed){
 WorkScope scope{*this,w};if(resumed)app.ResumeInner();unsigned result=0;
 if(User::IsWindow(w->value[1]))User::WindowLong(w->value[1],0,result,false);Free(w->value[0]);return result;
}
unsigned Application::PropertySheetEnvironment::ContinueRelease(PropertyWork*w,bool resumed){
 WorkScope scope{*this,w};unsigned s=f->state;
 if(resumed)app.ResumeInner();else{f->released=true;
  if((Read(s+0x60)&0x10)&&Read(s+24)>=65536){Free(Read(s+24));Write(s+24,0);}w->value[0]=Read(s+0x1c);}
 while(w->value[0]){unsigned page=Read(Read(s+0x24)+(--w->value[0])*4);Phase(w,1);DestroyPage(Read(page-24));}
 Write(s+0x1c,0);Write(s+0x38,0);return 1;
}
unsigned Application::PropertySheetEnvironment::ContinueRemove(PropertyWork*w,bool resumed){
 WorkScope scope{*this,w};unsigned index=w->value[0];
 if(resumed){app.ResumeInner();if(w->phase==2)goto released;}else{
  w->value[1]=Read(f->state+0x1c);w->value[2]=Read(f->state+0x24);
  unsigned page=Read(w->value[2]+index*4);w->value[3]=PageWindow(index);w->value[4]=Read(page-24);
  if(w->value[3]){if(w->kept)app.threads[app.current_thread].host_frames[w->index].b=w->value[3];Phase(w,1);app.GiveTail(Application::TailNone);app.Api("DestroyWindow",&w->value[3]);}
 }
 app.Send(f->tab,0x1308,index,0);Phase(w,2);DestroyPage(w->value[4]);
 released:
 for(unsigned i=index;i+1<w->value[1];++i){Write(w->value[2]+i*4,Read(w->value[2]+(i+1)*4));f->children[i]=f->children[i+1];}
 f->children[w->value[1]-1]=0;Write(f->state+0x1c,w->value[1]-1);return 1;
}
unsigned Application::PropertySheetEnvironment::AddPage(unsigned handle){
 auto*w=Begin(Application::HostPropertyAdd);w->value[0]=handle;return ContinueAdd(w,false);
}
unsigned Application::PropertySheetEnvironment::ContinueAdd(PropertyWork*w,bool resumed){
 WorkScope scope{*this,w};unsigned answer=resumed?app.ResumeInner():0,s=f->state;
 struct Buffers{PropertySheetEnvironment&e;PropertyWork*w;~Buffers(){e.Free(w->value[3]);e.Free(w->value[4]);}}buffers{*this,w};
 if(resumed){if(w->phase==2)goto created;goto inserted;}
 w->value[1]=Read(s+0x1c);
 if(!w->value[0]||w->value[1]>=99||!app.AllocationSize(w->value[0]-24))return 0;
 w->value[2]=Read(w->value[0]-8);Write(Read(s+0x24)+w->value[1]*4,w->value[2]);f->children[w->value[1]]=0;Write(s+0x1c,w->value[1]+1);
 w->value[3]=Allocate(0x1c8);w->value[4]=Allocate(32);
 if(!w->value[3]||!w->value[4]||!Win98PropertySheet::PageInfo(*this,w->value[2],w->value[3],0x409,7))goto failed;
 {
 auto*d=app.FindDialog(f->hwnd);Gdi::RECT rect={0,0,int(Read(w->value[3])),int(Read(w->value[3]+4))};if(d)Win98Dialog::MapRect(&rect,d->xunit,d->yunit);
 Write(w->value[2]-16,rect.right);Write(w->value[2]-12,rect.bottom);
 unsigned values[]={0xbu|(Read(w->value[3]+0x58)?4u:0u),0,0,w->value[3]+0x64,0,~0u,0,0};app.Store(w->value[4],values,sizeof values);
 Phase(w,1);app.Send(f->tab,0x133e,w->value[1],w->value[4]);
 }
 inserted:
 if(Read(w->value[2]+4)&0x400){Phase(w,2);answer=CreatePageWindow(w->value[1]);
 created:
  if(!answer){app.Send(f->tab,0x1308,w->value[1],0);goto failed;}
 }
 if(Read(s+0x40)>=w->value[1])Write(s+0x40,Read(s+0x40)+1);return 1;
 failed:
 Write(s+0x1c,w->value[1]);f->children[w->value[1]]=0;return 0; // Failure leaves the public page owned by the caller.
}
unsigned Application::PropertySheetEnvironment::RemoveRequest(unsigned index,unsigned handle){
 if(handle)for(unsigned i=0;i<Read(f->state+0x1c);++i)if(Read(Read(Read(f->state+0x24)+i*4)-24)==handle){index=i;break;}
 if(index>=Read(f->state+0x1c))return 0;
 auto*w=Begin(Application::HostPropertyRemoveRequest);w->value[0]=index;return ContinueRemoveRequest(w,false);
}
unsigned Application::PropertySheetEnvironment::ContinueRemoveRequest(PropertyWork*w,bool resumed){
 WorkScope scope{*this,w};if(resumed){app.ResumeInner();if(w->phase==3)return 1;if(w->phase==2)goto activated;goto killed;}
 w->value[1]=Selection();
 if(w->value[0]==w->value[1]){Phase(w,1);Win98PropertySheet::KillActive(*this,f->state);
 killed:
  unsigned next=w->value[1]?w->value[1]-1:1;
  if(app.Send(f->tab,0x130c,next,0)==~0u)app.Send(f->tab,0x130c,0,0);Phase(w,2);Activate(1);
 }
 activated:
 if(unsigned hwnd=PageWindow(w->value[0]))if(Read(f->state+0x38)==hwnd)Write(f->state+0x38,0);
 Phase(w,3);RemovePage(w->value[0]);return 1;
}
unsigned Application::PropertySheetEnvironment::ContinueSetCurrent(PropertyWork*w,bool resumed){
 WorkScope scope{*this,w};unsigned answer=resumed?app.ResumeInner():0;
 if(resumed&&w->phase==2)return w->value[2];
 if(!resumed){Phase(w,1);answer=Win98PropertySheet::KillActive(*this,f->state);app.GiveTail(Application::TailNone);}
 if(answer||!Alive())return 0;
 unsigned old=Selection();w->value[2]=1;
 if(app.Send(f->tab,0x130c,w->value[0],0)==~0u){w->value[2]=0;app.Send(f->tab,0x130c,old,0);}
 Phase(w,2);Activate(w->value[1]);return w->value[2];
}
unsigned Application::PropertySheetEnvironment::QuerySiblings(unsigned wp,unsigned lp){
 auto*w=Begin(Application::HostPropertyQuery);w->value[0]=wp;w->value[1]=lp;return ContinueQuery(w,false);
}
unsigned Application::PropertySheetEnvironment::ContinueQuery(PropertyWork*w,bool resumed){
 WorkScope scope{*this,w};unsigned answer=resumed?app.ResumeInner():0;
 if(resumed){if(answer)return answer;++w->value[2];}
 for(;w->value[2]<Read(f->state+0x1c)&&Alive();++w->value[2])if(unsigned page=PageWindow(w->value[2])){
  Phase(w,1);answer=app.Send(page,0x46c,w->value[0],w->value[1]);if(answer)break;
 }return answer;
}
unsigned Application::PropertySheetEnvironment::Apply(unsigned command){auto*w=Begin(Application::HostPropertyApply);w->value[0]=command;return ContinueApply(w,false);}
unsigned Application::PropertySheetEnvironment::ContinueApply(PropertyWork*w,bool resumed){
 WorkScope scope{*this,w};unsigned s=f->state,answer=resumed?app.ResumeInner():0;
 if(resumed){if(w->phase==2)goto notified;if(w->phase==3)return w->value[7];}
 if(!resumed){w->value[3]=~201u;w->value[4]=Read(s+0x48);w->value[7]=1;
  if(w->value[0]==1){w->value[1]=1;w->value[2]=1;}
  else if(w->value[0]==2||w->value[0]==8){w->value[1]=1;w->value[2]=w->value[0]==8;w->value[3]=~202u;}
  else if(w->value[0]!=0x3021)return 0;
  if(w->value[3]==~201u){Phase(w,1);answer=Win98PropertySheet::Notify(*this,s,Read(s+0x40),~200u,0);}
 }
 if(w->value[3]==~201u){if(answer||!Alive())return 0;w->value[4]=1;}
 for(;w->value[5]<Read(s+0x1c)&&Alive();++w->value[5]){if(!PageWindow(w->value[5]))continue;
  Phase(w,2);answer=Win98PropertySheet::Notify(*this,s,w->value[5],w->value[3],w->value[2]);
 notified:
  if(!Alive())return 0;w->value[6]=answer;
  if(answer){w->value[7]=0;w->value[1]=0;break;}
  if(w->value[3]==~201u&&!w->value[1]&&Read(s+0x4c))w->value[1]=1;SetChanged(w->value[5],false);
 }
 if(w->value[7])Write(s+0x38,0);else if(w->value[6]!=2)Select(w->value[5]);
 if(w->value[7])Write(s+0x48,w->value[4]);
 if(!w->value[1]){if(w->value[7])Applied();if(w->value[6]!=2){Phase(w,3);Activate(1);}}
 return w->value[7];
}
unsigned Application::PropertySheetEnvironment::Command(unsigned command){auto*w=Begin(Application::HostPropertyCommand);w->value[15]=2;w->value[0]=command;return ContinueCommand(w,false);}
unsigned Application::PropertySheetEnvironment::ContinueCommand(PropertyWork*w,bool resumed){
 WorkScope scope{*this,w};unsigned answer=resumed?app.ResumeInner():0,s=f->state;
 if(resumed){if(w->phase==2)goto applied;if(w->phase==3)goto resets;}
 if(!resumed&&(w->value[0]==2||w->value[0]==8)){Phase(w,1);answer=Win98PropertySheet::Notify(*this,s,Read(s+0x40),~208u,0);}
 if((w->value[0]==2||w->value[0]==8)&&(answer||!Alive()))return 1;
 Phase(w,2);answer=Apply(w->value[0]);
 applied:
 if(!Alive())return 1;
 if(w->value[0]==1&&answer&&!Read(s+0x38)){
  w->value[2]=Read(s+0x1c);
 resets:
  while(w->value[2]&&Alive()){unsigned index=--w->value[2];if(PageWindow(index)){Phase(w,3);Win98PropertySheet::Notify(*this,s,index,~210u,1);}}
 }
 app.GiveTail(Application::TailNone);
 if(Alive()&&!Read(s+0x38)){f->closing=true;app.EndDialog(f->hwnd,Read(s+0x48));}return 1;
}
unsigned Application::PropertySheetEnvironment::ContinuePageWindow(PropertyWork*w,bool resumed){
 WorkScope scope{*this,w};unsigned answer=resumed?app.ResumeInner():0,index=w->value[0];
 unsigned page=resumed?w->value[1]:Read(Read(f->state+0x24)+index*4);w->value[1]=page;
 if(!resumed){Phase(w,1);answer=Win98PropertyPages::Callback(*this,page,2);app.GiveTail(Application::TailNone);}
 if(w->phase==1){
  if(!answer||!Alive()||!LocalSize(page-24)||!SyncAnsi(page))return 0;
  unsigned flags=Read(page+4),data=0,size=0,resource=0;
  if(flags&0x8000){app.last_error=50;return 0;}
  if(flags&1){data=Read(page+12);while(size<65536&&app.virtual_memory.Access(data,size+1,false)){unsigned next=((data+size)&~4095u)+4096-data;if(next>65536)next=65536;if(!app.virtual_memory.Access(data,next,false))break;size=next;}}
  else{resource=FindDialog(Read(page+8),Read(page+12),0x409);if(resource){unsigned a[]={Read(page+8),resource};size=app.ResourceApi("SizeofResource",a);data=LoadResource(a[0],resource);}}
  if(!data||!size||size>65536)return 0;
  w->size=size;w->bytes=new unsigned char[size];unsigned staging=Allocate(size);if(!staging)return 0;w->value[2]=staging;
  Copy(staging,data,size);unsigned saved=0;Win98PropertySheet::PageStyle(*this,staging,saved);app.Read(staging,w->bytes,size);
  unsigned shadow=Read(page-4),parameter=shadow?shadow:((Read(page+4)&0x200)?page-8:page);Phase(w,2);
  answer=app.CreateDialogTemplate(w->bytes,size,f->hwnd,Read(page+24),parameter,false,Read(page+8));
 }
 Free(w->value[2]);delete[]w->bytes;w->bytes=nullptr;w->size=0;
 if(!Alive()){if(answer&&User::IsWindow(answer))app.Api("DestroyWindow",&answer);return 0;}
 if(Read(page-4)&&!SyncAnsi(page))return 0;
 if(answer){f->children[index]=answer;unsigned item=Allocate(32);if(item){Write(item,8);app.Send(f->tab,0x133c,index,item);Write(item+24,answer);app.Send(f->tab,0x133d,index,item);Free(item);}}
 return answer;
}
unsigned Application::PropertySheetEnvironment::Activate(unsigned direction){
 auto*w=Begin(Application::HostPropertyActivate);if(!w)return Win98PropertySheet::Activate(*this,f->state,direction);
 w->value[0]=direction;w->value[1]=1;return ContinueActivate(w,false);
}
unsigned Application::PropertySheetEnvironment::ContinueActivate(PropertyWork*w,bool resumed){
 WorkScope scope{*this,w};unsigned answer=resumed?app.ResumeInner():0,s=f->state;
 if(resumed){if(w->phase==1){w->value[3]=answer;goto created;}if(w->phase==2)goto removed;if(w->phase==3)goto notified;}
 while(w->value[1]<=Read(s+0x1c)&&Alive()){
  w->value[2]=Selection();if(int(w->value[2])<0||w->value[2]>=Read(s+0x1c))return 0;
  w->value[3]=PageWindow(w->value[2]);
  if(!w->value[3]){Phase(w,1);w->value[3]=CreatePageWindow(w->value[2]);}
 created:
  if(!Alive())return 0;
  if(!w->value[3]){Phase(w,2);RemovePage(w->value[2]);
 removed:
   if(!Alive())return 0;Select(0);++w->value[1];continue;}
  PositionPage(w->value[3]);Phase(w,3);answer=Win98PropertySheet::Notify(*this,s,w->value[2],~199u,0);
 notified:
  if(!Alive())return 0;
  if(answer){unsigned next=Win98PropertySheet::NextPage(*this,s,w->value[2],answer==~0u?0:answer,w->value[0]);
   if(answer==~0u&&(next==w->value[2]||next>=Read(s+0x1c)))next=Read(s+0x40);
   if(next!=~0u){Select(next);ShowPage(w->value[3],false);++w->value[1];continue;}}
  unsigned page=Read(Read(s+0x24)+w->value[2]*4);
  if(Read(s+8)&0x200)EnableHelp((Read(page+4)&0x20)!=0);if(Read(s+8)&0x1402020)WizardTitle(w->value[2]);
  Write(s+0x60,Read(s+0x60)|1);ShowPage(w->value[3],true);
  unsigned previous=Read(s+0x38);if(previous&&previous!=w->value[3])ShowPage(previous,false);if(!Alive())return 0;
  Write(s+0x60,Read(s+0x60)&~1u);Write(s+0x38,w->value[3]);Write(s+0x40,w->value[2]);FocusPage(w->value[3]);return 1;
 }return 0;
}
unsigned Application::ResumePropertyWork(unsigned kind,unsigned index){
 auto&t=threads[current_thread];
 if(kind==HostPropertyNonClient||kind==HostPropertyPageNonClient){unsigned hwnd=t.host_frames[index].a;++t.host_resume_at;ResumeInner();
  if(User::IsWindow(hwnd))FinishPropertyNonClient(hwnd);PopHostFrame();return 1;}
 if(kind==HostPropertyModalFinish){auto frame=t.host_frames[index];
  if(frame.d==1){++t.host_resume_at;ResumeInner();}
  if(auto*dialog=FindDialog(frame.a))*dialog=Dialog{};
  unsigned font=frame.data[1];if(font&&font!=system&&font!=fixed)Api("DeleteObject",&font);
  if(frame.data[2])User::EnableWindow(frame.b,true);
  GiveTail(TailNone);
  if(User::IsWindow(frame.c))SetFocus(frame.c);else if(frame.b&&User::IsWindow(frame.b))SetFocus(frame.b);
  if(User::WinEventListening())WinEventNotify(0x11,frame.a,0,0,false);
  if(log){char text[96];snprintf(text,sizeof text,"modal dialog end result=%u",frame.data[0]);log(text);}
  PopHostFrame();return frame.data[0];
 }
 auto*w=PropertyWork::Find(*this,index,kind);if(!w||!w->kept){Fail("A saved property sheet lost its registered operation");return 0;}
 ++t.host_resume_at;PropertySheetEnvironment e{*this,w->sheet,true};
 switch(kind){
 case HostPropertyNotify:return e.ContinueNotify(w,true);
 case HostPropertyPageWindow:return e.ContinuePageWindow(w,true);
 case HostPropertyActivate:return e.ContinueActivate(w,true);
 case HostPropertyInit:return ContinuePropertyInit(w,true);
 case HostPropertyRelease:return e.ContinueRelease(w,true);
 case HostPropertyRemove:return e.ContinueRemove(w,true);
 case HostPropertyCommand:return w->value[15]==1?e.ContinueSetCurrent(w,true):e.ContinueCommand(w,true);
 case HostPropertyApply:return e.ContinueApply(w,true);
 case HostPropertyQuery:return e.ContinueQuery(w,true);
 case HostPropertyAdd:return e.ContinueAdd(w,true);
 case HostPropertyRemoveRequest:return e.ContinueRemoveRequest(w,true);
 case HostPropertyProcedure:{PropertySheetEnvironment::WorkScope scope{e,w};unsigned answer=ResumeInner();
  if(w->value[1]==0x110||!User::IsWindow(w->value[0]))return 0;unsigned result=w->value[3]?answer:w->value[2];User::WindowLong(w->value[0],0,result,true);return 1;}
 default:Fail("A saved property sheet has an unknown operation");return 0;
 }
}
unsigned Application::PropertySheetApi(ApiName name,const unsigned*a){
 const bool gate=TakeFresh();
 PropertySheetEnvironment e{*this};unsigned header=a[0];if(!virtual_memory.Access(header,36,false)){last_error=87;return ~0u;}
 unsigned size=Read32(header);if(Win98PropertySheet::HeaderSize(size)&&!virtual_memory.Access(header,size,false)){last_error=87;return ~0u;}
 unsigned count=Read32(header+24),array=Read32(header+32);if(count>99){
  // The original still destroys supplied handles on invalid counts. Native
  // pointers must cover that ownership list before entering the unchecked core.
  if(!(Read32(header+4)&8)&&(count>4096||!virtual_memory.Access(array,count*4,false))){last_error=87;return ~0u;}
 }
 if(!(Read32(header+4)&8)&&count&&!virtual_memory.Access(array,count*4,false)){last_error=87;return ~0u;}
 unsigned scratch=e.Allocate(52);
 const bool ansi=!API_IS(name,"PropertySheetW");
 const bool kept=gate&&PushHostFrame(HostPropertyApi,scratch,0);
 if(!scratch){ContinuePropertyReleaseInput(header,false,kept);if(kept)PopHostFrame();return ~0u;}
 if(!kept){unsigned result=ansi?Win98PropertySheet::Ansi(e,header,scratch):Win98PropertySheet::Wide(e,header);e.Free(scratch);return result;}
 unsigned result=~0u;
 if(!ansi||(Win98PropertySheet::HeaderSize(size)&&Win98PropertySheet::ConvertHeader(e,header,scratch))){
  unsigned converted=ansi?scratch:header;
  threads[current_thread].host_frames[threads[current_thread].host_frame_count-1].b=ansi;
  if(Win98PropertySheet::HeaderSize(Read32(converted))&&!(Read32(converted+4)&0xfc000000u)&&Read32(converted+24)<100){
   unsigned state_address=e.Allocate(0x27c);
   if(state_address){Write32(state_address+0x60,0);e.Copy(state_address+4,converted,Read32(converted));
    if(!(Read32(state_address+8)&0x100))Write32(state_address+0x28,0);Write32(state_address+0x24,state_address+0xec);
    if(kept)threads[current_thread].host_frames[threads[current_thread].host_frame_count-1].d=2;
    result=ContinuePropertyCollect(state_address,converted,ansi,false);
   }else ContinuePropertyReleaseInput(converted,false,true);
  }else ContinuePropertyReleaseInput(converted,false,true);
  if(ansi)Win98PropertySheet::FreeHeader(e,scratch);
 }else ContinuePropertyReleaseInput(header,false,true);
 if(kept)PopHostFrame();e.Free(scratch);return result;
}
unsigned Application::ContinuePropertyReleaseInput(unsigned header,bool resumed,bool recorded){
 auto&t=threads[current_thread];const unsigned at=resumed?t.host_resume_at:t.host_frame_count;
 PropertySheetEnvironment e{*this};GiveTail(TailNone);
 if(!resumed){
  if(Read32(header+4)&8)return ~0u;
  if(!recorded||!at||t.host_frames[at-1].kind!=HostPropertyApi){Win98PropertySheet::ReleaseInput(e,header);return ~0u;}
  auto&owner=t.host_frames[at-1];owner.d=3;owner.data[0]=header;
  if(!PushHostFrame(HostPropertyReleaseInput,header,Read32(header+24),0,1)){Win98PropertySheet::ReleaseInput(e,header);return ~0u;}
 }
 struct Record{Application&app;~Record(){app.GiveTail(TailNone);app.PopHostFrame();}}record{*this};
 if(resumed){++t.host_resume_at;ResumeInner();}
 auto&frame=t.host_frames[at];
 while(frame.b){unsigned page=Read32(Read32(frame.a+32)+4*(--frame.b));GiveTail(TailRecord);e.DestroyPage(page);GiveTail(TailNone);}
 return ~0u;
}
unsigned Application::ContinuePropertyCollect(unsigned s,unsigned header,bool ansi,bool resumed){
 auto&t=threads[current_thread];unsigned at=resumed?t.host_resume_at++:t.host_frame_count;
 bool inline_pages=(Read32(s+8)&8)!=0;
 const bool kept=resumed||(t.host_frame_count&&t.host_frames[t.host_frame_count-1].kind==HostPropertyApi&&PushHostFrame(HostPropertyCollect,s,Read32(header+32),0,inline_pages));
 PropertySheetEnvironment e{*this,nullptr,true};
 if(!kept){Fail("A property-sheet collector lost its registered caller");return ~0u;}
 auto&record=t.host_frames[at];
 if(!resumed){record.data[0]=header;record.data[1]=ansi;record.data[2]=inline_pages?Read32(s+0x1c):Read32(header+24);Write32(s+0x1c,0);}
 auto store_page=[&](unsigned page){unsigned count=Read32(s+0x1c);Write32(Read32(s+0x24)+count*4,page);if(page)Write32(s+0x1c,count+1);++record.c;
  if(inline_pages)record.b+=Read32(record.b);};
 unsigned result=0;
 if(resumed){result=ResumeInner();if(record.data[3]==2){PopHostFrame();return result;}store_page(result);}
 for(;record.c<record.data[2];){unsigned page=inline_pages?record.b:Read32(Read32(header+32)+record.c*4);
  if(inline_pages||!e.LocalSize(page-24)){record.data[3]=1;page=e.CreatePage(page,ansi,Read32(s+0x60)&8);}store_page(page);
 }
 for(unsigned i=0;i<Read32(s+0x1c);++i){unsigned p=Read32(s+0x24)+4*i;Write32(p,Read32(Read32(p)-8));}
 if(!(Read32(s+8)&0x200))for(unsigned i=0;i<Read32(s+0x1c);++i)if(Read32(Read32(Read32(s+0x24)+4*i)+4)&0x20){Write32(s+8,Read32(s+8)|0x200);break;}
 record.data[3]=2;result=e.Present(s);PopHostFrame();return result;
}
unsigned Application::ResumePropertyApi(unsigned scratch,bool ansi){
 auto&t=threads[current_thread];++t.host_resume_at;
 unsigned result=ResumeInner();
 PropertySheetEnvironment e{*this};
 if(ansi&&scratch)Win98PropertySheet::FreeHeader(e,scratch);
 e.Free(scratch);PopHostFrame();return result;
}
void Application::FinishPropertySheet(unsigned state_address){
 if(!state_address){
  // A terminal fault can abandon a captured caller before its fiber ever
  // rebuilds. Once all fibers have unwound, reclaim any controller it owned.
  for(;;){auto*f=SheetFrame::head;while(f&&f->app!=this)f=f->next;if(!f)break;FinishPropertySheet(f->state);}
  for(;;){auto*w=PropertyWork::head;while(w&&w->app!=this)w=w->next;if(!w)return;delete w;}
 }
 auto*f=SheetFrame::head;while(f&&(f->app!=this||f->state!=state_address))f=f->next;
 if(!f)return;PropertySheetEnvironment e{*this,f};
 if(f->hwnd&&User::IsWindow(f->hwnd)){unsigned hwnd=f->hwnd;Api("DestroyWindow",&hwnd);}
 e.Release();
 if(auto*d=FindDialog(f->hwnd)){unsigned font=d->font;*d=Dialog{};if(font&&font!=system&&font!=fixed)Api("DeleteObject",&font);}
 e.Free(f->scratch);e.Free(f->state);
 if(f->disabled_owner&&User::IsWindow(f->disabled_owner))User::EnableWindow(f->disabled_owner,true);
 if(User::IsWindow(f->previous_focus)&&User::IsEnabled(f->previous_focus))SetFocus(f->previous_focus);
 delete f;
}
unsigned Application::ResumePropertySheet(unsigned state_address){
 auto&thread=threads[current_thread];if(auto*w=PropertyWork::Find(*this,thread.host_resume_at,HostPropertySheet)){++thread.host_resume_at;return ContinuePropertySheet(w,true);}
 auto*f=SheetFrame::head;while(f&&(f->app!=this||f->state!=state_address))f=f->next;
 if(!f){Fail("A saved property sheet lost its controller");return ~0u;}
 auto&t=threads[current_thread];const unsigned frame_at=t.host_resume_at++;
 unsigned result=RunModal(f->hwnd,true);
 if(frame_at<t.host_frame_count)t.host_frames[frame_at].d=2;
 PropertySheetEnvironment e{*this,f};e.Release();
 if(int(result)>0&&Read32(state_address+0x4c))result=Read32(state_address+0x4c);
 if(state==Failed||state==Exited)result=~0u;
 FinishPropertySheet(state_address);PopHostFrame();return result;
}
unsigned Application::ShowPropertySheet(unsigned state_address){
 auto&frame=*new SheetFrame(this,state_address);auto*w=new PropertyWork(*this,&frame,HostPropertySheet);
 const auto&t=threads[current_thread];
 w->kept=t.host_frame_count&&t.host_frames[t.host_frame_count-1].kind==HostPropertyCollect&&
  t.host_frames[t.host_frame_count-1].a==state_address&&PushHostFrame(HostPropertySheet,state_address);
 return ContinuePropertySheet(w,false);
}
unsigned Application::ContinuePropertySheet(PropertyWork*w,bool resumed){
 auto&frame=*w->sheet;unsigned state_address=frame.state;PropertySheetEnvironment e{*this,&frame,w->kept};
 struct Cleanup{Application&app;unsigned state;~Cleanup(){app.FinishPropertySheet(state);}}cleanup{*this,state_address};
 PropertySheetEnvironment::WorkScope scope{e,w};unsigned answer=resumed?ResumeInner():0;
 if(resumed){if(w->phase==3)goto precreated;if(w->phase==4)goto created;if(w->phase==1)goto modal_done;if(w->phase==2)goto released;}
 {
 unsigned flags=Read32(state_address+8),owner=Read32(state_address+12);w->value[0]=owner;
 if(!Read32(state_address+0x1c))return ~0u;
 // The controller owns modal sheets and modal wizards (PSH_WIZARD). The
 // Wizard97 styles, modeless sheets, RTL and resource-language flags need
 // controllers of their own, not a fake HWND.
 if(flags&(0x01402000u|0x400u|0x800u|0x200000u)){last_error=50;return ~0u;}
 const bool wizard=(flags&0x20)!=0;
 if(owner&&(!User::IsWindow(owner)||(User::WindowProcess(owner)&&User::WindowProcess(owner)!=process_id))){last_error=87;return ~0u;}
 Write32(state_address+0x48,~0u);Write32(state_address+0x38,0);Write32(state_address+0x4c,0);
 frame.scratch=e.Allocate(4096);if(!frame.scratch)return ~0u;
 Write32(frame.scratch,8);Write32(frame.scratch+4,8);unsigned common_args[]={frame.scratch};CommonInitApi("InitCommonControlsEx",common_args);if(this->state==Failed)return ~0u;
 // /: resource 1006 for a sheet, 1020 for a wizard; with
 // PSH_WIZARDCONTEXTHELP the wizard frame gets DS_CONTEXTHELP.
 using namespace OriginalPropertySheetAssets;unsigned bytes=wizard?sizeof Template1020:sizeof Template1006;
 unsigned resource=e.Allocate(bytes*2);if(!resource)return ~0u;w->value[1]=resource;w->size=bytes*2;Store(resource,wizard?Template1020:Template1006,bytes);
 if(wizard&&(flags&0x1000))Write32(resource,Read32(resource)|0x2000);
 frame.previous_focus=User::GetFocus();unsigned disabled=owner;
 for(unsigned depth=0;disabled&&(User::Style(disabled)&0x40000000)&&depth<192;++depth)disabled=User::Parent(disabled);
 if(disabled&&disabled!=User::GetDesktopWindow()&&User::IsEnabled(disabled)){User::EnableWindow(disabled,false);frame.disabled_owner=disabled;}
 }
 if(unsigned callback=Read32(state_address+0x28)){unsigned a[]={0,2,w->value[1]};e.Phase(w,3);Call(callback,a,3);}
 precreated:
 if(!e.Alive()){e.Free(w->value[1]);w->value[1]=0;return ~0u;}
 w->bytes=new unsigned char[w->size];Read(w->value[1],w->bytes,w->size);e.Phase(w,4);
 answer=CreateDialogTemplate(w->bytes,w->size,w->value[0],PropertySheetToken,state_address,true);
 created:
 frame.hwnd=answer;delete[]w->bytes;w->bytes=nullptr;w->size=0;e.Free(w->value[1]);w->value[1]=0;
 if(!frame.hwnd||!e.Alive()||frame.initializing){if(e.Alive())last_error=50;return ~0u;}
 if(auto*d=FindDialog(frame.hwnd))d->previous_focus=frame.previous_focus;
 if(w->kept)threads[current_thread].host_frames[w->index].b=frame.hwnd;
 e.Phase(w,1);answer=RunModal(frame.hwnd,w->kept);
 modal_done:
 w->value[2]=answer;e.Phase(w,2);e.Release();
 released:
 answer=w->value[2];
 if(int(answer)>0&&Read32(state_address+0x4c))answer=Read32(state_address+0x4c);
 return this->state==Failed||this->state==Exited?~0u:answer;
}
bool Application::PropertySheetWindow(unsigned hwnd){return FindSheet(this,hwnd)!=nullptr;}
bool Application::PropertySheetRecordable(unsigned hwnd,unsigned message,unsigned wp,unsigned lp){
 auto*f=FindSheet(this,hwnd,message==0x110?lp:0);if(!f)return false;
 const auto&t=threads[current_thread];
 for(unsigned i=0;i<t.host_frame_count;++i){const auto&r=t.host_frames[i];
  if(r.kind==HostPropertySheet&&r.a==f->state&&PropertyWork::Find(*this,i,HostPropertySheet)){
   if(message==0x110)return r.d==4;
   if((r.d==1||r.d==4)&&(message==0x465||message==0x466||message==0x467||message==0x472||message==0x46e||message==0x46c))return true;
   if((r.d==1||r.d==4)&&message==0x111){unsigned id=wp&65535;return id==9||(!(Read32(f->state+8)&0x1402020)&&(id==1||id==2||id==8||id==0x3021));}
   if(r.d==1&&message==0x4e&&lp&&Read32(lp)==f->tab){unsigned code=Read32(lp+8);return code==0xfffffdd8||code==0xfffffdd9;}
   if(message==0x82){unsigned at=t.host_frame_count-1;
    if(t.host_frames[at].kind==HostDialogProcedure&&t.host_frames[at].a==hwnd&&t.host_frames[at].b==0x82&&at)--at;
    const auto&top=t.host_frames[at];return top.kind==HostPropertyNonClient&&top.a==hwnd;}
  }
 }return false;
}
bool Application::PropertyWorkRecorded(unsigned thread,unsigned index,unsigned kind,unsigned phase)const{
 auto*w=PropertyWork::Find(*this,index,kind,thread);return w&&w->kept&&w->phase==phase&&w->sheet&&w->sheet->app==this&&
  (kind!=HostPropertyCommand||w->value[15]==2||(w->value[15]==1&&phase<=2))&&
  (kind!=HostPropertyRemove||phase!=1||(w->value[0]<99&&w->sheet->children[w->value[0]]==w->value[3]))&&
  (kind!=HostPropertyProcedure||phase!=2||(w->value[1]==0x111&&(w->value[4]&65535)==9));
}
// Original PropSheetDlgProc convention: handled messages report through
// DWL_MSGRESULT and return TRUE; WM_INITDIALOG returns FALSE because the sheet
// has already chosen the focus itself. Unhandled messages fall to DefDlgProc.
unsigned Application::ContinuePropertyInit(PropertyWork*w,bool resumed){
 auto*f=w->sheet;PropertySheetEnvironment e{*this,f,w->kept};PropertySheetEnvironment::WorkScope scope{e,w};
 const unsigned hwnd=w->value[6],s=f->state;unsigned&info=w->value[0];unsigned&item=w->value[1];unsigned&i=w->value[2];
 const bool wizard=(Read32(s+8)&0x1402020)!=0;
 auto fail=[&](const char*why){last_error=50;Fail(why);};
 Win98PropertySheet::Start start{};unsigned page=0,answer=resumed?ResumeInner():0;auto*d=FindDialog(hwnd);
 if(!resumed){
  f->hwnd=hwnd;f->tab=User::GetDlgItem(hwnd,0x3020);if(!f->tab||(!wizard&&(User::Style(f->tab)&0x200)==0)){fail("Original PropertySheet template requires a multiline tab");return true;}
  Write32(s,hwnd);Write32(s+0x3c,f->tab);Write32(s+0x40,0);Write32(s+0x84,1);Write32(s+0x44,1);unsigned v=s;User::WindowLong(hwnd,8,v,true);
  // A wizard skips the frame caption : each page's title becomes
  // the caption when the page is activated.
  if(!wizard&&!e.SetCaption())return true;
  if(Read32(s+8)&6){unsigned icon=Read32(s+20);if(Read32(s+8)&4)icon=e.LoadIcon(Read32(s+16),icon);if(icon)Send(hwnd,0x80,0,icon);}
  Send(f->tab,0x130e,8,0);Send(f->tab,0xb,0,0);
  auto initial=Win98PropertySheet::BeginStart(e,s,f->scratch+3072);w->value[3]=initial.index;w->value[4]=initial.name;
  info=e.Allocate(0x1c8);item=e.Allocate(32);if(!info||!item){e.Free(info);e.Free(item);return true;}
 }
 start={w->value[3],w->value[4]};
 if(resumed){if(w->phase==1)goto removed;if(w->phase==2){if(!e.Alive())return true;goto sized;}if(w->phase==3)goto activated;if(w->phase==4)goto eager_done;}
 for(;i<Read32(s+0x1c)&&e.Alive();){page=Read32(Read32(s+0x24)+4*i);
   if(!Win98PropertySheet::PageInfo(e,page,info,0x409,7)){e.Phase(w,1);e.RemovePage(i);
 removed:
 continue;}
   if(Read32(info+8)){fail("PropertySheet tab icons need the native tab image-list renderer");break;}
   unsigned width=Read32(info),height=Read32(info+4);Gdi::RECT page_rect={0,0,int(width),int(height)};Win98Dialog::MapRect(&page_rect,d->xunit,d->yunit);
   // Native font mapping uses the same bitmap provider as CreateDialogTemplate;
   // original ordinary pages normally share the sheet's dialog font.
   Write32(page-16,page_rect.right);Write32(page-12,page_rect.bottom);
   if(page_rect.right>f->page_width)f->page_width=page_rect.right;if(page_rect.bottom>f->page_height)f->page_height=page_rect.bottom;
   unsigned values[]={0xbu|(Read32(info+0x58)?4u:0u),0,0,info+0x64,0,~0u,0,0};Store(item,values,sizeof values);
   if(Send(f->tab,0x133e,1000,item)==~0u){fail("PropertySheet tab insertion failed");break;}
   Win98PropertySheet::MatchStart(e,start,Read32(s+8),i,info+0x64);w->value[3]=start.index;++i;
  }
  e.Free(info);e.Free(item);info=item=0;Send(f->tab,0xb,1,0);if(!e.Alive()||!Read32(s+0x1c))return true;
  if(unsigned callback=Read32(s+0x28)){unsigned a[]={hwnd,1,0};e.Phase(w,2);Call(callback,a,3);if(!e.Alive())return true;}
 sized:
 {
  // Preserve the original template's margins, control sizes and TCS style.
  // Adapt its rectangle to the largest page through real dialog/tab providers.
  // A wizard ( skips the tab sizing) keeps its thin tab and grows by
  // the difference between the largest page and the tab's client area.
  Gdi::RECT client{},tab{},inner{};User::GetClientRect(hwnd,&client);User::GetClientRect(f->tab,&inner);int dx=0,dy=0;
  if(wizard){dx=f->page_width-inner.right;dy=f->page_height-inner.bottom;}
  else{Store(f->scratch,&inner,16);Send(f->tab,0x1328,0,f->scratch);Read(f->scratch,&inner,16);
   dx=f->page_width-(inner.right-inner.left);dy=f->page_height-(inner.bottom-inner.top);if(dx<0)dx=0;if(dy<0)dy=0;
   User::GetWindowRect(f->tab,&tab);Gdi::POINT top{tab.left,tab.top};User::ScreenToClient(hwnd,&top);User::MoveWindow(f->tab,top.x,top.y,tab.right-tab.left+dx,tab.bottom-tab.top+dy);}
  // COMCTL32 finishes the frame here, inside the sheet's own
  // WM_INITDIALOG, with a single SetWindowPos that both grows and moves it. The
  // sheet does not centre itself: it takes the owner's window origin and, while
  // the owner is visible, adds SM_CXSMICON/SM_CYSMICON. Without an owner the original asks the window manager for a
  // default corner through a throwaway "Static" window created at CW_USEDEFAULT
  // and keeps its own corner when that window cannot be created.
  Gdi::RECT outer{};User::GetWindowRect(hwnd,&outer);
  int width=outer.right-outer.left+dx,height=outer.bottom-outer.top+dy,x=outer.left,y=outer.top;
  unsigned sheet_owner=Read32(s+12);
  if(sheet_owner&&User::IsWindow(sheet_owner)){Gdi::RECT o{};
   if(User::GetWindowRect(sheet_owner,&o)){x=o.left;y=o.top;if(User::IsVisible(sheet_owner)){x+=int(SystemMetric(49));y+=int(SystemMetric(50));}}
  }else{unsigned klass=String("Static"),probe_args[]={0,klass,0,0,0x80000000u,0x80000000u,0,0,0,0,image.Base(),0};
   unsigned probe=CreateWindow(probe_args);Free(klass);
   if(probe){Gdi::RECT d{};if(User::GetWindowRect(probe,&d)){x=d.left;y=d.top;}Api("DestroyWindow",&probe);}}
  const unsigned frame_position[]={hwnd,0,unsigned(x),unsigned(y),unsigned(width),unsigned(height),0x14};
  PositionApi("SetWindowPos",frame_position);
  if(wizard){
   // the page rectangle is the largest page at the tab's client
   // origin, kept at +50h..+5Ch for every activation.
   Gdi::POINT origin{0,0};User::ScreenToClient(f->tab,&origin);Gdi::POINT corner{-origin.x,-origin.y};User::ScreenToClient(hwnd,&corner);
   Write32(s+0x50,corner.x);Write32(s+0x54,corner.y);Write32(s+0x58,f->page_width);Write32(s+0x5c,f->page_height);
   auto place=[&](unsigned id,Gdi::RECT&r)->unsigned{unsigned h=User::GetDlgItem(hwnd,id);if(!h||!User::GetWindowRect(h,&r))return 0;
    Gdi::POINT a{r.left,r.top},b{r.right,r.bottom};User::ScreenToClient(hwnd,&a);User::ScreenToClient(hwnd,&b);r={a.x,a.y,b.x,b.y};return h;};
   auto size=[&](unsigned h,int left,int top,int w,int hgt,unsigned flags){const unsigned p[]={h,0,unsigned(left),unsigned(top),unsigned(w),unsigned(hgt),flags};PositionApi("SetWindowPos",p);};
   // the separator line follows the frame.
   Gdi::RECT r{};if(unsigned line=place(0x3026,r))size(line,r.left,r.top+dy,r.right+dx-r.left,r.bottom-r.top,0x14);
   e.Enable(User::GetDlgItem(hwnd,0x3023),true);Write32(s+0x44,0x3024);
   // moves the buttons of up to a given one; Windows 98 takes
   // one out and closes the gap from the left by the width it leaves.
   static const unsigned order[]={0x3023,0x3024,0x3025,2,9};
   auto shift=[&](unsigned stop,int mx,int my){for(unsigned id:order){Gdi::RECT b{};if(unsigned h=place(id,b))size(h,b.left+mx,b.top+my,0,0,0x15);if(id==stop)break;}};
   auto remove=[&](unsigned id){unsigned previous=0;for(unsigned o:order){if(o==id)break;previous=o;}Gdi::RECT a{},b{};int mx=0;
    if(previous&&place(id,a)&&place(previous,b))mx=a.right-b.right;shift(id,mx,0);if(unsigned h=User::GetDlgItem(hwnd,id))User::ShowWindow(h,0);};
   shift(9,dx,dy);if(!(Read32(s+8)&0x200))remove(9);
   // without PSH_WIZARDHASFINISH, Finish shares the place of Next.
   if(!(Read32(s+8)&0x10)){remove(0x3025);Gdi::RECT n{};if(place(0x3024,n))if(unsigned finish=User::GetDlgItem(hwnd,0x3025))size(finish,n.left,n.top,n.right-n.left,n.bottom-n.top,0x14);}
  }
  const unsigned ids[]={1,2,0x3021,9};unsigned visible[4]{},nvisible=0;
  if(!wizard)for(unsigned id:ids){unsigned button=User::GetDlgItem(hwnd,id);bool show=(id!=9||(Read32(s+8)&0x200))&&(id!=0x3021||!(Read32(s+8)&0x80));if(button){User::ShowWindow(button,show?5:0);if(show)visible[nvisible++]=button;}}
  // Button metrics and order come directly from the loaded original template.
  if(nvisible&&!wizard){Gdi::RECT b{};User::GetWindowRect(visible[0],&b);int bw=b.right-b.left,bh=b.bottom-b.top;Gdi::POINT p{b.left,b.top};User::ScreenToClient(hwnd,&p);int gap=d->xunit*4/4,right=client.right+dx-d->xunit*4/4;
   for(unsigned n=nvisible;n;){unsigned b=visible[--n];right-=bw;User::MoveWindow(b,right,p.y+dy,bw,bh);right-=gap;}}
  Write32(s+0x48,0);f->initializing=false;Send(f->tab,0x130c,w->value[3],0);
 }
 e.Phase(w,3);answer=e.Activate(1);
 activated:
 if(!answer&&e.Alive()){fail("PropertySheet could not activate an initial page");return true;}
 for(;w->value[5]<Read32(s+0x1c)&&e.Alive();++w->value[5])if((Read32(Read32(Read32(s+0x24)+4*w->value[5])+4)&0x400)&&!e.PageWindow(w->value[5])){e.Phase(w,4);e.CreatePageWindow(w->value[5]);
 eager_done:
 ;}
  unsigned first=User::NextDialogItem(Read32(s+0x38),0,false);SetFocus(first?first:f->tab);User::InvalidateRect(hwnd,nullptr,true);return true;
}
unsigned Application::PropertySheetProcedure(unsigned hwnd,unsigned message,unsigned wp,unsigned lp){
 unsigned tail=TakeTail();bool recorded=(tail==TailRecord||tail==TailResult)&&PropertySheetRecordable(hwnd,message,wp,lp);
 auto*f=FindSheet(this,hwnd,message==0x110?lp:0);PropertySheetEnvironment e{*this,f,recorded};
 auto*w=e.Begin(HostPropertyProcedure);PropertySheetEnvironment::WorkScope scope{e,w};w->value[0]=hwnd;w->value[1]=message;w->value[4]=wp;w->value[5]=lp;e.Phase(w,1);
 unsigned result=0;if(!PropertySheetMessage(hwnd,message,wp,lp,result))return 0;
 if(message==0x110||!User::IsWindow(hwnd))return 0;
 User::WindowLong(hwnd,0,result,true);return 1;
}
bool Application::PropertySheetMessage(unsigned hwnd,unsigned message,unsigned wp,unsigned lp,unsigned&result){
 SheetFrame*f=FindSheet(this,hwnd,message==0x110?lp:0);if(!f)return false;auto&t=threads[current_thread];
 bool recorded=PropertyWork::head&&PropertyWork::head->app==this&&PropertyWork::head->thread==current_thread&&
  PropertyWork::head->kind==HostPropertyProcedure&&PropertyWork::head->kept;
 PropertySheetEnvironment e{*this,f,recorded};unsigned s=f->state;result=0;
 auto fail=[&](const char*why){last_error=50;Fail(why);};
 const bool wizard=(Read32(s+8)&0x1402020)!=0;
 if(message==0x110&&f->initializing){
  auto*w=e.Begin(HostPropertyInit);if(!w){last_error=50;return true;}w->value[6]=hwnd;return ContinuePropertyInit(w,false)!=0;
 }
 if(message==0x82){e.Release();return true;}
 if(f->released)return false;
 if(message==0x4e&&lp){unsigned code=Read32(lp+8);if(Read32(lp)!=f->tab)return false;
  if(code==0xfffffdd8){if(recorded)PropertyWork::head->value[3]=1;result=Win98PropertySheet::KillActive(e,s);return true;}
  if(code==0xfffffdd9){if(recorded){PropertyWork::head->value[2]=1;e.Activate(1);}else Win98PropertySheet::Activate(e,s,1);result=1;return true;}
 }
 if(message==0x111){unsigned id=wp&65535,control=lp;
  if(lp&&User::Parent(lp)!=hwnd)return false;
  // without a control window (PSM_PRESSBUTTON) the button is looked up.
  if(!control)control=User::GetDlgItem(hwnd,id);
  // OK and Apply do nothing in a wizard.
  if(wizard&&(id==1||id==0x3021)){result=1;return true;}
  // Back and Next remember the button as the default (+44h).
  if(id==0x3023||id==0x3024){Write32(s+0x44,id);e.WizardStep(id==0x3024);result=1;return true;}
  // Finish. The frame is disabled while the page answers
  // PSN_WIZFINISH; zero ends the wizard with 1, anything else keeps it and
  // may name the window to focus.
  if(id==0x3025){e.Enable(hwnd,false);unsigned answer=Win98PropertySheet::Notify(e,s,Read32(s+0x40),~207u,0);result=1;if(!e.Alive())return true;
   if(!answer){Write32(s+0x38,0);Write32(s+0x48,1);f->closing=true;EndDialog(hwnd,1);return true;}
   e.Enable(hwnd,true);if(User::IsWindow(answer)&&e.Within(answer))SetFocus(answer);return true;}
  if(id==1||id==2||id==8||id==0x3021){
   if(recorded){PropertyWork::head->value[2]=1;result=e.Command(id);return true;}
   if((id==2||id==8)&&(Win98PropertySheet::Notify(e,s,Read32(s+0x40),~208u,0)||!e.Alive())){result=1;return true;}
   bool ok=Win98PropertySheet::Apply(e,s,id);if(!e.Alive()){result=1;return true;}
   if(id==1&&ok&&!Read32(s+0x38))for(unsigned i=Read32(s+0x1c);i&&e.Alive();)if(e.PageWindow(--i))Win98PropertySheet::Notify(e,s,i,~210u,1);
   if(e.Alive()&&!Read32(s+0x38)){f->closing=true;EndDialog(hwnd,Read32(s+0x48));}result=1;return true;
  }
  // Help only while its button is enabled.
  if(id==9){if(recorded){auto*w=PropertyWork::head;w->value[2]=1;e.Phase(w,2);}if(!control||User::IsEnabled(control))Win98PropertySheet::Notify(e,s,Read32(s+0x40),~204u,0);result=1;return true;}
 }
 if(message==0x468||message==0x46d){
  for(unsigned i=0;i<Read32(s+0x1c);++i)if(e.PageWindow(i)==wp){e.SetChanged(i,message==0x468);if(message==0x468){unsigned b=User::GetDlgItem(hwnd,0x3021);if(b)User::EnableWindow(b,true);}break;}
  if(message==0x46d){bool changed=false;unsigned p=e.Allocate(32);if(p){Write32(p,8);for(unsigned i=0;i<Read32(s+0x1c);++i){Send(f->tab,0x133c,i,p);changed|=(Read32(p+28)&1)!=0;}e.Free(p);}unsigned b=User::GetDlgItem(hwnd,0x3021);if(b)User::EnableWindow(b,changed);}return true;
 }
 // PSM_ADDPAGE, COMCTL32 AddPropSheetPage: the page of the handle goes behind
 // the last one and gets its tab; its window is made when it is first shown.
 // NETCPL's first page adds the pages of the network components this way.
 // The frame keeps its size.
 if(message==0x467){if(recorded)PropertyWork::head->value[3]=1;result=e.AddPage(lp);return true;}
 if(message==0x466){e.RemoveRequest(wp,lp);return true;}
 // PSM_SETWIZBUTTONS, PSM_PRESSBUTTON (: the command handler called
 // directly with the button of table ), PSM_SETFINISHTEXTA/W,
 // PSM_RESTARTWINDOWS and PSM_REBOOTSYSTEM (bits of +4Ch).
 if(message==0x470){e.SetWizardButtons(lp);return true;}
 if(message==0x471){static const unsigned buttons[]={0x3023,0x3024,0x3025,1,0x3021,2,9};unsigned ignored=0;
  if(wp<=6)PropertySheetMessage(hwnd,0x111,buttons[wp],0,ignored);return true;}
 if(message==0x473||message==0x479){e.SetFinishText(lp,message==0x479);return true;}
 if(message==0x46f||message==0x478){e.SetTitle(wp,lp,message==0x478);return true;}
 if(message==0x469||message==0x46a){Write32(s+0x4c,Read32(s+0x4c)|(message==0x469?2:3));return true;}
 if(message==0x474){result=f->tab;return true;}if(message==0x476){result=Read32(s+0x38);return true;}
 if(message==0x465||message==0x472){unsigned index=wp;
  if(message==0x472)index=Win98PropertySheet::NextPage(e,s,0,lp,0);
  else if(lp){index=~0u;for(unsigned i=0;i<Read32(s+0x1c);++i)if(Read32(Read32(Read32(s+0x24)+4*i)-24)==lp){index=i;break;}}
  if(index<Read32(s+0x1c)){if(recorded)PropertyWork::head->value[3]=1;result=e.SetCurrent(index,1);}return true;
 }
 if(message==0x46c){if(recorded){PropertyWork::head->value[3]=1;result=e.QuerySiblings(wp,lp);}else for(unsigned i=0;i<Read32(s+0x1c)&&e.Alive();++i)if(unsigned page=e.PageWindow(i)){result=Send(page,0x46c,wp,lp);if(result)break;}return true;}
 if(message==0x46e){if(recorded){PropertyWork::head->value[3]=1;result=e.Apply(0x3021);}else result=Win98PropertySheet::Apply(e,s,0x3021);return true;}
 // PSM_CANCELTOCLOSE: what the page did cannot be taken back (SYSDM after
 // "Remove"). Cancel is disabled and OK becomes "Close", the string
 // 1040h of COMCTL32.
 if(message==0x46b){if(unsigned cancel=User::GetDlgItem(hwnd,2))User::EnableWindow(cancel,false);
  if(unsigned okay=User::GetDlgItem(hwnd,1)){User::SetWindowText(okay,"Close");User::InvalidateRect(okay,nullptr,true);}return true;}
 if(message>=0x465&&message<=0x488){char text[80];snprintf(text,sizeof text,"PropertySheet message %03x requires an unported controller path",message);fail(text);return true;}
 return false;
}
} // namespace X86

#include "x86_serialui.inc"
