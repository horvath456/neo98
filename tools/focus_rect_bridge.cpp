// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_focus_rect.h"
extern "C" unsigned focus_rect(const int*a,int*out){
 struct P{const int*a;int*out;unsigned count=0,selected=0x1111;unsigned Select(unsigned b){auto old=selected;selected=b;return old;}Win98FocusRect::Extent Viewport(){return{a[4],a[5]};}Win98FocusRect::Extent Window(){return{a[6],a[7]};}void Pat(int x,int y,int w,int h,unsigned rop){int*p=out+1+count++*5;p[0]=x;p[1]=y;p[2]=w;p[3]=h;p[4]=int(rop);}}p{a,out};
 unsigned result=Win98FocusRect::Frame(p,Win98FocusRect::Thunk({a[0],a[1],a[2],a[3]}),0x2222);out[0]=p.count;return result;
}
extern "C" unsigned focus_pattern(unsigned x,unsigned y){return Win98FocusRect::PatternBit(x,y);}
