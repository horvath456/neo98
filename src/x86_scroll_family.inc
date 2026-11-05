// SPDX-License-Identifier: GPL-2.0-or-later
// USER32 wrappers // retain DWORD range and position;
// the original Win16 exports with the same names truncate those arguments.
unsigned Application::ScrollApi(ApiName name,const unsigned*a){
 unsigned bar=a[1]&65535;
 struct Wrapper {
  Application&app;
  bool ValidWindow(unsigned h){return User::IsWindow(h);}
  unsigned InfoCall(bool get,unsigned h,unsigned b,Win98Scroll::Info&i,unsigned redraw){
   unsigned p=app.Allocate(sizeof i);if(!p)return 0;app.Store(p,&i,sizeof i);
   unsigned args[]{h,b,p,redraw};unsigned result=app.ScrollApi(get?"GetScrollInfo":"SetScrollInfo",args);
   if(get&&app.state!=Failed)app.Read(p,&i,sizeof i);app.Free(p);return result;
  }
  unsigned SetInfo(unsigned h,unsigned b,Win98Scroll::Info&i,unsigned r){return InfoCall(false,h,b,i,r);}
  unsigned GetInfo(unsigned h,unsigned b,Win98Scroll::Info&i){return InfoCall(true,h,b,i,0);}
 }wrapper{*this};
 if(API_IS(name,"GetScrollRange")){
  unsigned low=0,high=0,result=Win98Scroll::GetRange(wrapper,a[0],a[1],low,high);
  Store(a[2],&low,4);Store(a[3],&high,4);return result;
 }
 if(API_IS(name,"SetScrollPos"))return Win98Scroll::SetPosition(wrapper,a[0],a[1],a[2],a[3]);
 if(API_IS(name,"SetScrollRange"))return Win98Scroll::SetRange(wrapper,a[0],a[1],a[2],a[3],a[4]);
 if(bar>3||!User::IsWindow(a[0]))return 0;
 auto state=[&](unsigned operation,Win98Scroll::Info *info,unsigned value){
  bool layout=false;
  const unsigned result=User::ScrollState(a[0],operation,bar,info,value,&layout);
  // USER relayouts through SetWindowPos(0037h). Defer the
  // geometry update until WM_NCCALCSIZE has seen the old client rectangle.
  if(layout){unsigned position[1][7]={{a[0],0,0,0,0,0,0x37}};PositionWindows(1,position);}
  return result;
 };
 switch (name.id)
 {
 API_CASE("GetScrollInfo") API_CASE("SetScrollInfo"){
  unsigned probe[]{a[2],28};if(PointerProbeApi("IsBadReadPtr",probe))return 0;
  if(API_IS(name,"GetScrollInfo")&&PointerProbeApi("IsBadWritePtr",probe))return 0;
  Win98Scroll::Info i;if(!Read(a[2],&i,sizeof i)||!Win98Scroll::Valid(i))return 0;
  bool get=API_IS(name,"GetScrollInfo");
  if(bar==2){unsigned result=CallWindow(a[0],get?0xea:0xe9,get?0:a[3]&65535,a[2]);return get?1:result;}
  unsigned result=state(get?0:1,&i,get?0:a[3]);
  if(get)Store(a[2],&i,sizeof i);return result;
 }
 API_CASE("GetScrollPos"){
  if(bar==2)return CallWindow(a[0],0xe1);
  Win98Scroll::Info i{28,4,0,0,0,0,0};User::ScrollState(a[0],0,bar,&i,0);return i.position;
 }
 API_CASE("ShowScrollBar"){
  if(bar==2){ShowWindow(a[0],(a[2]&65535)?1:0);return 1;}
  return state(2,nullptr,a[2]);
 }
 API_CASE("EnableScrollBar"){
  unsigned flags=a[2]&65535;if(flags&~3u)return 0;
  if(bar==2){
   // USER also changes WS_DISABLED when both arrows become
   // disabled, or when a fully disabled control is enabled again.
   const unsigned old=User::ScrollState(a[0],4,(User::Style(a[0])&1)?1:0,nullptr,0);
   if(old==flags)return 0;
   const bool disable=flags&&((old|flags)==3),enable=!flags&&old==3;
   if(disable||enable){unsigned args[]={a[0],enable?1u:0u};Api("EnableWindow",args);return 1;}
   return CallWindow(a[0],0xe4,flags,0);
  }
  return User::ScrollState(a[0],3,bar,nullptr,flags);
 }
 default:
     break;
 }
 return 0;
}

// USER's built-in SCROLLBAR control, USER. The SBM_* family keeps its
// state in the window's own bar record (bar 1 for SBS_VERT, bar 0 otherwise);
// SetScrollInfo and friends with SB_CTL arrive here as messages, as USER32
// sends them. Painting and tracking use the same bodies the nonclient bars do.
unsigned Application::ScrollBarMessage(unsigned hwnd,unsigned message,unsigned wp,unsigned lp){
 const unsigned style=User::Style(hwnd);
 const unsigned bar=(style&1)?1:0;
 const bool size_box=(style&0x18)!=0;
 auto get=[&](Win98Scroll::Info&i){i={28,0x17,0,0,0,0,0};User::ScrollState(hwnd,0,bar,&i,0);};
 switch(message){
  case 0x83:return DefaultWindowMessage(hwnd,message,wp,lp); // USER ->.
  case 0x81:return 1;
  case 1:{
   // USER. SBS_*ALIGN pins the control to one edge of its creation
   // rectangle and gives it exactly one scrollbar metric across.
   if(style&6){
    Gdi::RECT r{};if(!User::GetWindowRect(hwnd,&r))return 0;
    const int cx=(int)SystemMetric(2),cy=(int)SystemMetric(3);
    if(size_box){if(style&4){r.left=r.right-cx;r.top=r.bottom-cy;}r.right=r.left+cx;r.bottom=r.top+cy;}
    else if(bar){if(style&2)r.right=r.left+cx;else r.left=r.right-cx;}
    else {if(style&2)r.bottom=r.top+cy;else r.top=r.bottom-cy;}
    Gdi::POINT at{r.left,r.top};
    if(unsigned parent=User::Parent(hwnd))User::ScreenToClient(parent,&at);
    unsigned position[1][7]={{hwnd,0,(unsigned)at.x,(unsigned)at.y,(unsigned)(r.right-r.left),(unsigned)(r.bottom-r.top),0x14}};
    PositionWindows(1,position);
   }
   // USER. A control created disabled starts with both arrows off.
   if(style&0x8000000)User::ScrollState(hwnd,3,bar,nullptr,3);
   return 0;
  }
  // USER answers DLGC_WANTARROWS so the dialog manager leaves the
  // caret keys to the control.
  case 0x87:return 1;
  // USER. Only SBS_SIZEGRIP claims the sizing corner.
  case 0x84:return (style&0x10)?17u:User::DefaultMessage(hwnd,message,wp,lp);
  // USER turns WM_ENABLE into SBM_ENABLE_ARROWS.
  case 0xa:return ScrollBarMessage(hwnd,0xe4,wp?0u:3u,0);
  case 0x201:{
   if(size_box)return 0;
   if(style&0x40000)SetFocus(hwnd);
   Gdi::POINT origin{0,0};
   if(!User::ScreenToClient(hwnd,&origin))return 0;
   User::BeginScrollTrack(hwnd,bar,(short)(lp&0xffff)-origin.x,(short)(lp>>16)-origin.y);
   return 0;
  }
  // USER. The caret keys scroll; the key release closes the sequence.
  case 0x100:case 0x101:{
   if(size_box||wp<0x21||wp>0x28)return User::DefaultMessage(hwnd,message,wp,lp);
   static const unsigned codes[8]={2,3,7,6,0,0,1,1}; // PRIOR NEXT END HOME LEFT UP RIGHT DOWN
   const unsigned code=message==0x100?codes[wp-0x21]:8u; // SB_ENDSCROLL on key up
   if(unsigned parent=User::Parent(hwnd))Send(parent,bar?0x115u:0x114u,code,hwnd);
   return 0;
  }
  case 0xe0:{Win98Scroll::Info i;get(i);const unsigned old=i.position;i.mask=4;i.position=wp;User::ScrollState(hwnd,1,bar,&i,lp&65535);return old;}
  case 0xe1:{Win98Scroll::Info i;get(i);return i.position;}
  case 0xe2:case 0xe6:{Win98Scroll::Info i;get(i);const unsigned old=i.position;i.mask=1;i.minimum=wp;i.maximum=lp;User::ScrollState(hwnd,1,bar,&i,message==0xe6);return old;}
  case 0xe3:{Win98Scroll::Info i;get(i);if(wp)Store(wp,&i.minimum,4);if(lp)Store(lp,&i.maximum,4);return 0;}
  // USER. The flags accumulate with OR until a zero clears them; the
  // control keeps its own arrow state, separate from the nonclient pair.
  case 0xe4:{
   const unsigned flags=wp&65535;if(flags&~3u)return 0;
   return User::ScrollState(hwnd,3,bar,nullptr,flags);
  }
  case 0xe9:{Win98Scroll::Info i;if(!Read(lp,&i,sizeof i)||!Win98Scroll::Valid(i))return 0;return User::ScrollState(hwnd,1,bar,&i,wp&65535);}
  case 0xea:{Win98Scroll::Info requested;if(!Read(lp,&requested,sizeof requested)||!Win98Scroll::Valid(requested))return 0;Win98Scroll::Info i;get(i);i.size=requested.size;i.mask=requested.mask;Store(lp,&i,sizeof i);return 1;}
  default:return User::DefaultMessage(hwnd,message,wp,lp);
 }
}
