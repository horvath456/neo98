// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_menu.h"
namespace Win98MenuGeometry {
using Win98Menu::U;using Win98Menu::W;using Win98Menu::S;
struct Rect {S left=0,top=0,right=0,bottom=0;};
struct Window {Rect outer;S client_x=0,client_y=0;bool popup_class=false;};
struct Environment {
 virtual ~Environment()=default;
 virtual Win98Menu::Menu*Menu(U)=0;
 virtual Window*FindWindow(U)=0;
 virtual U PopupWindow(U owner,U menu)=0;
 virtual void LayoutBar(U owner,U menu)=0;
 virtual S BorderX()=0;virtual S BorderY()=0;
};
inline bool Contains(const Rect&r,S x,S y){return x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom;}
// USER. The rectangle is cleared before the index check.
inline bool ItemRect(Environment&e,U window,U menu,W index,Rect&r){
 r={};auto*m=e.Menu(menu);if(!m||index>=m->count)return false;
 auto*w=e.FindWindow(window);if(!w)return false;
 if(w->popup_class){window=e.PopupWindow(window,menu);w=e.FindWindow(window);if(!w)return false;}
 S x,y;if(m->flags&1){x=w->client_x;y=w->client_y;}else{e.LayoutBar(window,menu);x=w->outer.left;y=w->outer.top;}
 const auto&i=m->items[index];r.left=S(W(x)+i.x);r.top=S(W(y)+i.y);r.right=S(W(r.left)+i.width);r.bottom=S(W(r.top)+i.height);return true;
}
// USER. Negative return values include scroll arrows (-2/-3).
inline S Hit(Environment&e,U menu,U window,S x,S y){
 auto*m=e.Menu(menu);auto*w=e.FindWindow(window);if(!m||!w)return -1;
 if(m->flags&1){Rect outer=w->outer;outer.left=S(outer.left+e.BorderX());outer.top=S(outer.top+e.BorderY());outer.right=S(outer.right-e.BorderX());outer.bottom=S(outer.bottom-e.BorderY());if(!Contains(outer,x,y))return -1;
  x=S(W(x)-W(w->client_x));y=S(W(y)-W(w->client_y));if(y<0)return -2;if(y>S(m->height))return -3;
 }else{x=S(W(x)-W(w->outer.left));y=S(W(y)-W(w->outer.top));}
 if(m->status&3){U index=m->first_visible;if(index>=m->count)return -1;const auto&first=m->items[index];Rect r{0,0,S(first.width),S(first.height)};
  for(;index<m->count&&r.top<S(m->height);++index){if(Contains(r,x,y))return S(index);r.top=r.bottom;if(index+1<m->count)r.bottom=S(W(r.bottom)+m->items[index+1].height);}return -1;
 }
 for(U n=0;n<m->count;++n){const auto&i=m->items[n];Rect r{S(i.x),S(i.y),S(i.x+i.width),S(i.y+i.height)};if(Contains(r,x,y))return S(n);}return -1;
}
// USER selects the current popup window before hit testing.
inline S FromPoint(Environment&e,U window,U menu,S x,S y){auto*m=e.Menu(menu);if(!m)return -1;window=e.PopupWindow(window,menu);if(!e.FindWindow(window))return -1;if(!(m->flags&1))e.LayoutBar(window,menu);return Hit(e,menu,window,x,y);}
// USER. Disabled items remain navigable; separator and special
// system bitmap decisions are made independently of item state.
inline S Next(const Win98Menu::Menu&m,S current,S direction,W flags){
 S original=current,index=current,count=S(m.count);if(!count)return -1;
 if(current<0&&direction>0)original=index=count;else if(current>=count&&direction<0)original=index=-1;
 bool inspect=index==0&&count==1&&direction>0;if(inspect)direction=0;
 for(U steps=0;steps<U(m.count)*2+2;++steps){if(!inspect){index=S(index+direction);if(index==original)return -1;if(index>=count)index=0;else if(index<0)index=S(count-1);}inspect=false;
  if(index<0||index>=count)return -1;const auto&i=m.items[W(index)];if(i.type&0x800){if(flags&1)return index;continue;}if(i.bitmap>=2&&i.bitmap<=7)continue;return index;
 }return -1; // Native bound for malformed callers that cannot terminate the WORD loop.
}
}
