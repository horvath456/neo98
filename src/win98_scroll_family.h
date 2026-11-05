// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 USER14 scrollbar records, update rules and thumb geometry.
#pragma once
namespace Win98Scroll {
using U=unsigned;using Q=unsigned long long;
inline long long Signed(U v){return v&0x80000000u?static_cast<long long>(v)-0x100000000ll:v;}
inline int Word(int v){U x=U(v)&65535;return int(x&32767)-int(x&32768);}
struct Info{U size,mask,minimum,maximum,page,position,track;};
struct Bar{U minimum=0,maximum=100,page=0,position=0;};
struct Window{U disabled=0;Bar bars[2];};
// USER validates 28 accessible bytes even when cbSize is 24.
inline bool Valid(const Info&i){return(i.size==24||i.size==28)&&!(i.mask&~0x701fu);}
// USER. Info is a by-value stack copy in the original.
inline bool Update(Bar&b,Info i,U&visible,U&result){
 bool changed=false;if(i.mask&0x1000)result=b.position;
 if(i.mask&1){
  if(Signed(i.minimum)>Signed(i.maximum))i.maximum=i.minimum;
  if(b.minimum!=i.minimum||b.maximum!=i.maximum){
   b.minimum=i.minimum;b.maximum=i.maximum;
   if(!(i.mask&2)){i.mask|=2;i.page=b.page;}
   if(!(i.mask&4)){i.mask|=4;i.position=b.position;}
   changed=true;
  }
 }
 if(i.mask&2){
  U range=b.maximum-b.minimum;if(range&0x80000000u)range=0-range;++range;
  if(i.page>range)i.page=range;
  if(i.page!=b.page){b.page=i.page;if(!(i.mask&4)){i.mask|=4;i.position=b.position;}changed=true;}
 }
 if(i.mask&4){
  U top=b.maximum-(b.page?b.page-1:0);
  if(Signed(i.position)<Signed(b.minimum))i.position=b.minimum;
  else if(Signed(i.position)>Signed(top))i.position=top;
  if(i.position!=b.position){b.position=i.position;changed=true;}
 }
 if(!(i.mask&0x1000))result=b.position;
 if(i.mask&1){visible=b.minimum!=b.maximum;if(!visible)return changed;}
 else if(!(i.mask&2))return changed;
 visible=Signed(b.maximum-b.minimum)>=Signed(b.page);return changed;
}
// USER. Size/mask validation belongs to the public wrapper.
inline U Query(const Bar&b,Info&i,bool tracking=false,U tracked=0){
 if(i.mask&1){i.minimum=b.minimum;i.maximum=b.maximum;}
 if(i.mask&2)i.page=b.page;if(i.mask&4)i.position=b.position;
 if(i.mask&16)i.track=tracking?tracked:b.position;
 return(i.mask&0x17)!=0;
}
// USER and1285..12A7. Disable flags accumulate; zero clears.
inline bool Enable(Window&w,U bar,U flags){
 U before=w.disabled;
 if(bar==0||bar==3){if(flags)w.disabled|=flags;else w.disabled&=~3u;}
 if(bar==1||bar==3){if(flags)w.disabled|=flags<<2;else w.disabled&=~12u;}
 return before!=w.disabled;
}
inline U StyleMask(U bar){return bar==0?0x100000u:bar==1?0x200000u:bar==3?0x300000u:0;}
struct Geometry{int start,end,cross_start,cross_end,arrow,track_start,track_end,thumb_start,thumb_end;};
inline bool Ratio(U a,U b,U denominator,U&out){
 if(!denominator){out=a;return true;}Q value=(Q(a)*b+denominator/2)/denominator;
 if(value>0xffffffffu)return false;out=U(value);return true;
}
// USER plus actual unsigned rounded helper USER.
inline bool Calculate(const Bar&b,int start,int end,int cross_start,int cross_end,int metric,Geometry&g){
 start=Word(start);end=Word(end);cross_start=Word(cross_start);cross_end=Word(cross_end);metric=Word(metric);
 int half=Word(end-start)/2,arrow=half<metric?half:metric;
 int track_start=Word(start+arrow),track_end=Word(end-arrow),thumb=metric;
 U range=b.maximum-b.minimum+1,value;
 if(b.page){if(!Ratio(U(track_end-track_start),b.page,range,value))return false;thumb=Word(int(value));if(thumb<=metric/2)thumb=metric/2;}
 int travel=Word(Word(end-Word(start+arrow))-arrow-thumb);
 if(!Ratio(b.position-b.minimum,U(travel),range-(b.page?b.page:1),value))return false;
 int thumb_start=Word(int(value+U(track_start)));
 g={start,end,cross_start,cross_end,arrow,track_start,track_end,thumb_start,Word(thumb_start+thumb)};return true;
}
}
namespace Win98Scroll {
// Original local-heap ownership is represented explicitly for native allocation
// and differential tests. Paint/layout/DC providers return no guest callbacks here.
struct Owner{U allocated=0,style=0,nonclient=0;Window records;};
struct Effects{U result=0,allocate=0,release=0,layout=0,paint=0,dc_acquire=0,dc_release=0;};
inline bool Allocate(Owner&o,Effects&e,bool okay){
 if(o.allocated)return true;++e.allocate;if(!okay)return false;
 o.records=Window{};o.allocated=1;return true;
}
inline void EnableWindow(Owner&o,U bar,U flags,bool visible,bool allocate_ok,bool dc_ok,Effects&e){
 if(!o.allocated&&!flags){e.result=0;return;}
 if(!Allocate(o,e,allocate_ok)){e.result=0;return;}
 ++e.dc_acquire;if(!dc_ok){e.result=0;return;}
 U before=o.records.disabled;Enable(o.records,bar,flags);
 if(visible&&!(o.style&0x20000000u)){
  if((bar==0||bar==3)&&(o.nonclient&4)&&((before^o.records.disabled)&3))++e.paint;
  if((bar==1||bar==3)&&(o.nonclient&2)&&((before^o.records.disabled)&12))++e.paint;
 }
 ++e.dc_release;e.result=before!=o.records.disabled;
}
// USER, ordinary nonclient bars. DC/painter, accessibility and
// active mouse-tracking services are separate providers.
inline Effects Set(Owner&o,U bar,Info i,bool redraw,bool visible,bool allocate_ok=true,bool dc_ok=true){
 Effects e;U vertical=bar!=0,bit=vertical?0x200000u:0x100000u;
 U before=(o.style&bit)!=0,shown=before;bool fresh=!o.allocated;
 if(!(i.mask&1)&&!before&&!o.allocated)return e;
 if(!Allocate(o,e,allocate_ok))return e;
 U result=0;bool changed=Update(o.records.bars[vertical],i,shown,result);
 if(!changed&&!fresh){if(before&&redraw&&visible)++e.paint;e.result=result;return e;}
 o.style&=~bit;if(shown)o.style|=bit;
 else if(!(o.style&0x300000u)&&o.records.bars[0].minimum==o.records.bars[0].maximum&&o.records.bars[1].minimum==o.records.bars[1].maximum){o.allocated=0;++e.release;}
 if(i.mask&8){
  if(before){o.style|=bit;EnableWindow(o,bar,shown?0:3,visible,allocate_ok,dc_ok,e);}
 }else if(shown!=before){++e.layout;e.result=result;return e;}
 if(shown&&redraw&&visible&&(o.nonclient&(vertical?2:4)))++e.paint;
 e.result=result;return e;
}
// USER. Show succeeds even if its optional allocation fails.
inline Effects Show(Owner&o,U bar,bool show,bool allocate_ok=true){
 Effects e;e.result=1;U bits=StyleMask(bar),before=o.style;
 if(show){o.style|=bits;Allocate(o,e,allocate_ok);}else o.style&=~bits;
 e.layout=o.style!=before;return e;
}
}
namespace Win98Scroll {
// Unlike the Win16 exports,
// these wrappers transport full DWORD positions and range endpoints.
template<class E>U SetPosition(E&e,U h,U bar,U position,U redraw){
 Info i{28,0x1004,0,0,0,position,0};return e.SetInfo(h,bar,i,redraw);
}
template<class E>U SetRange(E&e,U h,U bar,U minimum,U maximum,U redraw){
 if(bar>2||!e.ValidWindow(h))return 0;Info i{28,1,minimum,maximum,0,0,0};e.SetInfo(h,bar,i,redraw);return 1;
}
template<class E>U GetRange(E&e,U h,U bar,U&minimum,U&maximum){
 Info i{28,1,0,0,0,0,0};U result=e.GetInfo(h,bar,i);minimum=i.minimum;maximum=i.maximum;return result;
}
}
namespace Win98Scroll {
// USER's pressed-page state: hit code 2 or 3, the tracked window and bar, and
// the rectangle the tracker stored at USER data 0748h. DrawTrack clamps one of
// its edges against the current thumb before inverting it.
struct Press{U part=0,window=0,vertical=0;int left=0,top=0,right=0,bottom=0;};
// USER in full. Track(a,z) is the fill helper, Thumb(a,z)
// is the shared button frame USER called with state and flags zero, and
// Invert is USER, a DSTINVERT PatBlt over the whole stored rectangle.
template<class E>void DrawTrack(E&e,const Geometry&g,U disabled,U vertical=0,U window=0,Press*press=nullptr){
 if(g.start>=g.end||g.cross_end<=g.cross_start)return;
 if((disabled&3)==3||Word(g.track_end-g.track_start)<Word(g.thumb_end-g.thumb_start)){
  e.Track(g.track_start,g.track_end);return;
 }
 if(g.track_start<g.thumb_start)e.Track(g.track_start,g.thumb_start);
 if(g.thumb_end<g.track_end)e.Track(g.thumb_end,g.track_end);
 e.Thumb(g.thumb_start,g.thumb_end);
 if(!press||(press->part!=2&&press->part!=3))return;
 if(press->window!=window||press->vertical!=vertical)return;
 int&start=vertical?press->top:press->left;
 int&end=vertical?press->bottom:press->right;
 if(press->part==2)end=g.thumb_start;else start=g.thumb_end;
 if(end<=start)return;
 e.Invert(*press);
}
// USER. A scrollbar background brush other than COLOR_BTNHIGHLIGHT's and
// other than USER's own dither brush first gets a flat sunken edge on the two
// long sides, and the fill then uses the adjusted rectangle.
inline bool TrackEdge(U brush,U highlight_brush,U dither_brush,U vertical,U&edge,U&flags){
 if(brush==highlight_brush||brush==dither_brush)return false;
 edge=0x000a;                       // EDGE_SUNKEN
 flags=0x6000u|(vertical>=1?0x5u:0xau);   // BF_ADJUST|BF_FLAT plus the long sides
 return true;
}
}
