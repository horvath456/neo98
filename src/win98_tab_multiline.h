// SPDX-License-Identifier: GPL-2.0-or-later
// Original upper TCS_MULTILINE geometry: COMCTL32, /79AC,
// /7FB7. Font measurement and repaint/scrolling are provider boundaries.
#pragma once
#include "win98_tab.h"
namespace Win98Tab {
template<class E> bool CarryRow(E&e,U t,int end,int source,int repeat,int bx){
 for(;;){U item=Item(e,t,source),last=Item(e,t,end);int width=e.Read32(item+8)-e.Read32(item);
  if(int(e.Read32(t+0x50)-e.Read32(last+8))<width+2*bx)return false;
  U next=Item(e,t,source+1);int delta=e.Read32(item)-e.Read32(next);
  e.Write32(item,e.Read32(item)-delta);e.Write32(item+8,e.Read32(item+8)-delta);
  e.Write32(item+4,e.Read32(last+4));e.Write32(item+12,e.Read32(last+12));e.Write32(item+0x28,e.Read32(last+0x28));
  for(int i=source+1;i<=end;++i){last=Item(e,t,i);e.Write32(last,e.Read32(last)+width);e.Write32(last+8,e.Read32(last+8)+width);}
  if(!repeat)return true;
  int row=e.Read32(last+0x28);if(row<=1)return false;
  int previous=source;end=source-1;source=end;
  if(previous){for(;;){U candidate=Item(e,t,source);if(int(e.Read32(candidate+0x28))!=row-1)break;if(source<=0)return false;--source;}}
  --repeat;
 }
}
template<class E> void BalanceRows(E&e,U t,int bx){
 int count=Count(e,t);if(!count)return;int source=count-2;
 while(int(e.Read32(Item(e,t,source)+0x28))==int(e.Read32(t+0x70))){if(source<=0)return;--source;}
 if(!source)return;
 int repeat=0;
 do{U last=Item(e,t,count-1),item=Item(e,t,source);int width=e.Read32(t+0x50),unused=width-int(e.Read32(last+8));
  if(!unused||unused<=width/8-int(e.Read32(item+8))+width)return;
  if(!CarryRow(e,t,count-1,source,repeat++,bx))return;
 }while(--source);
}
// all visible tabs in each row absorb equal slack, followed by one
// extra pixel for each nonzero remainder step. Hidden items do not absorb it.
template<class E> bool StretchRows(E&e,U t,int bx){
 if(int(e.Read32(t+0x70))<1)return true;BalanceRows(e,t,bx);
 U count=Count(e,t),start=0;
 while(start<count){U end=start,row=e.Read32(Item(e,t,start)+0x28),visible=0;
  while(end<count&&e.Read32(Item(e,t,end)+0x28)==row){if(!(e.Read32(Item(e,t,end)+0x30)&4))++visible;++end;}
  // Original IDIV faults for a completely hidden row. Return a provider failure
  // rather than claiming successful layout or dividing by zero in native C++.
  if(!visible)return false;
  int slack=e.Read32(t+0x50)-e.Read32(Item(e,t,end-1)+8)-bx;
  int extra=slack/int(visible),remainder=slack%int(visible),shift=0;
  for(U i=start;i<end;++i){U item=Item(e,t,i);if(e.Read32(item+0x30)&4)continue;
   e.Write32(item+0x14,e.Read32(item+0x14)+extra/2);e.Write32(item+0x20,e.Read32(item+0x20)+extra/2);
   e.Write32(item,e.Read32(item)+shift);shift+=extra+(remainder!=0);if(remainder)--remainder;
   e.Write32(item+8,e.Read32(item+8)+shift);
  }start=end;
 }return true;
}
// rotate the selected row to the row adjacent to the page, keeping
// rows above it in place and moving following rows up by one row height.
template<class E> void MoveRow(E&e,U t,U row){
 U last=e.Read32(t+0x70);if(row==last)return;
 int height=e.Read32(t+0x4c);
 for(U n=Count(e,t);n;){U item=Item(e,t,--n),old=e.Read32(item+0x28);int dy=0;
  if(int(old)>int(row)){e.Write32(item+0x28,old-1);dy=-height;}
  else if(old==row){e.Write32(item+0x28,last);dy=(last-row)*height;}
  else continue;
  e.Write32(item+4,e.Read32(item+4)+dy);e.Write32(item+12,e.Read32(item+12)+dy);
 }
}
// non-image, non-button metrics; item placement. Original
// equality-at-edge and oversized-item exceptions are preserved.
template<class E> bool MultilineLayout(E&e,U t,const Metrics&m,int width){
 int px=e.Read32(t+0x60),py=e.Read32(t+0x64),height=e.Read32(t+0x6c),minimum=e.Read32(t+0x48);
 if(!(e.Read32(t+0x9c)&1)){minimum=2*(3*m.average+px);e.Write32(t+0x48,minimum);}
 int baseline=2*m.border_y-1;
 if(!height){int spacing=2*py;if(spacing<2*m.border_y)spacing=2*m.border_y;baseline=spacing-m.border_y-1;height=m.height+spacing-1;}
 e.Write32(t+0x4c,height);e.Write32(t+0x54,m.height+2*m.border_y);e.Write32(t+0x50,width);e.Write32(t+0x98,m.height);
 e.Write32(t+0x78,(height-m.height-baseline+1)/2);e.Write32(t+0x7c,(height-baseline)/2);
 U count=Count(e,t),row=0;int x=m.border_x,y=m.border_y,limit=width-m.border_x;
 for(U i=0;i<count;++i){U item=Item(e,t,i);int text=e.TextWidth(t,item),w=(e.Read32(t+8)&0x400)?int(e.Read32(t+0x68)):text+2*px;
  e.Write32(item+0x1c,text);if(!(e.Read32(t+8)&0x400)&&w<minimum)w=minimum;if(e.Read32(item+0x30)&4)w=0;
  if(x>limit||(x+w>=limit&&w<limit)){x=m.border_x;y+=height;++row;}
  e.Write32(item+0x28,row);e.Write32(item,x);e.Write32(item+4,y);x+=w;e.Write32(item+8,x);e.Write32(item+12,y+height);
  int offset=e.Read32(t+8)&0x400?(w-text)/2:px;if(offset<m.border_x)offset=m.border_x;
  e.Write32(item+0x20,offset);e.Write32(item+0x14,offset);
  e.Write32(item+0x24,py-m.border_y/2+int(e.Read32(t+0x7c)));e.Write32(item+0x18,py-m.border_y/2+int(e.Read32(t+0x78)));
 }
 e.Write32(t+0x44,x);e.Write32(t+0x70,count?row:~0u);
 if(!(e.Read32(t+8)&0xc00)&&!StretchRows(e,t,m.border_x))return false;
 U selected=e.Read32(t+0x3c);if(selected!=~0u){U item=Item(e,t,selected);if(!item)return false;e.Write32(t+0x74,~0u);MoveRow(e,t,e.Read32(item+0x28));}
 return true;
}
// choose the last item in the adjacent row whose left edge lies
// strictly before the current item's midpoint. No nearest-distance heuristic.
template<class E> U RowKey(E&e,U t,U index,U key){
 U item=Item(e,t,index);if(!item)return 0;
 int row=int(e.Read32(item+0x28))+(key==0x26?-1:1),middle=int(e.Read32(item)+e.Read32(item+8))/2;
 if(row<0||row>int(e.Read32(t+0x70)))return index;
 for(U n=Count(e,t);n;){U i=--n,p=Item(e,t,i);if(int(e.Read32(p+0x28))==row&&int(e.Read32(p))<middle)return i;}
 return index;
}

}
