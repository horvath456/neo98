// SPDX-License-Identifier: GPL-2.0-or-later
// Bridge for the original USER menu measurement and owner-draw comparison.
#include "menu_family_bridge.cpp"
#include "../src/win98_menu_ownerdraw.h"
namespace O = Win98MenuOwnerDraw;
struct OwnerWire {
 int bar,item,pad,edge,checkw,checkh,edgex,borderx,barx,bary,barextra,sysbmp,mdi[4];
 U uniform,align,gap,bitmap_w,bitmap_h,owner_w,owner_h;
 U measures,draws,bold,bitmaps,fonts;
 U m_window,m_id,m_data;
 U d_type,d_control,d_item,d_action,d_state,d_window,d_dc,d_data;
 int d_left,d_top,d_right,d_bottom;
};
struct OE : O::Environment {
 Env&base;Wire&w;OwnerWire&o;
 OE(Env&b,Wire&v,OwnerWire&c):base(b),w(v),o(c){uniform_height=W(c.uniform);}
 ~OE(){o.uniform=uniform_height;}
 Menu*Find(U h)override{return base.Find(h);}
 S BarHeight()override{return S(o.bar);}S ItemHeight()override{return S(o.item);}
 S TextPad()override{return S(o.pad);}S EdgePad()override{return S(o.edge);}
 S CheckWidth()override{return S(o.checkw);}S CheckHeight()override{return S(o.checkh);}
 S EdgeX()override{return S(o.edgex);}S BorderX()override{return S(o.borderx);}
 S BarPadX()override{return S(o.barx);}S BarPadY()override{return S(o.bary);}
 S BarExtra()override{return S(o.barextra);}S SystemBitmapWidth()override{return S(o.sysbmp);}
 S MdiBitmapWidth(W b)override{return S(o.mdi[(b-8)&3]);}
 void SelectMenuFont(bool b)override{++o.fonts;if(b)++o.bold;}
 bool BitmapSize(W,O::Size&s)override{++o.bitmaps;s={S(o.bitmap_w),S(o.bitmap_h)};return true;}
 const unsigned char*Text(P p)const{return p>=0xa000&&p<0xb000?w.text+(p-0xa000):nullptr;}
 U TabIndex(P p)override{const unsigned char*s=Text(p);if(!s)return 0;U n=0;while(s[n]&&s[n]!=9)++n;return n;}
 S TextWidth(P p,U offset,U count)override{const unsigned char*s=Text(p);if(!s)return 0;S v=0;
  for(U n=0;n<count;++n){const unsigned char c=s[offset+n];if(!c)break;v=S(v+S(5+(c&7)));}return v;}
 bool BackspacePrefix(P p)override{const unsigned char*s=Text(p);return s&&s[0]==8;}
 bool BarAlign(U)override{return o.align!=0;}
 bool ColumnGap(U)override{return o.gap!=0;}
 void MeasureOwner(U window,W id,U data,O::Size&s)override{
  ++o.measures;o.m_window=window;o.m_id=id;o.m_data=data;
  if(o.owner_w!=0xffffffffu){s.cx=S(o.owner_w);s.cy=S(o.owner_h);}
 }
 void DrawOwner(U window,const O::Draw&d)override{
  ++o.draws;o.d_type=d.type;o.d_control=d.control;o.d_item=d.item;o.d_action=d.action;
  o.d_state=d.state;o.d_window=d.window;o.d_dc=d.dc;o.d_data=d.data;o.m_window=window;
  o.d_left=d.rect.left;o.d_top=d.rect.top;o.d_right=d.rect.right;o.d_bottom=d.rect.bottom;
 }
};
extern "C" int menu_ownerdraw(Wire*w,OwnerWire*o,U op,U h,U a,U b,U c,int*out)
{
 Env e(*w);OE x(e,*w,*o);
 if(op==0){ // USER one item
  Menu*m=e.Find(h);if(!m)return 0;O::Size s{};
  const bool owner=O::MeasureItem(x,*m,m->items[W(a)],b,c!=0,s);
  out[0]=s.cx;out[1]=s.cy;return owner?0x100:0; // the original returns the masked MF_OWNERDRAW bit
 }
 if(op==1)return O::MeasureMenu(x,h,a,S(b),S(c),S(*out)); // USER whole menu
 if(op==2){ // USER owner-draw painting
  Menu*m=e.Find(h);if(!m)return 0;
  O::DrawOwnerItem(x,c?m:nullptr,c?0:h,m->items[W(a)],b!=0,S(out[0]),U(out[1]),U(out[2]),U(out[3]));
  return 0; // the original returns whatever the owner's WM_DRAWITEM answered
 }
 if(op==3)return O::Scale(x,S(h)); // USER
 if(op==4){ // USER column finish
  Menu*m=e.Find(h);if(!m)return 0;
  return O::FinishColumn(x,*m,S(a),S(b),S(c),S(out[0]));
 }
 if(op==5){ // USER check-mark selection
  Menu*m=e.Find(h);if(!m)return 0;W bitmap=0;S offset=0;bool radio=false;
  const U kind=O::CheckMark(x,m->items[W(a)],bitmap,offset,radio);
  out[0]=int(kind);out[1]=kind?offset:0;out[2]=kind==2?bitmap:kind==1&&radio?1:0;return kind?1:0;
 }
 return 0;
}
