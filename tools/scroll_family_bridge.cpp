// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_scroll_family.h"
extern "C" unsigned scroll_update(Win98Scroll::Bar*b,Win98Scroll::Info*i,unsigned*v,unsigned*r){return Win98Scroll::Update(*b,*i,*v,*r);}
extern "C" unsigned scroll_query(Win98Scroll::Bar*b,Win98Scroll::Info*i,unsigned tracking,unsigned tracked){return Win98Scroll::Query(*b,*i,tracking,tracked);}
extern "C" unsigned scroll_valid(Win98Scroll::Info*i){return Win98Scroll::Valid(*i);}
extern "C" unsigned scroll_geometry(Win98Scroll::Bar*b,int *rect,unsigned vertical,int metric,Win98Scroll::Geometry*g){return Win98Scroll::Calculate(*b,rect[vertical?1:0],rect[vertical?3:2],rect[vertical?0:1],rect[vertical?2:3],metric,*g);}
extern "C" void scroll_owner(Win98Scroll::Owner*o,unsigned op,unsigned bar,Win98Scroll::Info*i,unsigned value,unsigned visible,unsigned alloc_ok,unsigned dc_ok,Win98Scroll::Effects*e){
 if(op==0)*e=Win98Scroll::Set(*o,bar,*i,value&65535,visible,alloc_ok,dc_ok);
 if(op==1)*e=Win98Scroll::Show(*o,bar,value&65535,alloc_ok);
 if(op==2){*e={};Win98Scroll::EnableWindow(*o,bar,value&65535,visible,alloc_ok,dc_ok,*e);}
}
struct ScrollWrapper {
 unsigned okay,result,minimum,maximum,calls[12]={};
 bool ValidWindow(unsigned h){calls[0]=h;return okay;}
 unsigned SetInfo(unsigned h,unsigned b,Win98Scroll::Info&i,unsigned r){calls[1]=h;calls[2]=b;calls[3]=r;calls[4]=i.size;calls[5]=i.mask;calls[6]=i.minimum;calls[7]=i.maximum;calls[8]=i.position;return result;}
 unsigned GetInfo(unsigned h,unsigned b,Win98Scroll::Info&i){SetInfo(h,b,i,0);if(okay){i.minimum=minimum;i.maximum=maximum;}return result;}
};
extern "C" unsigned scroll_wrapper(unsigned op,unsigned*a,ScrollWrapper*e,unsigned*low,unsigned*high){
 if(op==0)return Win98Scroll::SetPosition(*e,a[0],a[1],a[2],a[3]);
 if(op==1)return Win98Scroll::SetRange(*e,a[0],a[1],a[2],a[3],a[4]);
 return Win98Scroll::GetRange(*e,a[0],a[1],*low,*high);
}
struct ScrollDraw {
 unsigned count=0;int entries[15]={};
 const Win98Scroll::Geometry&g;unsigned vertical;
 void Add(int kind,int a,int z){int*p=entries+5*count++;p[0]=kind;p[1]=vertical?g.cross_start:a;p[2]=vertical?a:g.cross_start;p[3]=vertical?g.cross_end:z;p[4]=vertical?z:g.cross_end;}
 void Track(int a,int z){Add(0,a,z);}void Thumb(int a,int z){Add(1,a,z);}
 // Only reached with a pressed page; scroll_draw passes none.
 void Invert(const Win98Scroll::Press&){}
};
extern "C" unsigned scroll_draw(Win98Scroll::Geometry*g,unsigned vertical,unsigned disabled,int*out){ScrollDraw e{0,{},*g,vertical};Win98Scroll::DrawTrack(e,*g,disabled);for(unsigned i=0;i<15;++i)out[i]=e.entries[i];return e.count;}
