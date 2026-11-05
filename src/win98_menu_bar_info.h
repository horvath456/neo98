// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_menu_geometry.h"
namespace Win98MenuBarInfo {
using Win98Menu::U;using Win98Menu::W;using Win98Menu::S;using Win98MenuGeometry::Rect;
struct Info {U size=18;Rect rect;W menu=0,popup=0,flags=0;};
struct Window {Rect outer,client;U menu=0,system=0,popup_menu=0;bool child=false,system_style=false,system_button=false;S frame_x=0,frame_y=0;W caption_height=0,button_width=0,button_height=0;};
struct Focus {bool valid=false,menu_bar=false,system=false,popup_matches=false;S selected=-1;U child=0;};
struct Environment {virtual ~Environment()=default;virtual bool GetWindow(U,Window&)=0;virtual Win98Menu::Menu*Menu(U)=0;virtual Focus Tracking(U,bool popup)=0;};
// USER through the second USER31 thunk table (0574 + index0).
// Frame calculation and system-menu/window lookup use their existing providers.
inline bool Get(Environment&e,U hwnd,int object,int item,Info&i){
 Window w;if(!e.GetWindow(hwnd,w)||i.size!=18)return false;i.rect={};i.menu=i.popup=0;i.flags&=W(~3);
 U h=object==-3?w.menu:object==-1?w.system:object==-4?w.popup_menu:0;
 if((object==-3&&w.child)||(object==-1&&!w.system_style))return false;
 auto*m=e.Menu(h);if(!m||item<0||U(item)>m->count)return false;i.menu=W(h);
 if(object==-3){if(m->width&&m->height){if(!item)i.rect={w.frame_x,S(w.frame_y+w.caption_height),S(w.frame_x+m->width),S(w.frame_y+w.caption_height+m->height)};else{auto&v=m->items[item-1];i.rect={S(w.outer.left+v.x),S(w.outer.top+v.y),S(w.outer.left+v.x+v.width),S(w.outer.top+v.y+v.height)};}}}
 else if(object==-1){if(w.system_button)i.rect={w.frame_x,w.frame_y,S(w.frame_x+w.button_width),S(w.frame_y+w.button_height)};}
 else if(object==-4){if(!item)i.rect=w.client;else{auto&v=m->items[item-1];i.rect={S(w.client.left+v.x),S(w.client.top+v.y),S(w.client.left+v.x+v.width),S(w.client.top+v.y+v.height)};}}
 else return false;
 auto focus=e.Tracking(hwnd,object==-4);bool active=focus.valid&&(object==-4?focus.popup_matches:focus.menu_bar&&(focus.system==(object==-1)));
 if(active){i.flags|=1;if(!item)i.flags|=2;else if(int(focus.selected)+1==item){i.flags|=2;i.popup=W(focus.child);}}
 return true;
}
}
