// SPDX-License-Identifier: GPL-2.0-or-later
// Native USER2 message-box geometry. Native fonts, text measurement,
// monitor selection and window creation are supplied separately.
#pragma once
namespace Win98MessageBoxLayout {
using U=unsigned;
inline int Word(U value){return int(value&32767)-int(value&32768);}
inline int Add(int a,int b){return Word(U(a)+U(b));}
inline int Sub(int a,int b){return Word(U(a)-U(b));}
inline int Times(int a,int b){return Word(U(a)*U(b));}
inline int Half(int value){return Word(value)/2;}
struct Rect {int left=0,top=0,right=0,bottom=0;};
struct Measure {int width=0,height=0;};
struct Input {
 U style=0;
 int dialog_base_x=0,dialog_base_y=0,button_width=0,caption_text_width=0;
 int icon_width=0,icon_height=0;bool has_icon=false;
 Rect work_rect,monitor_rect;
 int frame_x=0,frame_y=0,caption_height=0;
 int caption_button_x=0,caption_button_y=0,edge_x=0,edge_y=0,cascade_depth=0;
};
struct Result {
 bool valid=false;int width_attempts=0,button_count=0;
 Rect outer,absolute_outer,client,text,icon,buttons[4];
 int button_row_width=0,text_width=0,text_height=0;
 Rect selected_bounds;
};
// USER adds unsigned denominator/2 even for a negative product.
// Native arithmetic failure replaces the original overflowing IDIV exception.
struct Arithmetic {
 bool valid=true;
 int Scale(int value,int numerator,int denominator){
  value=Word(value);numerator=Word(numerator);denominator=Word(denominator);
  if(!denominator)return value;
  const long long product=static_cast<long long>(value)*numerator+((U(denominator)&65535)>>1);
  const long long result=product/denominator;
  if(result < -32768 || result > 32767){valid=false;return 0;}return int(result);
 }
};
// USER includes internal types6/7. Type7 has no base button.
inline constexpr unsigned char Counts[]={1,2,3,3,2,2,1,0};
inline constexpr unsigned char Starts[]={0,2,5,12,9,16,1,2};
inline Rect Area(int x,int y,int width,int height){return {x,y,Add(x,width),Add(y,height)};}
// Provider supplies LabelLength(index), LabelWidth(index,length) with original
// mnemonic-prefix handling, and ZeroWidth. USER selects the
// longest byte string first; it does not choose the visually widest label.
template<class E> int ButtonWidth(E&e,int dialog_base_x,bool*okay=nullptr){
 int longest=0,index=0;
 for(int i=0;i<9;++i){int length=Word(e.LabelLength(i));if(length>longest){longest=length;index=i;}}
 int estimate=Add(Times(Sub(Add(Word(e.ZeroWidth()),longest),1),2),Word(e.LabelWidth(index,longest)));
 Arithmetic arithmetic;int minimum=arithmetic.Scale(50,dialog_base_x,4);
 if(okay)*okay=arithmetic.valid;
 return estimate>minimum?estimate:minimum;
}
// Provider MeasureText(width,height_limit) returns the modified DrawText
// rectangle's width and function height, both in pixels. Original flags2E50.
template<class E> Result Calculate(E&e,const Input&in){
 Result out;const U kind=in.style&15;if(kind>7)return out;
 Arithmetic math;
 const int bx=Word(in.dialog_base_x),by=Word(in.dialog_base_y);
 const int fx=Word(in.frame_x),fy=Word(in.frame_y),caption=Word(in.caption_height);
 const int count=Counts[kind]+((in.style&0x4000)?1:0);
 const int gap=math.Scale(4,bx,4),button=Word(in.button_width);
 out.button_count=count;out.button_row_width=Add(Times(count,button),Times(count-1,gap));
 const int icon_extra=in.has_icon?Add(math.Scale(10,bx,4),Word(in.icon_width)):0;
 const int icon_height=in.has_icon?Word(in.icon_height):0;
 const int title=Add(Word(in.caption_text_width),Times(in.caption_button_x,2));
 Rect bounds={Word(in.work_rect.left),Word(in.work_rect.top),Word(in.work_rect.right),Word(in.work_rect.bottom)};
 int desired=math.Scale(Sub(bounds.right,bounds.left),5,8);
 const int padding=Times(math.Scale(7,bx,4),2);
 int outer_width=0,outer_height=0,text_width=0,text_height=0;bool completed=false;
 for(int attempt=0;attempt<3;++attempt){
  ++out.width_attempts;
  int available=Sub(desired,Times(fx,2));
  auto measured=e.MeasureText(Sub(Sub(available,padding),icon_extra),Sub(bounds.bottom,bounds.top));
  int width=Add(Add(Word(measured.width),padding),icon_extra);
  if(width<title)width=title;if(width>available)width=available;
  const int minimum=Add(padding,out.button_row_width);if(width<minimum)width=minimum;
  text_width=Sub(Sub(width,padding),icon_extra);
  measured=e.MeasureText(text_width,Sub(bounds.bottom,bounds.top));text_height=Word(measured.height);
  outer_width=Add(width,Times(fx,2));
  outer_height=Add(Add(Add(math.Scale(38,by,8),Times(fy,2)),caption),text_height>=icon_height?text_height:icon_height);
  if(outer_height<=Sub(bounds.bottom,bounds.top)){completed=true;break;}
  const int six=math.Scale(Sub(bounds.right,bounds.left),6,8);
  if(desired==math.Scale(Sub(bounds.right,bounds.left),5,8)){desired=six;continue;}
  if(desired==six){
   bounds={Word(in.monitor_rect.left),Word(in.monitor_rect.top),Word(in.monitor_rect.right),Word(in.monitor_rect.bottom)};
   desired=math.Scale(Sub(bounds.right,bounds.left),7,8);continue;
  }
  completed=true;break;
 }
 // USER. Clamps after centering are deliberately asymmetric.
 int x=Add(Times(in.caption_button_x,in.cascade_depth),Half(Add(Sub(bounds.right,outer_width),bounds.left)));
 if(x<bounds.left)x=bounds.left;
 int y=Add(Half(Sub(Add(bounds.top,bounds.bottom),outer_height)),Times(in.caption_button_y,in.cascade_depth));
 if(y<bounds.top)y=bounds.top;
 if(Add(x,outer_width)>bounds.right)x=Sub(Sub(bounds.right,outer_width),in.edge_x);
 if(Add(y,outer_height)>bounds.bottom){
  y=Sub(Sub(bounds.bottom,in.edge_y),outer_height);
  if(y<bounds.top)y=Sub(Sub(in.monitor_rect.bottom,in.edge_y),outer_height);
 }
 out.absolute_outer=Area(x,y,outer_width,outer_height);
 out.outer=Area(Sub(x,in.monitor_rect.left),Sub(y,in.monitor_rect.top),outer_width,outer_height);
 out.client=Area(0,0,Sub(outer_width,Times(fx,2)),Sub(Sub(outer_height,Times(fy,2)),caption));
 out.selected_bounds=bounds;out.text_width=text_width;out.text_height=text_height;
 const int left=math.Scale(7,bx,4),top=math.Scale(7,by,8);
 out.icon=Area(left,top,in.has_icon?Word(in.icon_width):0,icon_height);
 out.text=Area(Add(left,icon_extra),Add(top,text_height<icon_height?Half(Sub(icon_height,text_height)):0),text_width,text_height);
 // USER passes row origin/bottom to0532; that helper uses14DU.
 int button_x=Half(Sub(Sub(outer_width,out.button_row_width),Times(fx,2)));
 const int button_height=math.Scale(14,by,8);
 const int button_y=Sub(Sub(out.client.bottom,top),button_height);
 for(int i=0;i<count;++i){out.buttons[i]=Area(button_x,button_y,button,button_height);button_x=Add(button_x,Add(gap,button));}
 out.valid=math.valid&&completed;return out;
}
}
