// SPDX-License-Identifier: GPL-2.0-or-later
#include "menu_family_bridge.cpp"
#include "../src/win98_menu_geometry.h"
namespace G=Win98MenuGeometry;
struct Geometry {int left,top,right,bottom,cx,cy;U special,popup_valid;int borderx,bordery;U layouts;};
struct GE:G::Environment {Env base;Geometry&g;G::Window w;
 GE(Wire&a,Geometry&b):base(a),g(b),w{{S(g.left),S(g.top),S(g.right),S(g.bottom)},S(g.cx),S(g.cy),bool(g.special)}{}
 Win98Menu::Menu*Menu(U h)override{return base.Find(h);}G::Window*FindWindow(U h)override{return h?&w:nullptr;}
 U PopupWindow(U h,U m)override{return Menu(m)&&(Menu(m)->flags&1)?g.popup_valid?1:0:h;}
 void LayoutBar(U,U)override{++g.layouts;}S BorderX()override{return S(g.borderx);}S BorderY()override{return S(g.bordery);}
};
extern "C" int menu_geometry(Wire*w,Geometry*g,U op,U h,U a,U b,U c,int*out){GE e(*w,*g);G::Rect r;int result;
 if(op==0)result=G::Next(*e.Menu(h),S(a),S(b),W(c));
 else if(op==1){result=G::ItemRect(e,a,h,W(b),r);out[0]=r.left;out[1]=r.top;out[2]=r.right;out[3]=r.bottom;}
 else if(op==2)result=G::Hit(e,h,a,S(b),S(c));else result=G::FromPoint(e,a,h,S(b),S(c));return result;}
