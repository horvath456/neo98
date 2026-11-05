// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_menu.h"
// Menu measurement and owner-draw/bitmap painting decisions matching
// the local Windows 98 USER.EXE (SHA256
// ac1d4c3af96f02640d0295439a1f33f49e1e7bf1cc77fc9f603be5fa354468f4).
// Segment offsets are recorded beside every routine. Fonts, device contexts,
// bitmap rasters and the window-procedure send path stay native providers.
namespace Win98MenuOwnerDraw {
using Win98Menu::U;using Win98Menu::W;using Win98Menu::S;using Win98Menu::P;
using Win98Menu::Item;using Win98Menu::Menu;
struct Size {S cx=0,cy=0;};
struct Rect {S left=0,top=0,right=0,bottom=0;};
// Win16 DRAWITEMSTRUCT field order; the 32-bit adapter widens the members.
struct Draw {U type=1,control=0,item=0,action=0,state=0,window=0,dc=0;Rect rect;U data=0;};
struct Environment {
 virtual ~Environment()=default;
 W uniform_height=0; // USER data word [0x1114]: the menu bar's shared item height.
 virtual Menu*Find(U)=0;
 // USER data words. The native provider supplies this desktop's values.
 virtual S BarHeight()=0;         // [0x34c]
 virtual S ItemHeight()=0;        // [0xbd8]
 virtual S TextPad()=0;           // [0xbe2]
 virtual S EdgePad()=0;           // [0x6e8]
 virtual S CheckWidth()=0;        // [0x96c]
 virtual S CheckHeight()=0;       // [0x96e]
 virtual S EdgeX()=0;             // [0x2e8]
 virtual S BorderX()=0;           // [0x2ec]
 virtual S BarPadX()=0;           // [0x338]
 virtual S BarPadY()=0;           // [0x33a]
 virtual S BarExtra()=0;          // [0xd12]
 virtual S SystemBitmapWidth()=0; // [0x34a]
 virtual S MdiBitmapWidth(W)=0;   // [0x9ec] table, entries 8..11
 virtual void SelectMenuFont(bool bold)=0;   // [0x1a8]/[0x1aa] and the synthesized bold fallback
 virtual bool BitmapSize(W,Size&)=0;         // GDI bitmap dimensions
 virtual U TabIndex(P)=0;                    // characters before the first tab
 virtual S TextWidth(P,U offset,U count)=0;  // text extent in the selected font
 virtual bool BackspacePrefix(P)=0;          // the original menu-bar right-justify marker
 virtual bool BarAlign(U window)=0;          // window class byte [[window+0x24]+9] & 0x10
 virtual bool ColumnGap(U window)=0;         // window byte [window+0x2e] & 0x40
 virtual void MeasureOwner(U window,W id,U data,Size&)=0; // WM_MEASUREITEM
 virtual void DrawOwner(U window,const Draw&)=0;          // WM_DRAWITEM
};
// USER. The original masks only AL, which equals a 16-bit &~7.
inline S Scale(Environment&e,S v){const S c=e.BorderX();return S(S(S(S(S(v+e.EdgeX())+c)+7)&~7)-c);}
// USER. The measured size is cached in the item's bitmap fields and
// is recomputed only while they still hold the 0xffff sentinel. The original
// leaves MEASUREITEMSTRUCT.itemWidth uninitialized on the stack; the native
// provider passes zero.
inline void MeasureOwnerItem(Environment&e,U window,Item&i){
 if(i.bitmap_width!=0xffff)return;
 Size s{0,e.ItemHeight()};e.MeasureOwner(window,i.id,i.data,s);
 i.bitmap_width=W(s.cx);i.bitmap_height=W(s.cy);
}
// USER. Measures one item and returns the original's owner-draw
// answer. Owner-draw items skip the text, check-column and bitmap accounting.
inline bool MeasureItem(Environment&e,Menu&m,Item&i,U window,bool popup,Size&out){
 S height=popup?0:e.BarHeight(),width_extra=0,height_extra=0,content=0;
 const bool bold=(i.state&0x1000)!=0;if(bold)e.SelectMenuFont(true);
 bool measured=false;
 if(i.type&0x100){
  MeasureOwnerItem(e,window,i);content=S(i.bitmap_width);
  if(popup){height=S(i.bitmap_height);content=S(content+S(e.TextPad()*2));}
  measured=true;
 }else if(i.bitmap){
  const W b=i.bitmap;
  if(b==1)content=S(e.BarPadX()+e.SystemBitmapWidth());
  else if(b==2||b==3||(b>=5&&b<=7))content=e.SystemBitmapWidth();
  else if(b>=8&&b<=11)content=e.MdiBitmapWidth(b);
  else{
   if(b==0xffff)MeasureOwnerItem(e,window,i);
   else{
    if(i.bitmap_width==0xffff){Size s{};e.BitmapSize(b,s);i.bitmap_width=W(s.cx);i.bitmap_height=W(s.cy);}
    if(!popup){width_extra=1;height_extra=1;}
   }
   content=S(i.bitmap_width);
   if(popup)height=S(i.bitmap_height);
   else{const S v=S(S(i.bitmap_height)+width_extra);if(v>height)height=v;content=S(content+height_extra);}
  }
  if(S(m.scroll)<content)m.scroll=W(content); // USER, the check/bitmap column accumulator
 }
 if(!measured){
  if(i.text){
   if(popup){const S v=S(S(e.BarPadY()+e.BarExtra())+e.ItemHeight());if(v>height)height=v;}
   const U label=e.TabIndex(i.text);
   if(content)content=S(content+e.EdgePad());
   content=S(content+e.TextWidth(i.text,0,label));
  }
  if(popup){
   content=S(content+2);
   content=S(content+((m.flags&8)?e.EdgePad():Scale(e,e.CheckWidth())));
   height=S(height+2);
  }
 }
 if(bold)e.SelectMenuFont(false);
 out={content,height};return (i.type&0x100)!=0;
}
// USER. Gives one popup column a common width, records the
// accelerator origin of every item and returns the column's right edge.
inline S FinishColumn(Environment&e,Menu&m,S x,S width,S first,S last){
 const S right=S(x+width);S max_right=0;
 if(U(W(first))>=m.count||W(last)<W(first))return 0;
 W owner=0;
 for(S n=first;W(n)<W(last);++n){
  Item&i=m.items[W(n)];S accel=0;i.layout22=W(right);
  if(i.text){
   const U label=e.TabIndex(i.text);
   if(S(label)<S(i.length))accel=S(e.TextWidth(i.text,label+1,U(i.length-label-1))+e.TextPad());
  }
  if(i.type&0x100)++owner;
  accel=S(accel+right);if(accel>max_right)max_right=accel;
 }
 S result=max_right;
 if(W(S(last-S(owner)))!=W(first)){
  result=S(result+e.TextPad());
  result=S(result+((m.flags&8)?e.EdgePad():e.CheckWidth()));
 }
 const S item_width=S(result-x);
 for(S n=first;W(n)<W(last);++n)m.items[W(n)].width=W(item_width);
 return result;
}
// USER. Lays out a whole menu: separators, owner-draw and bitmap
// items, MF_MENUBREAK/MF_MENUBARBREAK popup columns, menu-bar rows with the
// original backspace right-justify marker, and the check/bitmap column width.
inline S MeasureMenu(Environment&e,U handle,U window,S bar_width,S start_x,S start_y){
 Menu*found=e.Find(handle);if(!found)return 0;Menu&m=*found;
 m.window=window;if(!m.count)return 0;
 e.SelectMenuFont(false);
 m.flags=W(m.flags&~4);
 const bool popup=(m.flags&1)!=0;
 if(popup)e.uniform_height=0;else if(window)m.width=W(bar_width);
 const S top=start_y;S row_y=start_y,x=start_x,total=0,col_width=0,row_height=0,col_first=0;
 bool need_column=false,saw_owner=false;
 m.scroll=0;
 for(W n=0;n<m.count;++n){
  Item&i=m.items[n];
  if((i.type&0x800)&&!(i.type&0x100)){i.width=0;i.height=W(S(e.BarHeight()/2));}
  else{
   if(i.bitmap&&i.text)need_column=true;
   Size s{};if(MeasureItem(e,m,i,window,popup,s))saw_owner=true;
   i.width=W(s.cx);i.height=W(s.cy);
   if(!popup&&(!i.bitmap||i.text)){
    i.width=W(S(i.width)+S(e.TextPad()*2));
    if(e.BarAlign(window)&&!(i.type&0x4000))i.width=W((i.width+7)&0xfff8);
   }
  }
  if(e.uniform_height)i.height=e.uniform_height;
  if(!n)row_height=S(i.height);
  if(popup){
   const W brk=W(i.type&0x60);const bool last=W(n+1)==W(m.count);
   if(brk||last){
    S colw=col_width;
    if(last&&S(i.width)>col_width)colw=S(i.width);
    colw=FinishColumn(e,m,x,colw,col_first,S(brk?n:W(n+1)));
    if(brk){
     x=S(colw+((saw_owner&&!e.ColumnGap(window))?e.EdgeX():e.EdgePad()));
     col_width=S(i.width);row_y=top;col_first=S(n);
     if(last)colw=FinishColumn(e,m,x,col_width,S(n),S(W(n+1)));
    }
    if(last)m.width=W(colw);
   }
   i.x=W(x);i.y=W(row_y);
   row_y=S(row_y+S(i.height));if(row_y>total)total=row_y;
  }else{
   if(i.text&&e.BackspacePrefix(i.text))i.width=W(S(i.width)-e.TextPad());
   if((i.type&0x60)||(S(S(S(i.width)+x)+e.TextPad())>S(m.width)&&n)){
    row_y=S(row_y+row_height);x=start_x;row_height=S(i.height);m.flags|=4;
   }
   i.y=W(row_y);i.x=W(x);x=S(x+S(i.width));
  }
  if(S(i.width)>col_width)col_width=S(i.width);
  if(S(i.height)!=row_height){
   if(S(i.height)>row_height)row_height=S(i.height);
   if(!popup)e.uniform_height=W(row_height);
  }
  if(!popup)total=S(row_height+row_y);
 }
 if(!need_column)m.scroll=0;
 else if(m.flags&8)m.scroll=W(S(m.scroll)+e.EdgePad());
 else if(m.flags&0x400){
  if(S(m.scroll)<e.CheckWidth())m.scroll=W(e.CheckWidth());
  m.scroll=W(S(m.scroll)+e.EdgePad());
 }else m.scroll=W(S(m.scroll)+Scale(e,e.CheckWidth()));
 e.SelectMenuFont(false);
 m.height=W(total-start_y);return S(m.height);
}
// USER. The first MFT_RIGHTJUSTIFY (MF_HELP, 4000h) item and all
// items after it move to the right end of the bar, packed from the last item
// backwards; an item never moves left. A leading 08h in the text does not
// right-justify: only takes one text pad off its width and skips
// the character when drawing.
inline void RightJustify(Menu&m){
 S first=0;while(first<S(m.count)&&!(m.items[W(first)].type&0x4000))++first;
 if(first>=S(m.count))return;
 S right=S(S(m.items[0].x)+S(m.width));
 for(S n=S(S(m.count)-1);n>=first;--n){Item&i=m.items[W(n)];right=S(right-S(i.width));if(right>S(i.x))i.x=W(right);}
}
// USER. The menu bar measurement: clear the shared item height at
// [1114h], measure, measure a menu bar once more when a row settled on a
// common height, then right-justify. The caller adds the border.
inline S MeasureBar(Environment&e,U handle,U window,S bar_width,S start_x,S start_y){
 e.uniform_height=0;
 S height=MeasureMenu(e,handle,window,bar_width,start_x,start_y);
 Menu*m=e.Find(handle);if(!m||(m->flags&1))return height;
 if(e.uniform_height){height=MeasureMenu(e,handle,window,bar_width,start_x,start_y);m=e.Find(handle);if(!m)return height;}
 RightJustify(*m);return height;
}
// USER. Builds DRAWITEMSTRUCT and sends WM_DRAWITEM. On the menu
// bar (centered) the original takes the rectangle from the owner-measured size
// and ignores the item's own y origin.
inline void DrawOwnerItem(Environment&e,Menu*m,U menu,const Item&i,bool centered,S x,U action,U owner,U dc){
 Draw d;d.type=1;d.control=0;d.item=i.id;d.action=action;
 W state=0;
 if(m&&(m->flags&0x200))state|=0x80;   // ODS_INACTIVE
 if(i.state&1)state|=2;                // ODS_GRAYED
 if(i.state&3)state|=4;                // ODS_DISABLED
 if(i.state&8)state|=8;                // ODS_CHECKED
 if(i.state&0x80)state|=1;             // ODS_SELECTED
 if(i.state&0x100)state|=0x40;         // ODS_HOTLIGHT
 if(i.state&0x1000)state|=0x20;        // ODS_DEFAULT
 d.state=state;
 d.window=m?m->handle:W(menu);
 d.dc=dc;
 S t=S(i.y);
 if(centered)t=S(S(S(i.height)-S(i.bitmap_height))/2);
 d.rect.left=S(S(i.x)+x);d.rect.top=t;
 d.rect.right=S(S(centered?S(i.bitmap_width):S(i.width))+S(S(i.x)+x));
 d.rect.bottom=S(S(centered?S(i.bitmap_height):S(i.height))+t);
 d.data=i.data;
 e.DrawOwner(owner,d);
}
// USER. Chooses the check-mark bitmap and its vertical offset:
// 0 falls back to the system check (or the radio bullet for MFT_RADIOCHECK)
// and only while the item is checked, 0xffff and the reserved handles below
// twelve draw nothing, and every other handle is blitted at the check width.
// Returns 0 for nothing, 1 for a system bitmap and 2 for the item's own; the
// original BOOL is 1 in both drawing cases.
inline U CheckMark(Environment&e,const Item&i,W&bitmap,S&offset,bool&radio){
 offset=S(S(i.height)-e.CheckHeight());if(offset<0)offset=0;offset=S(offset/2);
 radio=false;bitmap=(i.state&8)?i.checked:i.unchecked;
 if(!bitmap){if(!(i.state&8))return 0;radio=(i.type&0x200)!=0;return 1;}
 if(bitmap==0xffff||bitmap<12)return 0;
 return 2;
}
}
