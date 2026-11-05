// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98IndirectDialog {
using U=unsigned;
// USER, after template decoding and compatibility preparation.
inline void Styles(U&style,U&extended,bool win40){
 if(style&0x80)extended|=0x101;
 if((style&0x2000)&&win40)extended|=0x400;
 if(style&0x400)style&=0xff37fffdu;
 else if(style&0x400000)extended|=0x100;
 if(style&2)extended|=8;
 if(!(style&0x40000000)||(style&0x400))extended|=0x10000;
}
// USER: desktop is no modal owner; a child owner becomes
// its top-level parent. Parent walking is the existing native USER provider.
// USER places the dialog window, Windows 98 keeps it on the monitor.
// The template position is already in pixels (MulDiv by the base units); width and height are the outer size after AdjustWindowRectEx.
struct Rect{int left,top,right,bottom;};
struct Placement{
 U style=0,extended=0;bool win40=false,mouse=true;
 bool owner=false;int owner_x=0,owner_y=0;                   // owner client origin, ClientToScreen
 int cursor_x=0,cursor_y=0;Rect monitor{},work{},screen{};    // monitor record +10h and +18h, USER 11B2h
 int kanji=0;                                                 // SM_CYKANJIWINDOW, USER [2FCh]
 int margin=4;                                                // USER [6EAh], set to 4
};
inline void Place(const Placement&p,int x,int y,int width,int height,int&left,int&top){
 Rect r{};
 if((p.style&0x1000)&&p.mouse&&p.win40){            // DS_CENTERMOUSE
  r.left=p.cursor_x-width/2;r.top=p.cursor_y-height/2;
 }else if((p.style&0x1800)&&p.win40){               // DS_CENTER: middle of the work area
  r.left=(p.work.left+p.work.right-width)/2;r.top=(p.work.top+p.work.bottom-height)/2;
 }else{
  int bx=0,by=0;
  // relative to the owner's client area; a WS_CHILD dialog (
  // tests its own style) keeps the template position inside its parent.
  if(!(p.style&1)&&p.owner){if((p.style&0xc0000000u)!=0x40000000u){bx=p.owner_x;by=p.owner_y;}}
  else{bx=p.monitor.left;by=p.monitor.top;}                                // DS_ABSALIGN or no owner:
  r.left=bx+x;r.top=by+y;
 }
 r.right=r.left+width;r.bottom=r.top+height;
 // a child with WS_EX_CONTROLPARENT stays where it is; any other
 // child keeps to the screen, a tool window to the monitor, the rest to the
 // work area. Bottom first, then top, right, left, so the top left corner wins.
 const Rect*area=nullptr;
 if(p.style&0x40000000){if(!(p.extended&0x10000))area=&p.screen;}
 else area=(p.extended&0x80)?&p.monitor:&p.work;
 if(area){
  auto move=[&](int dx,int dy){r.left+=dx;r.right+=dx;r.top+=dy;r.bottom+=dy;};
  const int limit=area->bottom-p.kanji-p.margin;
  if(limit<r.bottom)move(0,limit-r.bottom);
  if(area->top>r.top)move(0,area->top-r.top);
  if(area->right<r.right)move(area->right-r.right,0);
  if(area->left>r.left)move(area->left-r.left,0);
 }
 left=r.left;top=r.top;
}
template<class E> U ModalOwner(E&e,U owner){if(owner==e.Desktop())owner=0;if(owner&&(e.Style(owner)&0xc0000000u)==0x40000000u)owner=e.Root(owner);return owner;}
}
