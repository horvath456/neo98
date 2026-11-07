// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "win98_combobox.h"
#include "win98_combo_mouse.h"
#include "user_draw.h"
#include <string.h>
namespace X86 {
struct Application::ComboBox {
 Application&app;Win98ComboBox::State state;unsigned hwnd=0,list=0,edit=0,dc=0,active=0,list_scroll_style=0,shown_height=0;bool building=false,layout=false,syncing=false,closing=false;Win98ComboMouse::ListState tracking;
 explicit ComboBox(Application&a):app(a){}
 ~ComboBox(){if(dc)Gdi::DeleteDC(dc);}
 unsigned Parent(){return User::Parent(hwnd);}
 void Invalidate(){if(hwnd)User::InvalidateRect(hwnd,nullptr,true);}
 unsigned Notify(unsigned code){return hwnd?app.CallWindow(Parent(),0x111,User::ControlID(hwnd)|(code<<16),hwnd):0;}
 unsigned List(unsigned message,unsigned wp,unsigned lp){
  // DestroyWindow takes owned pop-ups down before the owner's WM_DESTROY, so
  // a dialog's cleanup can reach its combo boxes after their drop-down lists
  // are gone. USER forwards to the list handle regardless, and SendMessage
  // to a destroyed window answers 0: COMDLG32's Save As cleanup counts items
  // with CB_GETCOUNT and spun 4 billion times on CB_ERR.
  if(!list||!User::IsWindow(list))return list?0:~0u;
  if(message>=0x1aa&&message<=0x1ad){app.last_error=50;return ~0u;}
  return app.CallWindow(list,message,wp,lp);
 }
 unsigned Edit(unsigned message,unsigned wp,unsigned lp){return edit&&User::IsWindow(edit)?app.Send(edit,message,wp,lp):0;}
 unsigned SelectedText(unsigned capacity,unsigned output,bool length){
  const unsigned index=List(0x188,0,0),size=index==~0u?0:List(0x18a,index,0);
  if(size==~0u||size>=8192)return 0;if(length)return size;
  if(!capacity||!output)return 0;
  const unsigned buffer=app.Allocate(size+1);if(!buffer)return 0;
  if(size)List(0x189,index,buffer);const char zero=0;app.Store(buffer+size,&zero,1);
  const unsigned copied=app.PutString(output,app.ReadString(buffer),capacity);app.Free(buffer);return copied;
 }
 unsigned ListKey(unsigned message,unsigned wp,unsigned lp){
  // Only an open dropdown suppresses the selection-end/close path.
  // Simple and closed combos send CBN_SELENDOK before CBN_SELCHANGE.
  const unsigned old=state.flags&0x400;if((state.flags&0x82)==0x82)state.flags|=0x400;
  const unsigned result=List(message,wp,lp);
  state.flags=(state.flags&~0x400u)|old;if(Alive())Sync(false);return result;
 }
 void Sync(bool paint=true){
  if(!hwnd||syncing)return;syncing=true;unsigned index=List(0x188,0,0),text=0;
  if(index!=~0u){unsigned n=List(0x18a,index,0);if(n!=~0u&&n<8192){text=app.Allocate(n+1);if(text)List(0x189,index,text);}}
  // USER: with the focus the new text of the edit field is
  // selected, so typing replaces it.
  if(hwnd){const char*value=text?app.ReadString(text):"";if(!(state.flags&0x10)&&(User::Style(hwnd)&0x200)&&edit)User::SetWindowText(edit,value);if(!(state.flags&0x10)&&edit&&(state.flags&4))Edit(0xb1,0,~0u);User::SetWindowText(hwnd,value);Invalidate();}
  if(text)app.Free(text);
  // USER draws a visible selection field synchronously,
  // including its parent's color callback after CBN_SELCHANGE.
  if(paint&&Alive()&&(state.flags&0x10)&&User::IsVisible(hwnd)){
   const unsigned window=hwnd;Gdi::RECT r{};User::GetClientRect(window,&r);
   if(const auto target=User::GetDC(window)){app.DrawComboBox(window,target,r);User::DrawingChanged(target);User::ReleaseDC(window,target);}
  }
  syncing=false;
 }
 void Layout(bool size_simple=false){
  if(!hwnd||layout||building)return;layout=true;
  Gdi::RECT r{};User::GetWindowRect(hwnd,&r);Gdi::POINT pos{r.left,r.top};unsigned parent=Parent();if(parent)User::ScreenToClient(parent,&pos);
  const unsigned frame=state.height+6,drop=state.flags&2?frame:unsigned(r.bottom-r.top);
  User::MoveWindow(hwnd,pos.x,pos.y,state.width,drop);
  if(edit)User::MoveWindow(edit,3,3,state.width>unsigned((state.flags&2)?22:6)?state.width-((state.flags&2)?22:6):0,state.height);
  if(list){if(state.flags&2){User::GetWindowRect(hwnd,&r);const bool positioned=size_simple||(state.flags&0x80);User::MoveWindow(list,positioned?r.left:0,(positioned?r.top:0)+frame,state.width>state.drop_width?state.width:state.drop_width,(state.flags&0x80)&&shown_height?shown_height:state.list_height);}else User::MoveWindow(list,0,frame,state.width,state.list_height);}
  // These composite children are created by USER, without a guest WNDPROC.
  // Finish their WM_SIZE work through the native list provider as well.
  if(list)List(5,0,0);
  if(size_simple&&!(state.flags&2)&&list){Gdi::RECT lr{};User::GetWindowRect(list,&lr);state.list_height=lr.bottom-lr.top;User::MoveWindow(hwnd,pos.x,pos.y,state.width,frame+state.list_height);}
  // USER marks the persistent list of CBS_SIMPLE open too.
  if(!(state.flags&2)&&list)state.flags|=0x80;
  Invalidate();layout=false;
 }
 unsigned SelectionHeight(unsigned h){if(h>255){app.last_error=87;return ~0u;}state.height=h&65535;Layout(true);return 0;}
 bool Alive(){return hwnd&&User::IsWindow(hwnd);}
 bool Header04(){return Alive();} // Supported modern control compatibility gate; not WS_VISIBLE.
 bool Win40(){return true;} // Native controls use the already supported Win40 path.
 void HideList(){if(list&&User::IsWindow(list))User::ShowWindow(list,0);}
 void InvalidateSelection(){Invalidate();}
 void Position(unsigned){if(Alive())Layout();}
 void Update(){if(Alive()){User::UpdateControl(hwnd);User::Render();}} // the original's UpdateWindow
 void SyncEdit(){if(Alive()&&edit)Sync();}
 void SelectionChanged(){if(Alive())Sync();}
 void Pressed(bool on){if(on)state.flags|=0x40;else state.flags&=~0x40u;Invalidate();}
 void ReleaseCapture(){unsigned capture=User::ThreadCapture(User::WindowThread(hwnd));if(capture==hwnd||capture==list)User::CaptureOperation(9,hwnd);}
 void MatchEdit(){
  if(!edit||!list)return;const unsigned n=User::TextLength(edit);if(n==~0u)return;
  char*text=new char[n+1];if(!text)return;
  User::GetWindowText(edit,text,n+1);const unsigned p=app.Allocate(n+1);
  if(p){app.Store(p,text,n+1);unsigned found=List(0x1a2,~0u,p);if(found!=~0u)List(0x186,found,0);app.Free(p);}delete[]text;
 }
 int Selection(){return int(List(0x188,0,0));}
 int Caret(){return int(List(0x19f,0,0));}
 int First(){return int(List(0x18e,0,0));}
 int Page(){Gdi::RECT r{};User::GetClientRect(list,&r);unsigned h=List(0x1a1,0,0);return h&&h!=~0u?r.bottom/int(h):0;}
 bool IsCombo(){return true;}
 void FinishRange(){app.Fail("Native ComboBox list is not a multiple-selection list");}
 void PaintSelection(bool){if(list)User::InvalidateRect(list,nullptr,false);}
 void RestoreSelection(int index){List(0x186,unsigned(index),0);}
 bool AccessibilityEnabled(){return false;}
 void AccessibilitySelection(){app.Fail("ComboBox accessibility event provider is unavailable");}
 void KillScrollTimer(){if(list){unsigned a[]={list,0xfffe};app.Api("KillTimer",a);}}
 void EnsureCaret(){unsigned caret=List(0x19f,0,0);if(caret!=~0u)List(0x19e,caret,0);}
 void ListNotify(unsigned code){if(Alive())Win98ComboMouse::ListCommand(*this,state.flags,code);}
 void EndList(bool accept){if(tracking.flags&0x400)Win98ComboMouse::EndTracking(*this,tracking,accept?0x11:3);}
 void BeginList(bool pressed,unsigned point){
  if(!Alive()||!list||!(state.flags&0x80))return;
  tracking.saved=Selection();tracking.flags=0x500|(pressed?0x200:0);
  User::CaptureOperation(1,list);if(pressed)ListMouse(0x200,1,point);
 }
 bool Close(bool accept,bool notify=true){
  if(closing)return Alive();closing=true;
  const bool alive=Win98ComboMouse::Close(*this,state.flags,notify,accept);
  closing=false;return alive&&Alive();
 }
 void Drop(bool show){
  if(!(state.flags&2)||!list)return;
  if(show){if(state.flags&0x80)return;Notify(7);if(!Alive())return;state.flags|=0x80;
   // USER: the selection (the first item without one)
   // becomes the top of the list; LB_SETTOPINDEX cuts it to the last
   // page, so every item that fits stays in view.
   {const unsigned selection=List(0x188,0,0);List(0x197,selection==~0u?0:selection,0);}
   if(!Alive())return;shown_height=DropHeight();Layout();User::ShowWindow(list,5);if(!(state.flags&0x20))BeginList(false,0);}
  else if(state.flags&0x80)Close(false);
  Invalidate();
 }
 // USER: the open list is as high as its items plus
 // SM_CYEDGE, never higher than the drop height of the template; without
 // CBS_NOINTEGRALHEIGHT it is cut to whole items. An empty list keeps the
 // drop height.
 unsigned DropHeight(){
  unsigned height=state.list_height;const unsigned count=List(0x18b,0,0),item=List(0x1a1,0,0);
  if(!item||item==~0u)return height;unsigned index=46;const unsigned edge=app.Api("GetSystemMetrics",&index);
  if(count&&count!=~0u){const unsigned long long needed=(unsigned long long)count*item+edge;if(needed<0x7fff&&needed<height)height=unsigned(needed);}
  if(!(User::Style(hwnd)&0x400)&&height>edge)height=edge+(height-edge)/item*item;
  return height;
 }
 int Hit(int x,int y){
  if(!list)return -1;Gdi::RECT client{};User::GetClientRect(list,&client);
  if(x<0||y<0||x>=client.right||y>=client.bottom)return -1;
  unsigned count=List(0x18b,0,0),p=app.Allocate(16);if(!p)return -1;int hit=-1;
  if(count!=~0u)for(unsigned i=0;i<count;++i){Gdi::RECT r{};if(List(0x198,i,p)==~0u||!app.Read(p,&r,16))break;if(x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom){hit=int(i);break;}}
  app.Free(p);return hit;
 }
 void ListMouse(unsigned message,unsigned,unsigned lp){
  if(closing||!(state.flags&0x80))return;
  if(message==0x215){if(tracking.flags&0x400)Win98ComboMouse::EndTracking(*this,tracking,4);return;}
  if(message==0x1f){Close(false);return;}
  if(!(tracking.flags&0x400))return;
  const int hit=Hit(short(lp),short(lp>>16));
  if(message==0x201||message==0x203){
   if(hit<0){
    // USER: capture also delivers presses on the list's
    // nonclient area. Temporarily leave list tracking, release capture,
    // forward WM_NCLBUTTONDOWN, then resume list capture. Only a press
    // outside the entire popup closes it. In particular its scrollbar
    // must run the ordinary modal tracker, including release painting.
    Gdi::POINT origin{};Gdi::RECT window{},client{};
    if(User::ScreenToClient(list,&origin)&&User::GetWindowRect(list,&window)&&User::GetClientRect(list,&client)){
     const int x=short(lp)-origin.x,y=short(lp>>16)-origin.y;
     const bool nonclient=short(lp)<client.left||short(lp)>=client.right||short(lp>>16)<client.top||short(lp>>16)>=client.bottom;
     if(nonclient&&x>=window.left&&x<window.right&&y>=window.top&&y<window.bottom){
      tracking.flags&=~0x400u;ReleaseCapture();
      if(Alive()&&User::IsWindow(list))app.SendFromNative(list,0xa1,User::FrameHitTest(list,x,y),(unsigned(x)&65535)|(unsigned(y)<<16));
      if(Alive()&&User::IsWindow(list)&&(state.flags&0x80)){User::CaptureOperation(1,list);tracking.flags|=0x400;}
      return;
     }
    }
    Close(false);return;
   }
   tracking.saved=Selection();tracking.flags=(tracking.flags&~0x800u)|0x200|(message==0x203?0x800:0);
   List(0x186,unsigned(hit),0);return;
  }
  if(message==0x200){if(hit>=0)List(0x186,unsigned(hit),0);return;}
  if(message==0x202&&(tracking.flags&0x200)){
   if(hit>=0)List(0x186,unsigned(hit),0);
   Win98ComboMouse::EndTracking(*this,tracking,hit>=0?13:7);
  }
 }
 unsigned Unsupported(unsigned message,unsigned,unsigned){app.last_error=50;app.Fail("Unsupported ComboBox message");return 0;}
};
Application::ComboBox*Application::FindComboBox(unsigned hwnd,bool create){
 for(auto*b:combo_boxes)if(b&&b->hwnd==hwnd&&hwnd)return b;
 if(create)for(auto*&b:combo_boxes)if(!b||(!b->hwnd&&!b->active)){delete b;b=new ComboBox(*this);if(b)b->hwnd=hwnd;return b;}return nullptr;
}
bool Application::ComboListMouse(unsigned hwnd,unsigned message,unsigned wp,unsigned lp){
 if(message!=0x200&&message!=0x201&&message!=0x202&&message!=0x203&&message!=0x215&&message!=0x1f)return false;
 for(auto*b:combo_boxes)if(b&&b->hwnd&&b->list==hwnd&&(b->state.flags&2)){
  struct Hold{ComboBox&b;Hold(ComboBox&x):b(x){++b.active;}~Hold(){--b.active;}}hold(*b);
  if(!(b->state.flags&0x80)&&!b->closing)return false;
  b->ListMouse(message,wp,lp);return true;
 }
 return false;
}
void Application::FreeComboBoxes(){for(auto*&b:combo_boxes){delete b;b=nullptr;}}
// The combo box that owns a ComboLBox, or 0. The drop-down list is a popup
// without a parent; its owner-draw messages still go to the combo box,
// which converts CtlType/ID before the application's parent sees them.
unsigned Application::ComboOfList(unsigned list){
 for(auto*b:combo_boxes)if(b&&b->hwnd&&b->list==list)return b->hwnd;
 return 0;
}
bool Application::ComboListNotify(unsigned list,unsigned code){
 for(auto*b:combo_boxes)if(b&&b->hwnd&&b->list==list){
  struct Hold{ComboBox&b;Hold(ComboBox&x):b(x){++b.active;}~Hold(){--b.active;}}hold(*b);
  CallWindow(b->hwnd,0x111,1000|(code<<16),list);return true;
 }
 return false;
}
bool Application::ComboEditKey(unsigned edit,unsigned message,unsigned wp,unsigned lp){
 // The combo's single-line edit intercepts vertical navigation; Home/End
 // still belong to the text caret. The edit answers 1.
 if(message!=0x100||(wp!=0x26&&wp!=0x28&&wp!=0x21&&wp!=0x22))return false;
 auto*b=FindComboBox(User::Parent(edit));if(!b||b->edit!=edit)return false;
 struct Hold{ComboBox&b;Hold(ComboBox&x):b(x){++b.active;}~Hold(){--b.active;}}hold(*b);
 b->ListKey(message,wp,lp);return true;
}
unsigned Application::ComboBoxMessage(unsigned hwnd,unsigned message,unsigned wp,unsigned lp){
 auto*b=FindComboBox(hwnd,message==0x81);if(!b)return message==0x81?0:~0u;
 struct Hold{ComboBox&b;Hold(ComboBox&x):b(x){++b.active;}~Hold(){--b.active;}}hold(*b);
 auto&s=b->state;
 if(message==0x81){unsigned initial=User::Style(hwnd),style=Win98ComboBox::WindowStyle(initial);b->list_scroll_style=Win98ComboBox::ListScrollStyle(initial);User::SetStyle(hwnd,style);User::SetExtendedStyle(hwnd,Win98ComboBox::WindowExtendedStyle(User::ExtendedStyle(hwnd)));s.flags=Win98ComboBox::Flags(style,0x4000);b->dc=Gdi::CreateCompatibleDC(0);if(!b->dc||!Gdi::SelectObject(b->dc,system))return 0;s.font=system;return 1;}
 if(message==1){
  if(b->list)return 0;b->building=true;Gdi::RECT r{};User::GetClientRect(hwnd,&r);s.width=r.right-r.left;Gdi::TEXTMETRICA metrics{};Gdi::GetTextMetricsA(b->dc,&metrics);s.height=metrics.tmHeight+2;
  unsigned style=User::Style(hwnd),instance=0;User::WindowLong(hwnd,-6,instance,false);
  if(s.flags&0x300){unsigned values[]={3,User::ControlID(hwnd),~0u,0,s.height,0},p=Allocate(sizeof values);if(p){Store(p,values,sizeof values);CallWindow(b->Parent(),0x2c,values[1],p);Read(p,values,sizeof values);Free(p);if(values[4]<=255)s.height=values[4];}}
  if(!b->hwnd)return ~0u;s.list_height=r.bottom>int(s.height+6)?r.bottom-s.height-6:0;
  unsigned ls=Win98ComboBox::ListStyle(style|b->list_scroll_style,s.flags);if(s.flags&2)ls=(ls&~0x50000000u)|0x80000000;
  // The list of a combo box is a window of the class ComboLBox (USER registers
  // it beside ListBox with the same procedure and CS_SAVEBITS): the open list
  // saves what it covers and puts it back when it closes, so nothing below it
  // has to paint.
  b->list=User::CreateWindow("ComboLBox","",ls,0,s.height+6,s.width,s.list_height,hwnd,1000,0,ThreadId(),instance);
  if(b->list&&!(s.flags&2))User::SetExtendedStyle(b->list,User::ExtendedStyle(b->list)|0x200);
  if(!b->list||!CallWindow(b->list,0x81,0,0)){b->building=false;return ~0u;}CallWindow(b->list,1,0,0);
  if(!b->hwnd)return ~0u;
  if(!(s.flags&0x10)){b->edit=User::CreateWindow("EDIT","",Win98ComboBox::EditStyle(style,s.flags),3,3,s.width>((s.flags&2)?22u:6u)?s.width-((s.flags&2)?22u:6u):0,s.height,hwnd,1001,0,ThreadId(),instance);if(!b->edit){b->building=false;return ~0u;}CallWindow(b->edit,0x81,0,0);CallWindow(b->edit,1,0,0);}
  b->building=false;b->Layout();return 0;
 }
 if(message==2){unsigned list=b->list;b->list=0;if(list&&User::IsWindow(list))Api("DestroyWindow",&list);return 0;}
 if(message==0x82){b->hwnd=0;b->edit=b->list=0;if(b->dc){Gdi::DeleteDC(b->dc);b->dc=0;}return 0;}
 if(message>=0x140&&message<=0x164){
  if(message==0x152){if(!lp)return 0;Gdi::RECT r{};User::GetWindowRect(hwnd,&r);r.right=r.left+int(s.width>s.drop_width?s.width:s.drop_width);r.bottom=r.top+int(s.height+6+s.list_height);return Store(lp,&r,sizeof r)?1:0;}
  return Win98ComboBox::Message(*b,s,message,wp,lp);
 }
 switch(message){
 // The combobox keeps its own no-redraw flag for WM_SETREDRAW; WS_VISIBLE stays.
 case 0xb:User::SetControlRedraw(hwnd,wp!=0);return 0;
 case 5:{if(!b->layout&&!b->building){Gdi::RECT r{};User::GetClientRect(hwnd,&r);s.width=r.right-r.left;b->Layout();}return 0;}
 // USER: a combobox with an edit field hands the focus on; the edit
 // reports EN_SETFOCUS, and that runs the focus helper, which selects
 // the whole text of the edit (EM_SETSEL 0,-1), marks the focus and sends
 // CBN_SETFOCUS. Without an edit field the helper runs at once.
 case 7:if(b->edit){unsigned h=b->edit;Api("SetFocus",&h);}else if(!(s.flags&4)){s.flags|=4;b->Invalidate();b->Notify(3);}return 0;
 case 8:{
  // USER closes only when the next focus is not a child.
  // The native list is an owned popup so it can extend outside the combo;
  // recognize that same list subtree as part of this composite control.
  for(unsigned next=wp;next;next=User::Parent(next))if(next==b->list||User::Parent(next)==hwnd)return 0;
  b->Drop(false);if(s.flags&4)b->Invalidate();s.flags&=~4u;b->Notify(4);return 0;
 }
 case 0x30:if(!Gdi::SelectObject(b->dc,wp?wp:system))return 0;s.font=wp?wp:system;b->Edit(0x30,wp,0);b->List(0x30,wp,0);if(!(s.flags&0x300)){Gdi::TEXTMETRICA metrics{};Gdi::GetTextMetricsA(b->dc,&metrics);s.height=metrics.tmHeight+2;}b->Layout();if(lp)b->Invalidate();return 0;
 case 0x31:return s.font;
 case 0xc:{
  // Programmatic text changes do not send CBN_EDITUPDATE/CBN_EDITCHANGE.
  // In particular, initializing a dialog must not queue a face change that
  // later overwrites the font style supplied by its initialization hook.
  const bool syncing=b->syncing;b->syncing=true;
  unsigned result=b->edit?b->Edit(message,wp,lp):User::SetWindowText(hwnd,ReadString(lp));
  b->syncing=syncing;
  if(b->edit){char text[8192];User::GetWindowText(b->edit,text,sizeof text);User::SetWindowText(hwnd,text);}
  b->Invalidate();return result;
 }
 case 0xd:return b->edit?b->Edit(message,wp,lp):b->SelectedText(wp,lp,false);
 case 0xe:return b->edit?b->Edit(message,wp,lp):b->SelectedText(0,0,true);
 case 0x87:{unsigned message=0;if(lp&&!Read(lp+4,&message,sizeof message))return 0;return Win98ComboBox::DialogCode(s.flags,wp,lp!=0,message);}
 case 0xf:User::ValidateRect(hwnd,nullptr);return 0;
 case 0x14:return 1;
 case 0x133:case 0x134:case 0x138:
  return SendFromNative(b->Parent(),message,wp,lp);
 case 0x2b:case 0x2c:case 0x2d:case 0x39:{
  // Existing LISTBOX owner callbacks reach the ComboBox first. Original USER
  // converts the control type/ID/handle before notifying the application's parent.
  unsigned size=message==0x2b?48:message==0x2c?24:message==0x2d?20:32,values[12]{};if(!Read(lp,values,size))return 0;
  values[0]=3;values[1]=User::ControlID(hwnd);if(message==0x2b)values[5]=hwnd;else if(message==0x2d)values[3]=hwnd;else if(message==0x39)values[2]=hwnd;
  Store(lp,values,size);return CallWindow(b->Parent(),message,values[1],lp);
 }
 case 0x111:{unsigned code=wp>>16;if(lp==b->list){if(code==1||code==2||code==3||code==65534){Win98ComboMouse::ListCommand(*b,s.flags,code);}else if(code==4)b->Notify(3);else if(code==5)b->Notify(4);}else if(lp==b->edit&&!b->syncing){if(code==0x300||code==0x400){char text[8192];User::GetWindowText(b->edit,text,sizeof text);User::SetWindowText(hwnd,text);b->Notify(code==0x300?5:6);}else if(code==0x100){if(!(s.flags&4)){b->Edit(0xb1,0,~0u);s.flags|=4;b->Notify(3);}}else if(code==0x200){s.flags&=~4u;b->Notify(4);}}return 0;}
 case 0x201:case 0x203:
  if(s.flags&2){if(s.flags&0x80)b->Close(true);else{s.flags|=0x20;b->Pressed(true);b->Drop(true);if(b->Alive())User::CaptureOperation(1,hwnd);}}return 0;
 case 0x200:if((s.flags&0x20)&&(s.flags&0x80)){
  Gdi::POINT origin{},point{short(lp),short(lp>>16)},list_origin{};User::ScreenToClient(hwnd,&origin);User::ScreenToClient(b->list,&list_origin);
  point.x+=list_origin.x-origin.x;point.y+=list_origin.y-origin.y;
  if(b->Hit(point.x,point.y)>=0){s.flags&=~0x20u;b->Pressed(false);b->ReleaseCapture();b->BeginList(true,unsigned(static_cast<unsigned short>(point.x))|(unsigned(static_cast<unsigned short>(point.y))<<16));}
 }return 0;
 case 0x202:Win98ComboMouse::ReleaseButton(*b,s.flags);return 0;
 case 0x215:if(s.flags&0x20){s.flags&=~0x20u;b->Pressed(false);b->Close(false);}return 0;
 case 0x1f:if(s.flags&0x80)b->Close(false);return 0;
 // USER and 05D4..05F0: an open list takes Enter and Escape;
 // every other key and character goes to the edit field of a combobox that
 // has one and to its list otherwise, and the answer is that control's.
 case 0x100:if(wp==0x73){b->Drop(!(s.flags&0x80));return 0;}if((wp==13||wp==27)&&(s.flags&0x80)){b->Close(wp==13);return 0;}if(b->edit)return b->Edit(message,wp,lp);[[fallthrough]];
 case 0x102:if(message==0x102&&b->edit)return b->Edit(message,wp,lp);return b->ListKey(message,wp,lp);
 // WM_MOUSEWHEEL, as USER handles it from Win98 on: an open list (and the
 // list of a simple combo box, which is always there) scrolls; a closed
 // combo box steps through its entries like the arrow keys, one for every
 // whole notch, so the small steps of a trackpad add up to it and three
 // lines of the fingers' way are one entry. With Shift or Ctrl the message
 // is the parent's.
 case 0x20a:{
  if(wp&0x000c)return DefaultWindowMessage(hwnd,message,wp,lp);
  User::NoteWheelActivity(hwnd);
  if((s.flags&0x80)||!(s.flags&2))return b->List(message,wp,lp);
  static unsigned window=0;static int rest=0;const int delta=(short)(wp>>16);
  if(window!=hwnd||(rest&&(rest<0)!=(delta<0)))rest=0;
  window=hwnd;rest+=delta;int notches=rest/120;rest-=notches*120;
  for(int i=0,n=notches<0?-notches:notches;i<n&&i<32&&User::IsWindow(hwnd);++i)CallWindow(hwnd,0x100,notches>0?0x26:0x28,0);
  return 0;
 }
 default:return DefaultWindowMessage(hwnd,message,wp,lp);
 }
}
void Application::DrawComboBox(unsigned hwnd,Gdi::HDC dc,const Gdi::RECT&bounds){
 auto*b=FindComboBox(hwnd);if(!b)return;struct Hold{ComboBox&b;Hold(ComboBox&v):b(v){++b.active;}~Hold(){--b.active;}}hold(*b);int saved=Gdi::SaveDC(dc);if(!saved)return;Gdi::IntersectClipRect(dc,bounds.left,bounds.top,bounds.right,bounds.bottom);Gdi::SelectObject(dc,b->state.font);
 unsigned window=5,text=8,face=15;auto fill=[&](Gdi::RECT r,unsigned color){unsigned brush=Gdi::CreateSolidBrush(Api("GetSysColor",&color));Gdi::FillRect(dc,&r,brush);Gdi::DeleteObject(brush);};fill(bounds,window);
 Gdi::RECT border=bounds;border.bottom=b->state.height+6;Win98Draw::DrawEdge(dc,&border,10,15);
 // USER paints the selection field of a combobox without an edit
 // (the item rectangle). With the focus and the list closed
 // it is filled with COLOR_HIGHLIGHT, the text is written in
 // COLOR_HIGHLIGHTTEXT one border inside
 // and DrawFocusRect frames the rectangle; an owner-drawn field
 // gets ODS_SELECTED|ODS_FOCUS instead. Previously, the field
 // never showed the focus.
 if(b->state.flags&0x10){
  const bool focused=(b->state.flags&4)&&!(b->state.flags&0x80);
  const Gdi::RECT item{3,3,b->state.width>19?int(b->state.width)-19:3,int(b->state.height)+3};
  Gdi::SetTextColor(dc,Api("GetSysColor",&text));Gdi::SetBkColor(dc,Api("GetSysColor",&window));
  const unsigned background=SendFromNative(b->Parent(),User::IsEnabled(hwnd)?0x133:0x138,dc,hwnd);
  if(!b->Alive()){Gdi::RestoreDC(dc,saved);return;}
  if(background)Gdi::FillRect(dc,&item,background);
  unsigned selection=SendFromNative(b->list,0x188,0,0);if(b->state.flags&0x300){unsigned data=selection==~0u?0:SendFromNative(b->list,0x199,selection,0);unsigned values[]={3,User::ControlID(hwnd),selection,1,Win98ComboBox::SelectionState(b->state.flags,User::IsEnabled(hwnd),b->Win40()),hwnd,dc,unsigned(item.left),unsigned(item.top),unsigned(item.right),unsigned(item.bottom),data};unsigned p=Allocate(sizeof values);if(p){Store(p,values,sizeof values);SendFromNative(b->Parent(),0x2b,values[1],p);Free(p);}}
  else{char label[8192];User::GetWindowText(hwnd,label,sizeof label);Gdi::SetBkMode(dc,Gdi::TRANSPARENT);unsigned highlight=13,highlight_text=14;if(focused){fill(item,highlight);Gdi::SetTextColor(dc,Api("GetSysColor",&highlight_text));Gdi::SetBkColor(dc,Api("GetSysColor",&highlight));}Gdi::TextOutA(dc,item.left+1,item.top+1,label,strlen(label));if(focused)Gdi::DrawFocusRect(dc,&item);}
 }
 if(!b->hwnd){Gdi::RestoreDC(dc,saved);return;}
 if(b->state.flags&2){Gdi::RECT button{int(b->state.width)-18,2,int(b->state.width)-2,int(b->state.height)+4};fill(button,face);Win98Draw::DrawEdge(dc,&button,(b->state.flags&0x40)?10:5,15);unsigned black=Gdi::CreateSolidBrush(0);int x=(button.left+button.right)/2-3,y=(button.top+button.bottom)/2-1;for(int row=0;row<4;++row){Gdi::RECT r{x+row,y+row,x+7-row,y+row+1};Gdi::FillRect(dc,&r,black);}Gdi::DeleteObject(black);}
 Gdi::RestoreDC(dc,saved);
}

// USER GetComboBoxInfo. Windows 98 takes the combo's own list box as
// well, so the search first asks whether this window is a combo and then
// whether it is the list of one. fills the item and button rectangles
// out of the control's own record; NEO98 keeps the same two rectangles in the
// layout DrawComboBox paints with. The rectangles are in client coordinates
// of the combo box, as in the original.
bool Application::ComboBoxView(unsigned hwnd, unsigned *out)
{
    ComboBox *b = FindComboBox(hwnd);
    if (!b)
        for (auto *candidate : combo_boxes)
            if (candidate && candidate->hwnd && candidate->list == hwnd) { b = candidate; break; }
    if (!b || !b->hwnd || !User::IsWindow(b->hwnd)) return false;
    const int width = int(b->state.width), height = int(b->state.height);
    const bool button = (b->state.flags & 2) != 0;
    // the text rectangle stops before the drop down button.
    out[0] = unsigned(3); out[1] = unsigned(3);
    out[2] = unsigned(button && width > 19 ? width - 19 : width - 3);
    out[3] = unsigned(height + 3);
    // draws the button in this cell; a simple combo has none.
    out[4] = unsigned(button ? width - 18 : 0); out[5] = unsigned(button ? 2 : 0);
    out[6] = unsigned(button ? width - 2 : 0); out[7] = unsigned(button ? height + 4 : 0);
    // an absent button is invisible, a held one is pressed.
    out[8] = (button ? 0u : 0x8000u) | ((b->state.flags & 0x40) ? 8u : 0u);
    out[9] = b->hwnd;
    // a combo without its own edit control answers zero.
    out[10] = b->edit && User::IsWindow(b->edit) ? b->edit : 0u;
    out[11] = b->list && User::IsWindow(b->list) ? b->list : 0u;
    return true;
}
} // namespace X86
