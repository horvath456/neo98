// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_message_box_layout.h"
using namespace Win98MessageBoxLayout;
struct Provider {
 const int*a;int*trace;unsigned count=0;
 Measure MeasureText(int width,int height){
  trace[2*count]=width;trace[2*count+1]=height;++count;
  int available=width>0?width:1;
  return {a[26]<available?a[26]:available,Word(unsigned(((a[27]+available-1)/available)*a[28]))};
 }
};
extern "C" void box_layout(const int*a,int*out,int*trace){
 Input in;in.style=a[0];in.dialog_base_x=a[1];in.dialog_base_y=a[2];in.button_width=a[3];in.caption_text_width=a[4];in.icon_width=a[5];in.icon_height=a[6];in.has_icon=a[7];in.work_rect={a[8],a[9],a[10],a[11]};in.monitor_rect={a[12],a[13],a[14],a[15]};in.frame_x=a[16];in.frame_y=a[17];in.caption_height=a[18];in.caption_button_x=a[19];in.caption_button_y=a[20];in.edge_x=a[21];in.edge_y=a[22];in.cascade_depth=a[23];
 Provider e{a,trace+1};auto r=Calculate(e,in);trace[0]=e.count;
 const int result[]={r.outer.left,r.outer.top,Sub(r.outer.right,r.outer.left),Sub(r.outer.bottom,r.outer.top),r.text_width,r.text_height,r.button_row_width,r.button_count,r.selected_bounds.left,r.selected_bounds.top,r.selected_bounds.right,r.selected_bounds.bottom,r.width_attempts,r.valid};
 for(unsigned i=0;i<14;++i)out[i]=result[i];
 for(int i=0;i<r.button_count;++i){out[14+i*4]=r.buttons[i].left;out[15+i*4]=r.buttons[i].top;out[16+i*4]=Sub(r.buttons[i].right,r.buttons[i].left);out[17+i*4]=Sub(r.buttons[i].bottom,r.buttons[i].top);}
}
extern "C" int box_button_width(const int*a){
 struct E {const int*a;int LabelLength(int i){return a[i];}int ZeroWidth(){return a[9];}int LabelWidth(int i,int){return a[10+i];}}e{a};return ButtonWidth(e,a[19]);
}
