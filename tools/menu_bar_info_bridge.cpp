// SPDX-License-Identifier: GPL-2.0-or-later
#include "menu_family_bridge.cpp"
#include "../src/win98_menu_bar_info.h"
namespace B=Win98MenuBarInfo;
struct BarWire {U valid;int left,top,right,bottom,cl,ct,cr,cb;U menu,system,popup,child,system_style,system_button;int fx,fy;U caption,bw,bh,focused,bar,system_focus,popup_focus;int selected;U child_window;};
struct BI {U size;int left,top,right,bottom;U menu,popup,flags;};
struct BE:B::Environment {Env base;BarWire&w;BE(Wire&a,BarWire&b):base(a),w(b){}bool GetWindow(U,B::Window&v)override{v={{S(w.left),S(w.top),S(w.right),S(w.bottom)},{S(w.cl),S(w.ct),S(w.cr),S(w.cb)},w.menu,w.system,w.popup,bool(w.child),bool(w.system_style),bool(w.system_button),S(w.fx),S(w.fy),W(w.caption),W(w.bw),W(w.bh)};return w.valid;}
 Win98Menu::Menu*Menu(U h)override{return base.Find(h);}B::Focus Tracking(U,bool)override{return {bool(w.focused),bool(w.bar),bool(w.system_focus),bool(w.popup_focus),S(w.selected),w.child_window};}};
extern "C" U menu_bar_info(Wire*w,BarWire*b,int object,int item,BI*out){BE e(*w,*b);B::Info i{out->size,{S(out->left),S(out->top),S(out->right),S(out->bottom)},W(out->menu),W(out->popup),W(out->flags)};U r=B::Get(e,1,object,item,i);*out={i.size,i.rect.left,i.rect.top,i.rect.right,i.rect.bottom,i.menu,i.popup,i.flags};return r;}
