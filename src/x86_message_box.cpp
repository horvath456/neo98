// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include <stdio.h>
#include "user_settings.h"
#include "win98_message_box.h"
#include "win98_message_box_layout.h"
#include <string.h>
namespace X86 {
namespace {
// A frame is a block of its own, in a list a saved state carries: the box of
// a loaded machine finds its frame by its window (FinishMessageBox). It
// lives as long as the call of BuildMessageBox; application destruction
// resumes suspended fibers with Failed state, unwinding RunModal and with it
// these registrations.
struct MessageBoxFrame {
 Application*app;MessageBoxFrame*next;unsigned hwnd=0,text=0,scratch=0,font=0,icon=0,thread=0;
 Win98MessageBox::Parameters parameters;Win98MessageBoxLayout::Result layout;
 // What is left to do when the box is gone: the windows of the task that a
 // task-modal box disabled, and the focus before it.
 unsigned previous_focus=0,disabled_count=0,disabled[User::WindowCapacity]{};
 static MessageBoxFrame*head;
 explicit MessageBoxFrame(Application*a):app(a),next(head),thread(a->ThreadId()){head=this;}
 ~MessageBoxFrame(){auto**at=&head;while(*at&&*at!=this)at=&(*at)->next;if(*at)*at=next;}
};
MessageBoxFrame*MessageBoxFrame::head=nullptr;
MessageBoxFrame*BoxFrame(Application*a,unsigned hwnd){for(auto*f=MessageBoxFrame::head;f;f=f->next)if(f->app==a&&f->hwnd==hwnd&&hwnd)return f;return nullptr;}
// Verified USER NE strings:84,85,89,90,87,86,88,78,111. Index matches
// USER's native string slots, including Error for the width helper.
const char*const BoxLabels[]={"OK","Cancel","&Yes","&No","&Retry","&Abort","&Ignore","Error","&Help"};
Gdi::RECT Rectangle(const Win98MessageBoxLayout::Rect&r){return {r.left,r.top,r.right,r.bottom};}
}
unsigned Application::MessageBoxApi(ApiName name,const unsigned*a){
 struct E {
  Application&app;
  struct Scratch {E&e;unsigned address;Scratch(E&v,unsigned size):e(v),address(v.app.Allocate(size)){if(address){unsigned char zero[336]{};e.app.Store(address,zero,size);}}~Scratch(){if(address)e.app.Free(address);}};
  unsigned Read(unsigned p){return app.Read32(p);}
  unsigned Allocate(unsigned n){return app.Allocate(n);}
  void Free(unsigned p){if(p)app.Free(p);}
  unsigned LoadString(unsigned module,unsigned id,unsigned out,unsigned capacity){unsigned a[]={module,id,out,capacity};return app.Api("LoadStringA",a);}
  unsigned Convert(unsigned text,unsigned out,unsigned capacity){unsigned a[]={0,0,text,~0u,out,capacity,0,0};return app.NlsApi(811,a);}
  unsigned Show(Win98MessageBox::Parameters p){return app.ShowMessageBox(p);}
 }e{*this};
 if(log&&API_IS(name,"MessageBoxA")){char line[400];snprintf(line,sizeof line,"MessageBox caption=%s text=%s",a[2]?ReadString(a[2]):"",a[1]?ReadString(a[1]):"");log(line);}
 if(API_IS(name,"MessageBoxIndirectW")){last_error=120;return 0;}
 if(API_IS(name,"MessageBoxIndirectA")){
  // MSGBOXPARAMSA: +8 hInstance, +0xc text, +0x10 caption; resource ids have no text here.
  if(log&&a[0]&&virtual_memory.Access(a[0],20,false)){const unsigned text=Read32(a[0]+12),caption=Read32(a[0]+16);char line[400];snprintf(line,sizeof line,"MessageBox caption=%s text=%s",caption>>16?ReadString(caption):"",text>>16?ReadString(text):"");log(line);}
  return Win98MessageBox::Indirect(e,a[0]);}
 bool extended=API_IS(name,"MessageBoxExA")||API_IS(name,"MessageBoxExW");
 if(API_IS(name,"MessageBoxW")||API_IS(name,"MessageBoxExW"))return Win98MessageBox::Wide(e,a[0],a[1],a[2],a[3],extended?a[4]:0);
 Win98MessageBox::Parameters p;p.owner=a[0];p.text=a[1];p.caption=a[2];p.style=a[3];p.language=extended?a[4]&65535:0;return ShowMessageBox(p);
}
unsigned Application::MessageBox(unsigned owner,const char*text,const char*title,unsigned flags){
 unsigned body=String(text?text:""),caption=title?String(title):0;
 Win98MessageBox::Parameters p;p.owner=owner;p.text=body;p.caption=caption;p.style=flags;
 unsigned result=ShowMessageBox(p);if(body)Free(body);if(caption)Free(caption);return result;
}
unsigned Application::ShowMessageBox(const Win98MessageBox::Parameters&p){
 struct E {
  Application&app;unsigned caption=0;
  ~E(){if(caption)app.Free(caption);}
  unsigned DefaultCaption(){return caption=app.String(BoxLabels[7]);}
  bool DesktopReady(){return app.user_initialized;}
  unsigned Dialog(const Win98MessageBox::Parameters&p,const Win98MessageBox::Buttons&b){return app.BuildMessageBox(p,b);}
  unsigned HardError(const Win98MessageBox::Parameters&,const Win98MessageBox::Buttons&){app.last_error=50;return 0;}
 }e{*this};return Win98MessageBox::Run(e,p);
}
unsigned Application::BuildMessageBox(const Win98MessageBox::Parameters&p,const Win98MessageBox::Buttons&buttons){
 // Only the box of a gate of its own has a record: one the host opens on
 // its way through another call returns into that call's work.
 const bool gate=TakeFresh();
 if(state==Failed||state==Exited)return 0;
 if(p.style&0x100000){last_error=50;return 0;} // Native bidi text provider is not installed.
 if((p.style&15)>7||(p.owner&&!User::IsWindow(p.owner))){last_error=87;return 0;}
 // The desktop as owner gives a top-level box without one, as for DialogBox
 // (Win98IndirectDialog::ModalOwner): disabling the desktop would leave no
 // window the mouse can reach, the box included.
 if(p.owner&&p.owner==User::GetDesktopWindow()){Win98MessageBox::Parameters top=p;top.owner=0;return ShowMessageBox(top);}
 // The owner may belong to another process: a program that has no window of
 // its own yet hands MessageBox what GetActiveWindow or GetTopWindow(0) gave
 // it (WLOADER.EXE started from the shell). USER shows the box over
 // that window and disables it for the duration like any other owner.
 if(log&&p.owner&&User::WindowProcess(p.owner)&&User::WindowProcess(p.owner)!=process_id){char klass[48]{},line[128];User::GetClassName(p.owner,klass,sizeof klass);snprintf(line,sizeof line,"MessageBox owner=%u class=%s of process %u",p.owner,klass,User::WindowProcess(p.owner));log(line);}
 Dialog*dialog=dialogs.Take();
 if(!dialog){last_error=8;return 0;}
 *dialog=Dialog{};
 struct Holder{MessageBoxFrame*f;~Holder(){delete f;}}holder{new MessageBoxFrame(this)};
 MessageBoxFrame&frame=*holder.f;frame.parameters=p;
 frame.scratch=Allocate(128);frame.text=String(ReadString(p.text));
 if(!frame.scratch||!frame.text){if(frame.scratch)Free(frame.scratch);if(frame.text)Free(frame.text);last_error=8;return 0;}
 struct Cleanup {Application&a;MessageBoxFrame&f;bool armed;~Cleanup(){if(!armed)return;if(f.font){unsigned v=f.font;a.Api("DeleteObject",&v);}a.Free(f.text);a.Free(f.scratch);}}cleanup{*this,frame,true};
 char title[256];const char*caption=ReadString(p.caption);unsigned title_length=strlen(caption);if(title_length>255)title_length=255;memcpy(title,caption,title_length);title[title_length]=0;
 auto&settings=User::SystemSettings();
 auto font=[&](unsigned selector){unsigned char source[50]{},target[60]{};if(!settings.Font(selector,source))return 0u;Win98Parameters::ExpandFont(source,target);Store(frame.scratch,target,sizeof target);return Api("CreateFontIndirectA",&frame.scratch);};
 frame.font=font(0x506);if(!frame.font)return 0;
 unsigned dc=User::GetDC(0);if(!dc){last_error=8;return 0;}
 auto old=Gdi::SelectObject(dc,frame.font);
 Gdi::SIZE alphabet{};Gdi::TEXTMETRICA metrics{};
 bool measured=Gdi::GetTextExtentPoint32A(dc,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",52,&alphabet)&&Gdi::GetTextMetricsA(dc,&metrics);
 unsigned caption_font=font(0x502);Gdi::SIZE caption_extent{};
 if(caption_font){Gdi::SelectObject(dc,caption_font);measured=measured&&Gdi::GetTextExtentPoint32A(dc,title,title_length,&caption_extent);Gdi::SelectObject(dc,frame.font);Api("DeleteObject",&caption_font);}else measured=false;
 struct LayoutProvider {
  Application&app;MessageBoxFrame&frame;unsigned dc;
  int LabelLength(int index){return strlen(BoxLabels[index]);}
  int ZeroWidth(){Gdi::SIZE size{};return Gdi::GetTextExtentPoint32A(dc,"0",1,&size)?size.cx:0;}
  int LabelWidth(int index,int){
   // USER measures the mnemonic-stripped label. The common DrawText
   // provider implements that prefix handling and the actual selected FNT.
   unsigned text=app.String(BoxLabels[index]);Gdi::RECT r={0,0,32767,32767};app.Store(frame.scratch,&r,sizeof r);
   unsigned a[]={dc,text,~0u,frame.scratch,0x420};app.Api("DrawTextA",a);app.Read(frame.scratch,&r,sizeof r);app.Free(text);return r.right-r.left;
  }
  Win98MessageBoxLayout::Measure MeasureText(int width,int height){Gdi::RECT r={0,0,width,height};app.Store(frame.scratch,&r,sizeof r);unsigned a[]={dc,frame.text,~0u,frame.scratch,0x2e50};unsigned result=app.Api("DrawTextA",a);app.Read(frame.scratch,&r,sizeof r);return {r.right-r.left,int(result)};}
 }provider{*this,frame,dc};
 Win98MessageBoxLayout::Input in;in.style=p.style;in.dialog_base_x=(alphabet.cx/26+1)/2;in.dialog_base_y=metrics.tmHeight;in.caption_text_width=caption_extent.cx;
 bool button_valid=true;in.button_width=Win98MessageBoxLayout::ButtonWidth(provider,in.dialog_base_x,&button_valid);
 unsigned icon_id=Win98MessageBox::Icon(p.style);
 if(icon_id){unsigned a[]={p.style&0x80?p.instance:0,p.style&0x80?p.icon:icon_id};frame.icon=ImageApi("LoadIconA",a);if(auto*icon=FindIcon(frame.icon)){in.has_icon=true;in.icon_width=icon->data->width;in.icon_height=icon->data->height;}else measured=false;}
 in.monitor_rect={0,0,int(SystemMetric(0)),int(SystemMetric(1))};in.work_rect={int(settings.Word(0x11ba)),int(settings.Word(0x11bc)),int(settings.Word(0x11be)),int(settings.Word(0x11c0))};
 in.frame_x=settings.Word(0x2ec);in.frame_y=settings.Word(0x2ee);in.caption_height=settings.Word(0x2e6);in.caption_button_x=settings.Word(0x31a);in.caption_button_y=settings.Word(0x31c);in.edge_x=settings.Word(0x338);in.edge_y=settings.Word(0x33a);
 for(auto*f=MessageBoxFrame::head;f;f=f->next)if(f!=&frame&&f->hwnd)++in.cascade_depth;
 if(measured&&button_valid&&in.dialog_base_x&&in.dialog_base_y)frame.layout=Win98MessageBoxLayout::Calculate(provider,in);
 Gdi::SelectObject(dc,old);User::ReleaseDC(0,dc);
 if(!frame.layout.valid||state==Failed||state==Exited){if(state!=Failed)last_error=50;return 0;}
 const auto&r=frame.layout.absolute_outer;
 unsigned klass=String("#32770"),caption_string=String(title);
 unsigned style=(p.style&0x3000)==0x1000?0x80c80347:0x80c801c5;
 unsigned args[]={0x101,klass,caption_string,style,unsigned(r.left),unsigned(r.top),unsigned(r.right-r.left),unsigned(r.bottom-r.top),p.owner,0,p.instance,0};
 unsigned previous_focus=User::GetFocus();frame.previous_focus=previous_focus;frame.hwnd=CreateWindow(args);Free(klass);Free(caption_string);if(!frame.hwnd)return 0;
 *dialog=Dialog{};dialog->hwnd=frame.hwnd;dialog->owner=p.owner;dialog->font=frame.font;dialog->previous_focus=previous_focus;dialog->xunit=in.dialog_base_x;dialog->yunit=in.dialog_base_y;
 unsigned first=0;bool controls=true;
 for(unsigned i=0;i<buttons.count;++i){
  unsigned id=buttons.ids[i];const auto&b=frame.layout.buttons[i];unsigned type=String("BUTTON"),label=String(BoxLabels[buttons.labels[i]-1]);
  unsigned a[]={0,type,label,0x50010000u|(i==0?0x20000u:0u)|(i==buttons.selected?1u:0u),unsigned(b.left),unsigned(b.top),unsigned(b.right-b.left),unsigned(b.bottom-b.top),frame.hwnd,id,p.instance,0};
  unsigned button=CreateWindow(a);Free(type);Free(label);if(!button){controls=false;break;}Send(button,0x30,frame.font,0);if(i==buttons.selected){first=button;dialog->default_id=id;}
 }
 if(!controls||state==Failed||state==Exited){EndDialog(frame.hwnd,0);*dialog=Dialog{};return 0;}
 // USER runs here, in the box's own WM_INITDIALOG, after the dialog
 // manager created the template's controls: an MB_OK box looks its IDOK button
 // up and writes IDCANCEL into the control id. The button is therefore created
 // with the template id and renumbered afterwards, as in the original, so the
 // creation trace and any window enumeration before this point see IDOK.
 if(unsigned lookup=Win98MessageBox::Lookup(p.style))
  if(unsigned okay=User::GetDlgItem(frame.hwnd,lookup)){
   unsigned renumbered=Win98MessageBox::ActualButton(p.style,lookup);
   User::SetControlID(okay,renumbered);
   if(dialog->default_id==lookup)dialog->default_id=renumbered;
  }
 if((p.style&0x40000)||(p.style&0x3000)==0x1000){unsigned a[]={frame.hwnd,~0u,0,0,0,0,0x13};Api("SetWindowPos",a);}
 unsigned*disabled=frame.disabled;unsigned&disabled_count=frame.disabled_count;
 if((p.style&0x2000)&&!p.owner){unsigned windows[User::WindowCapacity],count=0;if(User::WindowSnapshot(0,windows,User::WindowCapacity,count))for(unsigned i=0;i<count;++i)if(windows[i]!=frame.hwnd&&User::WindowThread(windows[i])==ThreadId()&&User::IsEnabled(windows[i])){User::EnableWindow(windows[i],false);disabled[disabled_count++]=windows[i];}}
 if(first)SetFocus(first);User::InvalidateRect(frame.hwnd,nullptr,true);
 // The rest is FinishMessageBox, which a saved state enters as well; the
 // frame stays until then, so the holder lets go of it here.
 const bool kept=gate&&PushHostFrame(HostMessageBox,frame.hwnd);
 const unsigned result=RunModal(frame.hwnd,kept);
 holder.f=nullptr;cleanup.armed=false;
 return FinishMessageBox(&frame,result);
}
// After the loop of a message box, also of one that came out of a saved
// state (frame null: found by the record of the thread). The callers of
// BuildMessageBox free the texts they made for it when it returns; a box
// taken up from a state returns to the program directly, and those few
// bytes of the program's heap stay.
unsigned Application::FinishMessageBox(void*pointer,unsigned result){
 auto&t=threads[current_thread];
 MessageBoxFrame*f=static_cast<MessageBoxFrame*>(pointer);
 if(t.host_frame_count&&t.host_frames[t.host_frame_count-1].kind==HostMessageBox){
  if(!f)f=BoxFrame(this,t.host_frames[t.host_frame_count-1].a);
  PopHostFrame();
 }
 if(!f)return 0;
 // The frame and what it holds of the program's heap end here, for a box
 // that ran through as for one taken up from a state; the loop has
 // deleted the font with the dialog.
 struct Holder{Application&a;MessageBoxFrame*f;~Holder(){a.Free(f->text);a.Free(f->scratch);delete f;}}holder{*this,f};
 f->font=0;
 for(unsigned i=0;i<f->disabled_count;++i)if(User::IsWindow(f->disabled[i]))User::EnableWindow(f->disabled[i],true);
 // RunModal cannot focus a task-disabled window. Restore it after reenabling
 // the task's windows, once the normal focus provider can accept the handle.
 if(f->disabled_count&&User::IsWindow(f->previous_focus)&&User::IsEnabled(f->previous_focus))SetFocus(f->previous_focus);
 return Win98MessageBox::Result(f->parameters.style,result);
}
void Application::DiscardSavedMessageBoxes(){
 for(auto*f=MessageBoxFrame::head,*next=f;f;f=next){
  next=f->next;if(f->app!=this)continue;
  bool live=false;
  for(unsigned i=0;i<ThreadCapacity;++i){const auto&t=threads[i];
   if(i!=current_thread&&t.id==f->thread&&t.fiber&&t.fiber->Started()&&!t.fiber->Finished()){live=true;break;}}
  if(live)continue;
  for(unsigned i=0;i<f->disabled_count;++i)if(User::IsWindow(f->disabled[i]))User::EnableWindow(f->disabled[i],true);
  Free(f->text);Free(f->scratch);delete f;
 }
}
bool Application::MessageBoxMessage(unsigned hwnd,unsigned message,unsigned wp,unsigned lp,unsigned&result){
 auto*f=BoxFrame(this,hwnd);if(!f)return false;result=0;
 if(message==0x111){
  const auto action=Win98MessageBox::Command(wp,User::GetDlgItem(hwnd,wp&65535)!=0);
  if(action==Win98MessageBox::End){EndDialog(hwnd,wp&65535);result=1;return true;}
  if(action==Win98MessageBox::Ignore)return true;
  message=0x53;lp=0;
 }
 if(message==0x53){
  // USER and USER always identify help control9 and the box HWND.
  const bool installed=(f->parameters.style&0x4000)!=0;
  unsigned info[]={28,1,9,hwnd,installed?f->parameters.context:0,unsigned(mouse_x),unsigned(mouse_y)};
  unsigned memory=Allocate(sizeof info);if(!memory)return true;Store(memory,info,sizeof info);
  if(installed&&f->parameters.callback)Call(f->parameters.callback,&memory,1);
  else if(f->parameters.owner&&User::IsWindow(f->parameters.owner))Send(f->parameters.owner,0x53,0,memory);
  Free(memory);return true;
 }
 if(message==0x14){Gdi::RECT r{};if(User::GetClientRect(hwnd,&r)){unsigned color=15;result=Gdi::FillRect(wp,&r,Api("GetSysColorBrush",&color));}return true;}
 if(message==0xf){
  User::Paint paint{};unsigned dc=User::BeginPaint(hwnd,paint);if(!dc)return true;
  Gdi::SaveDC(dc);Gdi::SelectObject(dc,f->font);Gdi::SetBkMode(dc,Gdi::TRANSPARENT);unsigned color=18;Gdi::SetTextColor(dc,Api("GetSysColor",&color));
  Gdi::RECT client{};User::GetClientRect(hwnd,&client);color=15;Gdi::FillRect(dc,&client,Api("GetSysColorBrush",&color));
  auto text=Rectangle(f->layout.text);Store(f->scratch,&text,sizeof text);unsigned a[]={dc,f->text,~0u,f->scratch,0x2a50u|((f->parameters.style&0x80000)?2u:0u)};Api("DrawTextA",a);
  if(f->icon){const auto&r=f->layout.icon;unsigned a[]={dc,unsigned(r.left),unsigned(r.top),f->icon,unsigned(r.right-r.left),unsigned(r.bottom-r.top),0,0,3};ImageApi("DrawIconEx",a);}
  Gdi::RestoreDC(dc,-1);User::DrawingChanged(dc);User::EndPaint(hwnd,paint);return true;
 }
 return false;
}
bool Application::MessageBoxWindow(unsigned hwnd){return BoxFrame(this,hwnd)!=nullptr;}
} // namespace X86
