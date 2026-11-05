// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/user_draw.h"
struct Target { unsigned char *pixels; unsigned trace=2166136261u; };
static void Fill(void *context,int x,int y,int w,int h,unsigned rgb)
{
    auto &t=*static_cast<Target *>(context);
    const unsigned values[]={(unsigned)x,(unsigned)y,(unsigned)w,(unsigned)h,rgb};
    for(unsigned value:values){
        for(unsigned i=0;i<4;++i)t.trace=(t.trace^((value>>(i*8))&255))*16777619u;
    }
    if(w<=0||h<=0)return;
    for(int row=y<0?0:y;row<y+h&&row<80;++row)for(int col=x<0?0:x;col<x+w&&col<96;++col){
        auto *p=t.pixels+(row*96+col)*4;
        for(unsigned i=0;i<4;++i)p[i]=rgb>>(i*8);
    }
}
extern "C" unsigned user_edge_probe(unsigned char *pixels,Gdi::RECT *rect,unsigned edge,unsigned flags,
    int depth,int bw,int bh,unsigned *trace)
{
    Target target={pixels};Win98Draw::Painter p={&target,Fill};p.depth=depth;p.border_width=bw;p.border_height=bh;
    auto result=Win98Draw::DrawEdge(p,rect,edge,flags);*trace=target.trace;return result;
}
static void Pattern(void *context,int x,int y,int w,int h,unsigned background,unsigned foreground)
{
    auto &t=*static_cast<Target *>(context);
    const unsigned values[]={0x504154u,(unsigned)x,(unsigned)y,(unsigned)w,(unsigned)h,background,foreground};
    for(unsigned value:values)for(unsigned i=0;i<4;++i)t.trace=(t.trace^((value>>(i*8))&255))*16777619u;
    if(w<=0||h<=0)return;
    for(int row=y<0?0:y;row<y+h&&row<80;++row)for(int col=x<0?0:x;col<x+w&&col<96;++col){
        const auto rgb=(row+col)&1?background:foreground;
        for(unsigned i=0;i<4;++i)t.pixels[(row*96+col)*4+i]=rgb>>(i*8);
    }
}
extern "C" unsigned user_button_probe(unsigned char *pixels,Gdi::RECT *rect,unsigned state,unsigned flags,
    int depth,int bw,int bh,unsigned *trace)
{
    Target target={pixels};Win98Draw::Painter p={&target,Fill};p.depth=depth;p.border_width=bw;p.border_height=bh;p.pattern=Pattern;
    auto result=Win98Draw::ButtonFrame(p,rect,state,flags);*trace=target.trace;return result;
}
