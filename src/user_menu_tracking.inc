// SPDX-License-Identifier: GPL-2.0-or-later
// Native window, drawing and queue providers for the implemented USER menu code.
namespace {
namespace MT=Win98MenuTracking;namespace MG=Win98MenuGeometry;
struct PopupView {bool used=false;MT::Session*session=nullptr;MT::Popup popup;MG::Window geometry;unsigned parent=0,bar_menu=0,bar_index=0;int width=0,height=0;unsigned char*pixels=nullptr;int pixel_width=0,pixel_height=0;unsigned painted_generation=0;bool painting=false;};
PopupView popup_views[16];MT::Session popup_sessions[8];unsigned popup_depth=0;
struct PopupInputNode {InputPacket packet;PopupInputNode*next=nullptr;};
struct PopupInputQueue {PopupInputNode*head=nullptr,*tail=nullptr;};
PopupInputQueue popup_inputs[8];
void ClearPopupInput(MT::Session*s){
 auto&q=popup_inputs[s-popup_sessions];while(q.head){auto*n=q.head;q.head=n->next;delete n;}q.tail=nullptr;
}

void*menu_context=nullptr;MenuSend menu_send=nullptr;MenuLoop menu_loop=nullptr;MenuThread menu_thread=nullptr;
// Menu mode of a menu bar, from WM_ENTERMENULOOP to WM_EXITMENULOOP. The popups the
// bar drops run as tracked popups inside it; the session keeps the bar item
// highlighted, moves between items and knows whether the keyboard drives it.
// index is the bar position, BarSystem the system menu, BarNone nothing.
constexpr int BarSystem=-1,BarNone=-2;
struct BarSession {
 bool active=false,bar_only=false,leave=false,done=false,again=false,open=false,keyboard=false;
 unsigned owner=0,bar=0,system=0,notified_menu=0,round=0,phase=0,track_flags=0,result=0;int index=BarNone,notified=BarNone-1;
};
BarSession bar_session;
// MessageBeep of the menu code, played by the caller
// on the program's side.
bool menu_beep=false;
PopupView*PopupForWindow(unsigned h){for(auto&v:popup_views)if(v.used&&v.popup.window==h)return &v;return nullptr;}
PopupView*PopupForMenu(unsigned h){for(auto&v:popup_views)if(v.used&&v.popup.menu==h)return &v;return nullptr;}
PopupView*PopupForCore(MT::Popup&p){for(auto&v:popup_views)if(v.used&&&v.popup==&p)return &v;return nullptr;}
MT::Session*PopupSession(){return popup_depth?&popup_sessions[popup_depth-1]:nullptr;}
void FinishPopupDispatch(MT::Environment&e,MT::Session&s){if(s.current&&!(s.current->auxiliary&2))MT::Close(e,s,0,0,0);s.flags&=~0x44u;}
PopupView*PopupTop(){auto*s=PopupSession();if(!s||!s->current)return nullptr;auto*v=PopupForCore(*s->current);for(unsigned depth=0;v&&depth<16;++depth){PopupView*child=nullptr;for(auto&entry:popup_views)if(entry.used&&entry.parent==v->popup.window){child=&entry;break;}if(!child)return v;v=child;}return v;}
namespace OD=Win98MenuOwnerDraw;
void*menu_owner_context=nullptr;MenuMeasure menu_measure=nullptr;MenuDrawItem menu_draw=nullptr;
// Native fonts, device contexts, bitmap rasters and the guest send path for the
// implemented USER measurement. The metrics that this desktop does not carry
// in the shared settings block are named native provider values, not captured
// original data.
struct MenuOwnerEnvironment final : OD::Environment {
 int offset_x=0,offset_y=0; // client origin of the popup on the screen
 Win98Menu::Menu*Find(unsigned h)override{return FindMenu(h);}
 OD::S BarHeight()override{return OD::S(settings.Word(0x34c));}
 OD::S ItemHeight()override{return 13;}
 OD::S TextPad()override{return 6;}
 OD::S EdgePad()override{return OD::S(settings.Word(0x6e8));}
 OD::S CheckWidth()override{return 13;}
 OD::S CheckHeight()override{return 13;}
 OD::S EdgeX()override{return OD::S(settings.Word(0x2e8));}
 OD::S BorderX()override{return OD::S(settings.Word(0x2ec));}
 OD::S BarPadX()override{return OD::S(settings.Word(0x338));}
 OD::S BarPadY()override{return OD::S(settings.Word(0x33a));}
 OD::S BarExtra()override{return 0;}
 OD::S SystemBitmapWidth()override{return 16;}
 OD::S MdiBitmapWidth(unsigned short)override{return 16;}
 void SelectMenuFont(bool)override{Gdi::SelectObject(screen,system_font);} // no bold menu face yet
 bool BitmapSize(unsigned short h,OD::Size&s)override{unsigned info[6]{};if(!Gdi::BitmapInfo(h,info))return false;s={OD::S(info[1]),OD::S(info[2])};return true;}
 static const char*Text(OD::P p){return reinterpret_cast<const char*>(p);}
 unsigned TabIndex(OD::P p)override{const char*s=Text(p);unsigned n=0;while(s&&s[n]&&s[n]!='\t')++n;return n;}
 OD::S TextWidth(OD::P p,unsigned offset,unsigned count)override{
  const char*s=Text(p);if(!s||!count)return 0;
  // Menu layout uses the same accelerator-prefix rules as its text painter.
  // '&N' occupies the width of N; '&&' occupies one ampersand.
  char *label=new char[count+1];
  const auto prefix=Win98DrawText::Strip(s+offset,count,label,count);
  Gdi::SIZE size{};Gdi::GetTextExtentPoint32A(screen,label,int(prefix.count),&size);
  delete[] label;return OD::S(size.cx);
 }
 bool BackspacePrefix(OD::P p)override{const char*s=Text(p);return s&&s[0]=='\b';}
 bool BarAlign(unsigned)override{return false;}
 bool ColumnGap(unsigned window)override{return ModernWindow(window);}
 void MeasureOwner(unsigned window,unsigned short id,unsigned data,OD::Size&s)override{
  if(!menu_measure)return;int cx=s.cx,cy=s.cy;menu_measure(CallbackContext(menu_owner_context,window),window,id,data,&cx,&cy);
  s={OD::S(cx),OD::S(cy)};
 }
 void DrawOwner(unsigned window,const OD::Draw&d)override{
  if(!menu_draw)return;
  // The original rectangle is client relative; the shared surface needs the popup origin.
  const unsigned fields[12]={d.type,d.control,d.item,d.action,d.state,d.window,d.dc,
   unsigned(d.rect.left+offset_x),unsigned(d.rect.top+offset_y),
   unsigned(d.rect.right+offset_x),unsigned(d.rect.bottom+offset_y),d.data};
  menu_draw(CallbackContext(menu_owner_context,window),window,fields);
 }
} menu_owner_environment;
#include "user_menu_owner_bar.inc"
void MeasurePopup(PopupView&v){auto*m=FindMenu(v.popup.menu);if(!m){v.width=v.height=0;return;}
 const int save=Gdi::SaveDC(screen);
 OD::MeasureMenu(menu_owner_environment,v.popup.menu,v.popup.owner,0,0,0);
 if(save)Gdi::RestoreDC(screen,save);
 // WM_MEASUREITEM ran in the guest; it may have destroyed the menu or added
 // items, which frees and reallocates the item array. Resolve the handle again.
 m=FindMenu(v.popup.menu);if(!m){v.width=v.height=0;return;}
 int width=m->width,height=m->height;if(width<8)width=8;

 if(width>780)width=780;if(height>580)height=580;
 v.width=width;v.height=height;
}
// A popup goes on top of the topmost windows, as the SetWindowPos with
// HWND_TOPMOST of Win98's menu code puts it: below the taskbar, which is
// topmost too, the menu's part over it was never in its visible region, its
// saved bits did not restore it, and it stayed on the taskbar when it closed.
void PlacePopup(PopupView&v,MT::Point p,int width,int height){v.geometry={{p.x,p.y,MG::S(p.x+width),MG::S(p.y+height)},MG::S(p.x+2),MG::S(p.y+2),true};auto*w=Find(v.popup.window);if(w){w->x=p.x;w->y=p.y;w->width=width;w->height=height;w->visible=true;w->style|=0x10000000;Layout(v.popup.window);OrderWindow(v.popup.window,~0u);}dirty=true;}
void ReleasePopupPixels(PopupView&v){delete[] v.pixels;v.pixels=nullptr;v.pixel_width=v.pixel_height=0;}
void RemovePopupView(PopupView&v){if(auto*m=FindMenu(v.popup.menu))if(v.popup.selected>=0&&unsigned(v.popup.selected)<m->count)m->items[v.popup.selected].state&=~0x80u;unsigned h=v.popup.window;ReleasePopupPixels(v);v.used=false;DestroyWindow(h);dirty=true;}
struct PopupGeometry : MG::Environment {
 MG::Window normal;
 Win98Menu::Menu*Menu(unsigned h)override{return FindMenu(h);}
 MG::Window*FindWindow(unsigned h)override{if(auto*v=PopupForWindow(h))return &v->geometry;auto*w=Find(h);if(!w)return nullptr;auto p=Origin(*w);normal={{MG::S(p.x),MG::S(p.y),MG::S(p.x+w->width),MG::S(p.y+w->height)},MG::S(p.x+ClientLeft(*w)),MG::S(p.y+ClientTop(*w)),false};return &normal;}
 unsigned PopupWindow(unsigned owner,unsigned menu)override{if(auto*m=User::FindMenu(menu)){if(m->flags&1){if(auto*v=PopupForMenu(menu))return v->popup.window;return 0;}}return owner;}
 void LayoutBar(unsigned owner,unsigned menu)override{auto*w=Find(owner);auto*m=User::FindMenu(menu);if(!w||!m)return;Layout(owner);for(unsigned n=0;n<w->menu_items&&n<m->count;++n){auto&r=w->menu_item[n];auto&i=m->items[n];i.x=r.left;i.y=r.top;i.width=r.right-r.left;i.height=r.bottom-r.top;}m->width=w->menu_bar.right-w->menu_bar.left;m->height=w->menu_bar.bottom-w->menu_bar.top;}
 MG::S BorderX()override{return 2;}MG::S BorderY()override{return 2;}
} popup_geometry;
struct PopupEnvironment : MT::Environment {
 unsigned creating_bar=0,creating_index=0;
 MT::W CurrentThread()override{return menu_thread?MT::W(menu_thread(CurrentContext(menu_context))):1;}MT::W WindowThread(unsigned h)override{return MT::W(User::WindowThread(h));}
 MT::Session*CurrentSession()override{return PopupSession();}bool IsWindow(unsigned h)override{return User::IsWindow(h);}bool IsMenu(unsigned h)override{return User::IsMenu(h);}
 MT::W MenuFlags(unsigned h)override{auto*m=FindMenu(h);return m?m->flags:0;}bool ButtonDown(MT::W button)override{return User::PhysicalKeyDown(button);}
 MT::Popup*CreatePopup(unsigned menu,unsigned owner,MT::Point at,bool owned)override{if(!FindMenu(menu)||!User::IsWindow(owner))return nullptr;FindMenu(menu)->flags|=1;for(auto&v:popup_views)if(!v.used){unsigned h=User::CreateWindow("#32768","",0x80000000,at.x,at.y,100,100,owned?owner:0,0,0,CurrentThread());if(!h)return nullptr;v=PopupView{};v.used=true;v.bar_menu=creating_bar;v.bar_index=creating_index;creating_bar=creating_index=0;v.popup.owner=owner;v.popup.menu=menu;v.popup.window=h;v.popup.signature=0x5550;v.geometry.popup_class=true;return &v.popup;}return nullptr;}
 MT::Session*BeginSession(MT::Popup&p,MT::W thread)override{if(popup_depth==8)return nullptr;auto&s=popup_sessions[popup_depth++];ClearPopupInput(&s);s={};s.current=&p;s.owner=p.owner;s.thread=thread;if(auto*v=PopupForCore(p))v->session=&s;return &s;}
 void FinishSession(MT::Session*s)override{if(s){ClearPopupInput(s);for(auto&v:popup_views)if(v.used&&v.session==s){CloseSubmenus(*s,v.popup);unsigned h=v.popup.window;ReleasePopupPixels(v);v.used=false;if(User::IsWindow(h))User::DestroyWindow(h);}}if(s&&popup_depth&&s==&popup_sessions[popup_depth-1]){*s={};--popup_depth;}dirty=true;}
 unsigned Send(unsigned h,MT::W message,MT::W wp,unsigned lp)override{
  if(auto*v=PopupForWindow(h)){if(message==0x403){MeasurePopup(*v);return unsigned(v->width)|(unsigned(v->height)<<16);}return 0;}
  if(!menu_send||!User::IsWindow(h))return 0;
  // A popup of the bar belongs to the bar's menu mode: WM_ENTERMENULOOP,
  // WM_INITMENU, the closing WM_MENUSELECT and WM_EXITMENULOOP go out once
  // for the whole session, not once for each popup it drops.
  if(bar_session.active&&h==bar_session.owner&&(message==0x211||message==0x212||message==0x116||(message==0x11f&&!wp&&lp==0xffffu)))return 0;
  // USER selects the owner root for menu-bar initialization.
  // USER publishes the selected bar position in WM_INITMENUPOPUP.
  if(auto*s=PopupSession())if(s->current&&s->current->owner==h)if(auto*v=PopupForCore(*s->current))if(v->bar_menu){
   if(message==0x116)wp=v->bar_menu;
   else if(message==0x117&&wp==v->popup.menu)lp=(lp&0xffff0000u)|(v->bar_index==0xffffu?0u:v->bar_index);
   else if(message==0x211||message==0x212)wp=0;
  }
  unsigned wparam=wp;if(message==0x11f){wparam|=(lp&65535)<<16;lp>>=16;}
  unsigned result=menu_send(CallbackContext(menu_context,h),h,message,wparam,lp);return result;
 }
 void Post(unsigned h,MT::W message,MT::W wp,unsigned lp)override{const bool posted=PostMessage(h,message,wp,lp);USER_TRACE("MENU post hwnd=%u message=%04x wparam=%04x posted=%u\n",h,unsigned(message),unsigned(wp),posted?1u:0u);}
 // A menu of the bar is no TrackPopupMenu popup: posts its command,
 // so the program runs it after the menu mode has ended.
 bool PostCommands(unsigned h)override{return bar_session.active&&h==bar_session.owner;}
 void Select(MT::Popup&p,MT::S index)override{auto*m=FindMenu(p.menu);if(p.selected==index)return;if(m&&p.selected>=0&&unsigned(p.selected)<m->count)m->items[p.selected].state&=~0x80u;p.selected=index;
  if(m&&index>=0&&unsigned(index)<m->count){auto&i=m->items[index];i.state|=0x80;unsigned flags=(i.state&0x8b)|(i.type&0x4b64);if(i.bitmap)flags|=4;if(i.submenu)flags|=0x10;flags&=0x5fff;auto*s=PopupSession();if(s){if(s->input_mode==-1)flags|=0x8000;if(s->flags&2)flags|=0x2000;}Send(p.owner,0x11f,i.submenu?index:i.id,flags|(p.menu<<16));}dirty=true;
 }
 void CloseSubmenus(MT::Session&,MT::Popup&p)override{for(unsigned depth=0;depth<16;++depth){bool removed=false;for(auto&v:popup_views)if(v.used&&v.parent){auto*ancestor=PopupForWindow(v.parent);bool descendant=false;for(unsigned n=0;ancestor&&n<16;++n){if(&ancestor->popup==&p){descendant=true;break;}ancestor=PopupForWindow(ancestor->parent);}if(descendant){for(auto&i:popup_views)if(i.used&&i.parent==v.popup.window)i.parent=p.window;RemovePopupView(v);removed=true;}}if(!removed)break;}}
 void DestroyPopup(MT::Popup&p)override{if(auto*v=PopupForCore(p)){unsigned h=v->popup.window;ReleasePopupPixels(*v);if(!v->session)v->used=false;if(User::IsWindow(h))User::DestroyWindow(h);}dirty=true;}
 unsigned CaptureOwner()override{return CaptureOperation(8);}void SetCapture(MT::Popup&p)override{CaptureOperation(4,p.owner);}void ReleaseCapture()override{CaptureOperation(5);}
 unsigned WindowMenu(unsigned h)override{return User::GetMenu(h);}bool MenuAnimationEnabled()override{return false;}void StartMenuAnimation(MT::Popup&)override{}
 void Accessibility(MT::W,unsigned)override{}MT::Rect WorkArea(MT::Point)override{return {0,0,short(settings.metrics[0]),short(settings.metrics[1])};}
 MT::S BorderX()override{return 2;}MT::S BorderY()override{return 2;}void AnimationSetting(MT::W)override{}
 void PositionPopup(MT::Popup&p,MT::Point at,MT::S width,MT::S height,bool)override{if(auto*v=PopupForCore(p))PlacePopup(*v,at,width,height);}
 void UpdatePopup(MT::Popup&)override{dirty=true;}
 MT::W DispatchLoop(MT::Session&s,MT::Popup&p)override{s.flags|=4;
  // A popup the keyboard dropped from the bar opens with its first item
  // selected and takes the keyboard's selection flags.
  if(bar_session.active&&bar_session.keyboard&&!p.parent)if(auto*m=FindMenu(p.menu)){s.input_mode=1;Select(p,MG::Next(*m,-1,1,0));}unsigned result=menu_loop?menu_loop(CurrentContext(menu_context)):0;FinishPopupDispatch(*this,s);return MT::W(result);}
} popup_environment;
void OpenPopupChild(PopupView&opener,bool keyboard){PopupView*parent=&opener;auto*m=FindMenu(parent->popup.menu);if(!m||parent->popup.selected<0||unsigned(parent->popup.selected)>=m->count)return;auto item=m->items[parent->popup.selected];if(!item.submenu||(item.state&3))return;auto*s=PopupSession();if(!s)return;const unsigned parent_window=parent->popup.window;popup_environment.CloseSubmenus(*s,parent->popup);
 if(!(parent->popup.flags&0x1000))popup_environment.Send(parent->popup.owner,0x117,item.submenu,unsigned(parent->popup.selected));
 // WM_INITMENUPOPUP ran in the guest. It may end the tracking, destroy this
 // popup, or call AppendMenu, which frees the item array and lays the items
 // out again. The view is resolved through its window handle and the item is
 // read once more before any of its geometry is used.
 parent=PopupForWindow(parent_window);if(!parent||!PopupSession())return;
 m=FindMenu(parent->popup.menu);if(!m||parent->popup.selected<0||unsigned(parent->popup.selected)>=m->count)return;
 item=m->items[parent->popup.selected];
 if(!item.submenu||!User::IsMenu(item.submenu)||!User::IsWindow(parent->popup.owner))return;
 auto*p=popup_environment.CreatePopup(item.submenu,parent->popup.owner,{0,0},false);if(!p)return;auto*v=PopupForCore(*p);v->parent=parent->popup.window;p->parent=parent->popup.window;p->flags=parent->popup.flags&~0x200u;p->flags|=8;
 const auto parent_outer=parent->geometry.outer;const MG::S parent_client_y=parent->geometry.client_y;const unsigned child_window=v->popup.window;
 MeasurePopup(*v);
 // WM_MEASUREITEM is a second return into the guest; the child view and the
 // parent geometry above are taken from the snapshot, the view from the table.
 v=PopupForWindow(child_window);if(!v)return;p=&v->popup;
 MT::Rect avoid{parent_outer.left,MT::S(parent_client_y+item.y),parent_outer.right,MT::S(parent_client_y+item.y+item.height)};MT::Point at{parent_outer.right,avoid.top};MT::W animation=0x400;
 at=Win98MenuPlacement::Place({0,0,short(settings.metrics[0]),short(settings.metrics[1])},at,v->width+4,v->height+4,&avoid,0x40,animation);p->animation=animation;PlacePopup(*v,at,v->width+4,v->height+4);if(keyboard)if(auto*sub=FindMenu(p->menu))popup_environment.Select(*p,MG::Next(*sub,-1,1,0));
}

void ClosePopupParent(PopupView&v){if(!v.parent){MT::End(popup_environment);return;}auto*parent=PopupForWindow(v.parent);if(auto*s=PopupSession())popup_environment.CloseSubmenus(*s,parent?parent->popup:v.popup);if(parent)dirty=true;}
void ActivatePopupItem(PopupView&v,bool keyboard){auto*m=FindMenu(v.popup.menu);auto*s=PopupSession();if(!s)return;int index=v.popup.selected;if(!m||index<0||unsigned(index)>=m->count){if(keyboard)MT::Close(popup_environment,*s,0,0,0);return;}const auto item=m->items[index];if(item.state&3){if(keyboard)MT::Close(popup_environment,*s,0,0,0);return;}if(item.submenu){OpenPopupChild(v,keyboard);return;}MT::Close(popup_environment,*s,(s->current->flags&4)?0x112:0x111,item.id,0);}
}
void SetMenuCallbacks(void*c,MenuSend send,MenuLoop loop,MenuThread thread){menu_context=c;menu_send=send;menu_loop=loop;menu_thread=thread;}
// The check mark, the bullet of a radio item and the arrow of a submenu as
// USER's popup shows them at the standard size (the shapes of OBM_CHECK,
// OBM_BTNCORNERS' bullet and OBM_MNARROW, drawn in the colour of the text).
// They used to be the letters "*", "o" and ">" of the menu font.
const char*const MenuCheck[]={"......X",".....XX","X...XXX","XX.XXX.","XXXXX..",".XXX...","..X....",nullptr};
const char*const MenuBullet[]={".XXXX.","XXXXXX","XXXXXX","XXXXXX","XXXXXX",".XXXX.",nullptr};
const char*const MenuArrow[]={"X...","XX..","XXX.","XXXX","XXX.","XX..","X...",nullptr};
template<class DC>void MenuGlyph(DC dc,int x,int y,const char*const*rows,unsigned color){
 for(int r=0;rows[r];++r)for(int c=0;rows[r][c];++c){
  int end=c;while(rows[r][end]=='X')++end;
  if(end>c){Fill(dc,{x+c,y+r,x+end,y+r+1},color);c=end-1;}
 }
}
unsigned MenuCheckMarkSize(){return unsigned(menu_owner_environment.CheckWidth()&65535)|(unsigned(menu_owner_environment.CheckHeight()&65535)<<16);}
void SetMenuOwnerCallbacks(void*c,MenuMeasure measure,MenuDrawItem draw){menu_owner_context=c;menu_measure=measure;menu_draw=draw;}
bool MeasureMenuObject(HMENU h,HWND owner,int bar_width,int&width,int&height){auto*m=FindMenu(h);if(!m)return false;const int save=Gdi::SaveDC(screen);OD::MeasureBar(menu_owner_environment,h,owner,OD::S(bar_width),0,0);if(save)Gdi::RestoreDC(screen,save);m=FindMenu(h);if(!m)return false;width=m->width;height=m->height;return true;}

unsigned TrackPopup(HMENU menu,unsigned flags,int x,int y,HWND owner,const Gdi::RECT*exclude){if(!menu_loop||!User::IsWindow(owner)||!User::IsMenu(menu))return 0;MT::Rect rect;if(exclude)rect={MT::S(exclude->left),MT::S(exclude->top),MT::S(exclude->right),MT::S(exclude->bottom)};return MT::Track(popup_environment,menu,owner,{MT::S(x),MT::S(y)},MT::W(flags),exclude?&rect:nullptr);}
// The tail of PopupEnvironment::DispatchLoop and MT::Track after a loaded
// popup loop. Its session and popup were already created before the save.
unsigned FinishPopupTracking(unsigned result,unsigned flags){
 auto*s=PopupSession();if(!s)return 0;
 FinishPopupDispatch(popup_environment,*s);
 if(!(s->flags&0x40))popup_environment.FinishSession(s);
 return flags&0x100?result:1;
}
namespace {
unsigned BarSend(unsigned message,unsigned wp,unsigned lp){
 const unsigned h=bar_session.owner;if(!menu_send||!User::IsWindow(h))return 0;
 return menu_send(CallbackContext(menu_context,h),h,message,wp,lp);
}
Win98Menu::Menu*BarMenu(){auto*w=Find(bar_session.owner);if(!w||!bar_session.bar||w->menu!=bar_session.bar)return nullptr;return FindMenu(bar_session.bar);}
unsigned BarCount(){auto*m=BarMenu();auto*w=Find(bar_session.owner);if(!m||!w)return 0;return m->count<w->menu_items?m->count:w->menu_items;}
// The bar item carries MFS_HILITE while it is selected, as the popup items
// do; PaintMenuBar draws it in the highlight colours. WM_MENUSELECT reports
// the new selection like the popup's Select.
void BarHighlight(int index){
 if(auto*m=BarMenu())for(unsigned n=0;n<m->count;++n)m->items[n].state&=~0x80u;
 if(index>=0&&unsigned(index)<BarCount())BarMenu()->items[index].state|=0x80u;
 dirty=true;
 if(index==BarNone||(index==bar_session.notified&&bar_session.notified_menu==bar_session.bar))return;
 bar_session.notified=index;bar_session.notified_menu=bar_session.bar;
 const unsigned mouse=bar_session.keyboard?0:0x8000u;
 if(index==BarSystem){BarSend(0x11f,0x2090u<<16|mouse<<16,bar_session.system);return;}
 auto*m=BarMenu();if(!m||unsigned(index)>=m->count)return;const auto&i=m->items[index];
 unsigned flags=(i.state&0x8b)|(i.type&0x4b64)|0x80;if(i.bitmap)flags|=4;if(i.submenu)flags|=0x10;flags=(flags&0x5fff)|mouse;
 BarSend(0x11f,(i.submenu?unsigned(index):i.id)|flags<<16,bar_session.bar);
}
// Left and right through the bar: separators and the system bitmaps of an
// MDI bar are skipped, and the system menu sits before the first
// item, so the walk wraps through it.
int BarStep(int index,int direction){
 auto*m=BarMenu();const int count=int(BarCount());const int low=bar_session.system?BarSystem:0;
 if(count<=0)return bar_session.system?BarSystem:BarNone;
 int position=index<low?(direction>0?low-1:count):index;
 for(int k=0;k<count+1;++k){
  position+=direction;
  if(position>=count)position=low;else if(position<low)position=count-1;
  if(position==BarSystem)return position;
  const auto&i=m->items[position];if(i.type&0x800)continue;if(i.bitmap>=2&&i.bitmap<=7)continue;return position;
 }
 return index;
}
void BarEnd(){bar_session.done=true;bar_session.leave=true;}
void BarOpen(int index,bool keyboard){bar_session.index=index;bar_session.open=true;bar_session.keyboard=keyboard;bar_session.again=true;bar_session.leave=true;}
// Enter, or a mnemonic that names one item: an item with a popup drops it,
// an item without one sends its command and ends the menu mode.
void BarExecute(bool keyboard){
 const int index=bar_session.index;if(index==BarSystem){BarOpen(index,keyboard);return;}
 auto*m=BarMenu();if(!m||index<0||unsigned(index)>=m->count){BarEnd();return;}
 const auto item=m->items[index];
 if(item.submenu){if(item.state&3){menu_beep=true;BarEnd();return;}BarOpen(index,keyboard);return;}
 BarEnd();if(!(item.state&3))PostMessage(bar_session.owner,0x111,item.id,0);
}
// on the bar: the items whose mnemonic is the character. One enabled
// item executes, several are stepped through, only disabled ones end the
// menu mode. Without a match the space opens the system menu and anything
// else asks the owner with WM_MENUCHAR; nothing there beeps and ends.
void BarCharacter(unsigned character){
 auto upper=[](unsigned c){return c>='a'&&c<='z'?c-32:c;};const unsigned wanted=upper(character&255);
 auto*m=BarMenu();const int count=int(BarCount());int first=-1,next=-1,enabled=0,matches=0;
 for(int n=0;m&&n<count;++n){const char*label=MenuText(m->items[n]);for(unsigned k=0;label[k];++k){if(label[k]=='&'&&label[k+1]=='&'){++k;continue;}if(label[k]=='&'&&label[k+1]){if(upper((unsigned char)label[k+1])==wanted){++matches;if(!(m->items[n].state&3)){++enabled;if(first<0)first=n;if(next<0&&n>bar_session.index)next=n;}}break;}}}
 if(matches&&!enabled){BarEnd();return;}
 if(enabled){bar_session.index=next>=0?next:first;BarHighlight(bar_session.index);if(enabled==1)BarExecute(true);return;}
 if(wanted==' '&&bar_session.system){bar_session.index=BarSystem;BarHighlight(BarSystem);BarExecute(true);return;}
 const unsigned menu=bar_session.index==BarSystem?bar_session.system:bar_session.bar;
 const unsigned answer=BarSend(0x120,(character&255)|(bar_session.index==BarSystem?0x2000u<<16:0),menu);
 if(!bar_session.active)return;
 const unsigned action=answer>>16,position=answer&65535;
 if(action==2||action==3){if(BarMenu()&&position<BarCount()){bar_session.index=int(position);BarHighlight(bar_session.index);if(action==2)BarExecute(true);}return;}
 if(action==0)menu_beep=true;
 BarEnd();
}
}
// The system icon of the caption shows the selection of the bar's system
// menu (PaintCaption, USER with ).
bool SystemIconSelected(HWND h){return bar_session.active&&bar_session.owner==h&&bar_session.index==BarSystem&&bar_session.system;}
bool BarModeActive(){return bar_session.active&&bar_session.bar_only&&!bar_session.leave&&User::IsWindow(bar_session.owner);}
bool MenuBeepRequest(){const bool beep=menu_beep;menu_beep=false;return beep;}
// Handle keyboard input while the menu bar is active.
bool BarInputMessage(const Message&m){
 if(m.message==0x100||m.message==0x104){
  switch(m.wparam){
  case 0x25:case 0x27:bar_session.keyboard=true;bar_session.index=BarStep(bar_session.index,m.wparam==0x25?-1:1);BarHighlight(bar_session.index);return true;
  case 0x26:case 0x28:{if(bar_session.index==BarSystem){BarOpen(BarSystem,true);return true;}auto*menu=BarMenu();if(menu&&bar_session.index>=0&&unsigned(bar_session.index)<menu->count&&menu->items[bar_session.index].submenu)BarExecute(true);return true;}
  case 0x0d:BarExecute(true);return true;
  case 0x1b:case 0x12:case 0x79:BarEnd();return true;
  case 0x10:case 0x11:case 0x14:case 0x90:case 0x91:return true;
  default:{unsigned char characters[2];const int count=TranslateKey(m.wparam,m.lparam,characters);if(count>0)BarCharacter(characters[0]);return true;}
  }
 }
 if(m.message==0x101||m.message==0x105||m.message==0x103||m.message==0x107)return true;
 if(m.message==0x102||m.message==0x106){BarCharacter(m.wparam);return true;}
 return false;
}
// A press while only the bar is selected: on one of the bar's items it drops
// that item's popup, anywhere else it ends the menu mode and goes on to
// whatever lies there.
bool BarRouteInput(const InputPacket&p){
 if(!BarModeActive()||p.kind!=2||!(p.changed&p.buttons))return false;
 auto*w=Find(bar_session.owner);
 if(w&&(w->nonclient_flags&1)){const auto o=Origin(*w);for(unsigned j=0;j<w->menu_items&&j<BarCount();++j)if(Inside(w->menu_item[j],p.x-o.x,p.y-o.y)){bar_session.index=int(j);bar_session.keyboard=false;BarHighlight(int(j));BarExecute(false);return true;}}
 BarEnd();BarHighlight(BarNone);return false;
}
// Resume only the rest after the recorded menu loop. The popup session and
// bar state already exist; entering Track again would create a second popup.
unsigned ContinueBarSession(){
 const unsigned owner=bar_session.owner;
 for(;bar_session.round<256&&!bar_session.done;++bar_session.round){
  if(!User::IsWindow(owner))break;
  if(bar_session.bar&&!BarMenu())bar_session.bar=0;
  if(bar_session.index>=0&&unsigned(bar_session.index)>=BarCount())break;
  if(bar_session.index==BarSystem&&!bar_session.system)break;
  if(bar_session.index==BarNone)break;
  BarHighlight(bar_session.index);
  bar_session.again=bar_session.leave=false;
  if(bar_session.open){
   const int at=bar_session.index;auto*w=Find(owner);if(!w)break;const auto o=Origin(*w);
   unsigned menu=bar_session.system;MT::S x=MT::S(o.x+w->caption.left),y=MT::S(o.y+w->caption.bottom);
   if(at>=0){auto*m=BarMenu();if(!m)break;const auto item=m->items[at];if(!item.submenu){BarExecute(bar_session.keyboard);break;}menu=item.submenu;x=MT::S(o.x+w->menu_item[at].left);y=MT::S(o.y+w->menu_item[at].bottom);}
   popup_environment.creating_bar=bar_session.bar;popup_environment.creating_index=at>=0?unsigned(at):0xffffu;
   bar_session.track_flags=at==BarSystem?0x200:0;bar_session.phase=1;
   bar_session.result=MT::Track(popup_environment,menu,owner,{x,y},bar_session.track_flags);
   bar_session.phase=0;
   popup_environment.creating_bar=popup_environment.creating_index=0;
   if(!bar_session.again)break;       // a command, a click outside, ALT
  }else{
   // Only the bar item is selected: the menu loop of the owner's thread
   // runs until a key or a click decides.
   bar_session.bar_only=true;bar_session.keyboard=true;
   bar_session.phase=2;menu_loop(CurrentContext(menu_context));bar_session.phase=0;
   bar_session.bar_only=false;
   if(!bar_session.again)break;
  }
 }
 bar_session.index=BarNone;BarHighlight(BarNone);
 BarSend(0x11f,0xffffu<<16,0);   // WM_MENUSELECT, menu closed
 BarSend(0x212,0,0);              // WM_EXITMENULOOP
 const unsigned result=bar_session.result;bar_session=BarSession{};dirty=true;
 return result;
}
// One menu mode of a bar. index is the item to
// select first, open whether its popup drops at once, character the letter
// of ALT+letter (0 for ALT or F10 alone).
unsigned RunBarSession(HWND owner,HMENU bar,HMENU system,int index,bool keyboard,bool open,unsigned character){
 if(bar_session.active||PopupSession()||!menu_loop||!User::IsWindow(owner))return 0;
 if(bar&&!FindMenu(bar))bar=0;if(system&&!FindMenu(system))system=0;
 if(!bar&&!system)return 0;
 bar_session=BarSession{};bar_session.active=true;bar_session.owner=owner;bar_session.bar=bar;bar_session.system=system;
 bar_session.index=index;bar_session.open=open;bar_session.keyboard=keyboard;
 BarSend(0x211,0,0);                 // WM_ENTERMENULOOP, not a TrackPopupMenu
 if(bar_session.active)BarSend(0x116,bar?bar:system,0); // WM_INITMENU
 if(character&&IsWindow(owner)){bar_session.index=BarNone;BarCharacter(character);if(bar_session.index==BarNone)BarEnd();}
 return ContinueBarSession();
}
unsigned ResumeBarTracking(unsigned result){
 if(!bar_session.active)return 0;
 if(bar_session.phase==1){
  bar_session.result=FinishPopupTracking(result,bar_session.track_flags);
  popup_environment.creating_bar=popup_environment.creating_index=0;
 }else if(bar_session.phase==2)bar_session.bar_only=false;
 else return 0;
 bar_session.phase=0;
 if(!bar_session.again)bar_session.done=true;
 else ++bar_session.round;
 return ContinueBarSession();
}
// A press on an item of the menu bar.
unsigned TrackMenuBar(HMENU bar,unsigned index,int,int,HWND owner){
 unsigned system=0;if(auto*root=FindMenu(SystemMenuRoot(owner)))if(root->count)system=root->items[0].submenu;
 return RunBarSession(owner,bar,system,int(index),false,true,0);
}
// SC_MOUSEMENU of DefWindowProc for a press on the menu bar at the screen
// point x,y (USER with ): the item there drops its popup, an
// item without one is its command, a disabled or grayed one and the bar
// between the items do nothing.
unsigned MouseMenu(HWND owner,int x,int y){
 auto*w=Find(owner);if(!w||!(w->nonclient_flags&1))return 0;
 auto*m=FindMenu(w->menu);if(!m)return 0;
 const auto o=Origin(*w);
 for(unsigned j=0;j<w->menu_items&&j<m->count;++j)if(Inside(w->menu_item[j],x-o.x,y-o.y)){
  const auto&item=m->items[j];
  if(MenuFlags(item)&0x803)return 0;
  if(!item.submenu){PostMessage(owner,0x111,item.id,0);return 0;}
  return TrackMenuBar(w->menu,j,o.x+w->menu_item[j].left,o.y+w->menu_item[j].bottom,owner);
 }
 return 0;
}
unsigned KeyMenu(HWND owner,HMENU system,unsigned character){
 auto*w=Find(owner);if(!w||bar_session.active||PopupSession())return 0;
 const bool child=(w->style&0xc0000000u)==0x40000000u;
 HMENU bar=!child&&(w->nonclient_flags&1)?w->menu:0;
 if(bar&&!FindMenu(bar))bar=0;
 // the space, or the minus of a child, opens the system menu; a
 // window without one beeps.
 if(character==' '||(character=='-'&&child)){if(!system){menu_beep=true;return 0;}return RunBarSession(owner,bar,system,BarSystem,true,true,0);}
 if(!character){
  int first=BarNone;bar_session.bar=bar;bar_session.owner=owner;bar_session.system=0;
  if(bar)first=BarStep(BarNone,1);
  bar_session=BarSession{};
  if(first==BarNone)first=system?BarSystem:BarNone;
  if(first==BarNone)return 0;
  return RunBarSession(owner,bar,system,first,true,false,0);
 }
 return RunBarSession(owner,bar,system,BarNone,true,false,character);
}
bool EndMenu(){if(bar_session.active){BarEnd();bar_session.again=false;}return MT::End(popup_environment);}
bool PopupActive(){auto*s=PopupSession();return s&&s->current&&(s->flags&4)&&!(s->current->auxiliary&4)&&User::IsWindow(s->current->owner)&&User::IsMenu(s->current->menu);}
unsigned PopupCommand(){auto*s=PopupSession();return s?s->command:0;}
// The pointer rests on an item with a submenu: USER arms a timer on the popup
// window for SPI_GETMENUSHOWDELAY and opens the submenu when it fires, without
// a click. The request names the popup window whose selection the
// pointer moved; the menu loop sets the timer.
unsigned popup_hover_request=0;
unsigned PopupHoverRequest(){unsigned h=popup_hover_request;popup_hover_request=0;return h;}
bool PopupHoverOpen(unsigned window){auto*v=PopupForWindow(window);if(!v||!PopupSession())return false;
 auto*m=FindMenu(v->popup.menu);if(!m||v->popup.selected<0||unsigned(v->popup.selected)>=m->count)return true;
 const auto&item=m->items[v->popup.selected];if(!item.submenu||(item.state&3))return true;
 for(auto&child:popup_views)if(child.used&&child.parent==window)return true; // already open
 OpenPopupChild(*v,false);return true;}
unsigned PopupWindowForMenu(HMENU h){auto*v=PopupForMenu(h);return v?v->popup.window:0;}
bool MenuIdleTarget(HWND &owner,HWND &popup){
 if(PopupActive()){auto*p=PopupSession()->current;owner=p->owner;popup=p->window;return true;}
 if(BarModeActive()){owner=bar_session.owner;popup=0;return IsWindow(owner);}
 return false;
}
bool MenuItemRect(HWND h,HMENU menu,unsigned index,Gdi::RECT*out){if(!out)return false;MG::Rect rect;bool result=MG::ItemRect(popup_geometry,h,menu,index,rect);*out={rect.left,rect.top,rect.right,rect.bottom};return result;}
int MenuItemFromPoint(HWND h,HMENU menu,int x,int y){return MG::FromPoint(popup_geometry,h,menu,x,y);}
void PopupWindowDestroyed(HWND h){auto*s=PopupSession();if(!s||!s->current||s->current->auxiliary&2)return;if(s->current->owner==h||s->current->window==h)MT::End(popup_environment);}
void ResetPopupMenus(){menu_context=nullptr;menu_send=nullptr;menu_loop=nullptr;menu_thread=nullptr;menu_owner_context=nullptr;menu_measure=nullptr;menu_draw=nullptr;menu_owner_environment.uniform_height=0;for(auto&v:popup_views)if(v.used){unsigned h=v.popup.window;ReleasePopupPixels(v);v.used=false;User::DestroyWindow(h);}for(auto&s:popup_sessions){ClearPopupInput(&s);s={};}popup_depth=0;}
bool PopupInputMessage(const Message&m){if(BarModeActive())return BarInputMessage(m);auto*v=PopupTop();auto*s=PopupSession();if(!v||!s)return false;
 if(m.message==0x100||m.message==0x104){s->input_mode=1;if(s->flags&8){if(m.wparam!=0x70)return true;}auto*menu=FindMenu(v->popup.menu);if(!menu)return true;
  switch(m.wparam){case 0x26:case 0x28:popup_environment.CloseSubmenus(*s,v->popup);popup_environment.Select(v->popup,MG::Next(*menu,v->popup.selected,m.wparam==0x26?-1:1,0));return true;
   case 0xd:ActivatePopupItem(*v,true);return true;case 0x12:case 0x79:MT::Close(popup_environment,*s,0,0,0);return true;
   // In a popup of the bar, ESC on the first level goes back to the bar item,
   // LEFT there and RIGHT on an item without a popup move to the next bar
   // item's popup (with the bar as parent).
   case 0x1b:if(!v->parent&&bar_session.active){bar_session.open=false;bar_session.keyboard=true;bar_session.again=true;MT::Close(popup_environment,*s,0,0,0);return true;}ClosePopupParent(*v);return true;
   case 0x27:{const int selected=v->popup.selected;if(selected>=0&&unsigned(selected)<menu->count&&menu->items[selected].submenu&&!(menu->items[selected].state&3)){OpenPopupChild(*v,true);return true;}}
    if(bar_session.active){BarOpen(BarStep(bar_session.index,1),true);MT::Close(popup_environment,*s,0,0,0);}return true;
   case 0x25:if(v->parent){ClosePopupParent(*v);return true;}
    if(bar_session.active){BarOpen(BarStep(bar_session.index,-1),true);MT::Close(popup_environment,*s,0,0,0);}return true;
   default:return true;}}
 if(m.message==0x101||m.message==0x105)return true;
 if(m.message==0x102||m.message==0x106){auto*menu=FindMenu(v->popup.menu);if(!menu)return true;unsigned character=m.wparam;if(character>='a'&&character<='z')character-=32;
  int match=-1;unsigned matches=0;for(unsigned n=0;n<menu->count;++n){const char*label=MenuText(menu->items[n]);for(unsigned k=0;label[k];++k){if(label[k]=='&'&&label[k+1]=='&'){++k;continue;}if(label[k]=='&'&&label[k+1]){unsigned c=label[k+1];if(c>='a'&&c<='z')c-=32;if(c==character){if(match<0||(int(n)>v->popup.selected&&match<=v->popup.selected))match=n;++matches;}break;}}}
  // Every branch below returns into the guest (WM_MENUSELECT, WM_MENUCHAR).
  // The view is looked up again through its window handle afterwards, because
  // the handler may have ended the tracking and freed the slot.
  const unsigned window=v->popup.window;
  if(match>=0){popup_environment.Select(v->popup,match);v=PopupForWindow(window);if(!v)return true;if(matches==1)ActivatePopupItem(*v,true);}else if(menu_send){unsigned answer=menu_send(CallbackContext(menu_context,v->popup.owner),v->popup.owner,0x120,m.wparam|(0x10u<<16),v->popup.menu);v=PopupForWindow(window);if(!v)return true;unsigned action=answer>>16;if(action==0)menu_beep=true;if(action==1)MT::End(popup_environment);else if(action==2||action==3){popup_environment.Select(v->popup,answer&65535);v=PopupForWindow(window);if(!v)return true;if(action==2)ActivatePopupItem(*v,true);}}return true;

 }
 return false;
}
bool PopupRouteInput(const InputPacket&p){auto*s=PopupSession();if(!s||!s->current||!(s->flags&4))return false;
 // The application input collector runs on its main GUI thread; tracking
 // can belong to a worker. Preserve the packet until that menu loop runs.
 if(popup_environment.CurrentThread()!=s->thread){
  auto&q=popup_inputs[s-popup_sessions];
  if(q.tail&&p.kind==2&&!p.changed&&q.tail->packet.kind==2&&!q.tail->packet.changed&&q.tail->packet.buttons==p.buttons){
   const bool moved=q.tail->packet.moved;q.tail->packet=p;q.tail->packet.moved|=moved;
  }else{auto*n=new PopupInputNode{p,nullptr};if(q.tail)q.tail->next=n;else q.head=n;q.tail=n;}
  return true;
 }

 if(p.kind==1||p.kind==2)AcceptInputMetadata(p);
 // The menu loop of USER retrieves the key and hands it to TranslateMessage,
 // so the character comes from the layout with the key state
 // of that key, as for any other thread.
 if(p.kind==1){Message m{};m.message=p.message;m.wparam=p.key;m.lparam=p.parameter;if(PopupInputMessage(m)){unsigned char characters[2];int count=TranslateKey(p.key,p.parameter,characters,p.state);if(count>0){m.message=(p.message&~4u)==0x100?(0x102|(p.message&4)):0x102;for(int i=0;i<count&&i<2;++i){m.wparam=characters[i];PopupInputMessage(m);}}return true;}return false;}
 if(p.kind!=2)return false;PopupView*hit=nullptr;for(auto&v:popup_views)if(v.used&&MG::Contains(v.geometry.outer,p.x,p.y)){bool ancestor=false;for(auto&child:popup_views)if(child.used&&child.parent==v.popup.window&&MG::Contains(child.geometry.outer,p.x,p.y))ancestor=true;if(!ancestor)hit=&v;}
 // Left clicks operate every menu. TPM_RIGHTBUTTON additionally enables
 // right-button input; it does not disable the left button. Outside clicks
 // must still close the menu.
 s->input_mode=-1;const unsigned button=(s->current->flags&0x80)?3:1;bool down=p.changed&p.buttons&button,up=p.changed&~p.buttons&button;
 if(hit){int index=MG::Hit(popup_geometry,hit->popup.menu,hit->popup.window,p.x,p.y);if(index>=0){if(hit->popup.selected!=index){const unsigned window=hit->popup.window;popup_environment.CloseSubmenus(*s,hit->popup);popup_environment.Select(hit->popup,index);
   // WM_MENUSELECT ran in the guest; resolve the view and the session again.
   hit=PopupForWindow(window);s=PopupSession();if(!hit||!s)return true;
   if(!down&&!up)if(auto*m=FindMenu(hit->popup.menu))if(unsigned(index)<m->count&&m->items[index].submenu&&!(m->items[index].state&3))popup_hover_request=window;}
  if(down)OpenPopupChild(*hit,false);if(up){s=PopupSession();if(!s)return true;s->flags&=~8u;ActivatePopupItem(*hit,false);}}else if(up)s->flags&=~8u;}

 else{
  // Outside every popup: a bar item of the owner under the pointer switches
  // the tracking to it (on movement with the popup open, or on a press);
  // pressing the open item closes it; any other press closes as before.
  PopupView*root=nullptr;for(auto&v:popup_views)if(v.used&&v.bar_menu&&v.session==s){root=&v;break;}
  auto*owner=root?Find(root->popup.owner):nullptr;int bar_hit=-1;
  if(owner&&(owner->nonclient_flags&1)){const auto o=Origin(*owner);for(unsigned j=0;j<owner->menu_items;++j)if(Inside(owner->menu_item[j],p.x-o.x,p.y-o.y)){bar_hit=int(j);break;}}
  if(bar_hit>=0&&unsigned(bar_hit)!=root->bar_index&&(down||p.moved)&&bar_session.active){BarOpen(bar_hit,false);MT::Close(popup_environment,*s,0,0,0);}
  else if(down)MT::Close(popup_environment,*s,0,0,0);
  else if(up)s->flags&=~8u;
 }
 s=PopupSession();if(down&&s)s->flags|=8;dirty=true;return true;
}

bool PendingPopupInput(){
 auto*s=PopupSession();if(!s||!s->current||!popup_inputs[s-popup_sessions].head)return false;
 return (!active_thread||active_thread==s->thread)&&
        (!active_process||active_process==WindowProcess(s->current->owner));
}
bool DispatchPopupInput(){
 if(!PendingPopupInput())return false;
 auto*s=PopupSession();auto&q=popup_inputs[s-popup_sessions];auto*n=q.head;
 const auto packet=n->packet;q.head=n->next;if(!q.head)q.tail=nullptr;delete n;
 return PopupRouteInput(packet);
}

void MenuBlit(Gdi::HDC target,unsigned bitmap,int x,int y){
 unsigned info[6]{};if(!Gdi::BitmapInfo(bitmap,info))return;
 Gdi::HDC dc=Gdi::CreateCompatibleDC(target);if(!dc)return;
 auto previous=Gdi::SelectObject(dc,bitmap);
 Gdi::BitBlt(target,x,y,int(info[1]),int(info[2]),dc,0,0,0xcc0020);
 Gdi::SelectObject(dc,previous);Gdi::DeleteDC(dc);
}
// Popup drawing runs at the menu thread's USER wait, never in Render.
// A private buffer stays unpublished while WM_DRAWITEM yields or destroys
// the menu. Render keeps showing the previous complete one.
bool PendingPopupPaint(){
 for(const auto&v:popup_views){const auto*w=v.used?Find(v.popup.window):nullptr;
  if(!w||v.painting||!IsVisible(v.popup.window)||!FindMenu(v.popup.menu))continue;
  if((active_thread&&w->thread!=active_thread)||(active_process&&w->process!=active_process))continue;
  if(v.painted_generation!=dirty.generation)return true;
 }return false;
}
unsigned ServePopupPaints(){
 unsigned painted=0;
 for(auto&v:popup_views){auto*w=v.used?Find(v.popup.window):nullptr;
  auto*m=w?FindMenu(v.popup.menu):nullptr;
  if(!w||!m||v.painting||!IsVisible(v.popup.window))continue;
  if((active_thread&&w->thread!=active_thread)||(active_process&&w->process!=active_process))continue;
  if(v.painted_generation==dirty.generation)continue;
  const unsigned generation=dirty.generation,window=v.popup.window,menu=v.popup.menu;
  const auto r=v.geometry.outer;
  const int width=r.right-r.left,height=r.bottom-r.top;
  if(width<=0||height<=0||width>int(settings.metrics[0])||height>int(settings.metrics[1])){v.painted_generation=generation;continue;}
  auto*pixels=new unsigned char[size_t(width)*height*4]{};
  const auto dc=pixels?Gdi::CreateSurfaceDC({pixels,width,height,width*4,32}):0;
  if(!dc){delete[] pixels;v.painted_generation=generation;continue;}
  v.painting=true;
  Gdi::SetViewportOrgEx(dc,-r.left,-r.top);
  Fill(dc,{r.left,r.top,r.right,r.bottom},0xc0c0c0);Edge(dc,{r.left,r.top,r.right,r.bottom});
  MenuOwnerEnvironment environment;
Gdi::SelectObject(dc,system_font);
 const unsigned popup_window=v.popup.window;
 environment.offset_x=environment.offset_y=0;
 // The item is copied out of the table: WM_DRAWITEM below returns into the
 // guest, and AppendMenu there frees and reallocates the item array.
 for(unsigned n=0;n<m->count;++n){const auto i=m->items[n];
  const int x=v.geometry.client_x+i.x,y=v.geometry.client_y+i.y;
  const int right=i.width?x+i.width:r.right-2;
  if(y>=r.bottom-2)break;
  // The owner paints the whole item into the private popup surface.
  if((i.type&0x100)||i.bitmap==0xffff){
   const int saved=Gdi::SaveDC(dc);
   Gdi::SetViewportOrgEx(dc,v.geometry.client_x-r.left,v.geometry.client_y-r.top);
   const auto kept_dc=menu_surface_dc;const auto kept_window=menu_surface_window;
   menu_surface_dc=dc;menu_surface_window=popup_window;
   OD::DrawOwnerItem(environment,m,v.popup.menu,i,!(i.type&0x100),
      (i.type&0x100)?0:4+((m->flags&8)?0:environment.CheckWidth()),1,v.popup.owner,dc);
   menu_surface_dc=kept_dc;menu_surface_window=kept_window;
   if(saved)Gdi::RestoreDC(dc,saved);
   // The handler may have ended the tracking or changed the menu; resolve the
   // view and the menu again before the next item is read.
   if(!v.used||v.popup.window!=popup_window)break;
   m=FindMenu(v.popup.menu);if(!m)break;
   if(i.type&0x100)continue;}

  if(i.type&0x800){Fill(dc,{x+5,y+3,right-5,y+4},0x808080);Fill(dc,{x+5,y+4,right-5,y+5},0xffffff);continue;}
  const bool selected=int(n)==v.popup.selected;
  if(selected)Fill(dc,{x,y,right,y+i.height},0x000080);
  const unsigned color=(i.state&3)?0x808080u:selected?0xffffffu:0u;
  unsigned short mark=0;MG::S dy=0;bool radio=false;
  switch(OD::CheckMark(environment,i,mark,dy,radio)){
  case 1:MenuGlyph(dc,x+5,y+(int(i.height)-7)/2,radio?MenuBullet:MenuCheck,color);break;
  case 2:MenuBlit(dc,mark,x+2,y+dy);break;
  default:break;
  }
  const int text_x=x+(m->scroll?m->scroll:20);
  if(i.bitmap>=12&&i.bitmap!=0xffff&&!i.text){MenuBlit(dc,i.bitmap,text_x,y+1);}
  else{
   const char*source=MenuText(i);const char*tab=source;while(*tab&&*tab!='\t')++tab;
   const unsigned count=unsigned(tab-source);
   if(*tab){char accelerator[96];MenuLabel(tab+1,accelerator);Label(dc,v.geometry.client_x+i.layout22,y+2,accelerator,color);}
   PrefixLabel(dc,text_x,y+2,source,count,color); // underlined mnemonic
   if(i.state&0x1000)PrefixLabel(dc,text_x+1,y+2,source,count,color); // bold stand-in for the default item
  }
  if(i.submenu)MenuGlyph(dc,right-11,y+(int(i.height)-7)/2,MenuArrow,color);
 }

  Gdi::DeleteDC(dc);
  // Every guest callback may recycle the view slot. Match the generation-
  // tagged HWND before publishing or touching the replacement's flags.
  if(v.used&&v.popup.window==window){
   v.painting=false;
   if(v.popup.menu==menu&&FindMenu(menu)&&User::IsWindow(window)&&
      v.geometry.outer.left==r.left&&v.geometry.outer.top==r.top&&
      v.geometry.outer.right==r.right&&v.geometry.outer.bottom==r.bottom){
    ReleasePopupPixels(v);v.pixels=pixels;pixels=nullptr;
    v.pixel_width=width;v.pixel_height=height;
    const bool changed=dirty.generation!=generation;
    dirty.Mark();v.painted_generation=changed?generation:dirty.generation;++painted;
   }
  }
  delete[] pixels;
 }return painted;
}
void PaintPopupMenus(){
 for(const auto&v:popup_views)if(v.used&&v.pixels&&User::IsVisible(v.popup.window)){
  const auto previous=screen;if(host_window_surfaces)screen=HostFrameDC(v.popup.window);
  const auto r=v.geometry.outer;const int saved=Gdi::SaveDC(screen);if(!saved){screen=previous;continue;}
  Gdi::IntersectClipRect(screen,r.left,r.top,r.right,r.bottom);
  Gdi::CopySurface(screen,r.left,r.top,{v.pixels,v.pixel_width,v.pixel_height,v.pixel_width*4,32});
  Gdi::RestoreDC(screen,saved);
  screen=previous;
 }
}

namespace {
struct PopupBarInformation : Win98MenuBarInfo::Environment {
 bool GetWindow(unsigned h,Win98MenuBarInfo::Window&out)override{auto*w=Find(h);if(!w)return false;auto p=Origin(*w);out={};out.outer={MT::S(p.x),MT::S(p.y),MT::S(p.x+w->width),MT::S(p.y+w->height)};out.client={MT::S(p.x+ClientLeft(*w)),MT::S(p.y+ClientTop(*w)),MT::S(p.x+w->client.right),MT::S(p.y+w->client.bottom)};out.menu=w->menu;out.system=GetSubMenu(w->system_menu,0);out.child=w->style&0x40000000;out.system_style=w->style&0x80000;out.system_button=out.system_style&&(w->style&0xc00000);out.frame_x=p.x+w->caption.left;out.frame_y=p.y+w->caption.top;out.caption_height=w->caption.bottom-w->caption.top;out.button_width=out.button_height=out.caption_height;if(auto*v=PopupForWindow(h)){out.popup_menu=v->popup.menu;out.client={v->geometry.client_x,v->geometry.client_y,MT::S(v->geometry.outer.right-2),MT::S(v->geometry.outer.bottom-2)};}return true;}
 Win98Menu::Menu*Menu(unsigned h)override{return FindMenu(h);}
 Win98MenuBarInfo::Focus Tracking(unsigned h,bool popup)override{Win98MenuBarInfo::Focus f;auto*s=PopupSession();if(!s||!s->current)return f;f.valid=true;if(popup){auto*v=PopupForWindow(h);if(!v)return f;f.popup_matches=v->popup.owner==s->current->owner;f.selected=v->popup.selected;for(auto&child:popup_views)if(child.used&&child.parent==h)f.child=child.popup.window;}else{auto&p=*s->current;f.menu_bar=(p.flags&2)&&p.owner==h;f.system=p.flags&4;f.selected=p.selected;for(auto&child:popup_views)if(child.used&&child.parent==p.window)f.child=child.popup.window;}return f;}
} popup_bar_information;
}
bool MenuBarInfo(HWND h,int object,int item,unsigned*data){if(!data||data[0]!=32)return false;Win98MenuBarInfo::Info i{data[0]-14,{MT::S(data[1]),MT::S(data[2]),MT::S(data[3]),MT::S(data[4])},MT::W(data[5]),MT::W(data[6]),MT::W(data[7])};bool okay=Win98MenuBarInfo::Get(popup_bar_information,h,object,item,i);data[0]=i.size+14;data[1]=int(i.rect.left);data[2]=int(i.rect.top);data[3]=int(i.rect.right);data[4]=int(i.rect.bottom);data[5]=i.menu;data[6]=i.popup;data[7]=int(MT::S(i.flags));return okay;}

unsigned CaptureMenuState(unsigned&flags,unsigned&present){auto*s=PopupSession();present=s&&s->current;flags=s?s->flags:0;if(present)return s->current->owner;if(bar_session.active){present=1;flags=0;return bar_session.owner;}return 0;}
