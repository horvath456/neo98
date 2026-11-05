// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_gdi_pixel.h"
#include "../src/dibeng.h"
using namespace Win98GdiPixel;
struct Input{U width,height,depth,flags,x,y,color,rop,write,rgb565,count,palette[256];unsigned char memory[4096];U events;};
struct Provider{Input&s;DibEng::Bitmap b;Provider(Input&v):s(v),b{v.memory+128,int(v.width),int(v.height),int(((v.width*v.depth+31)/32)*4),int(v.depth)}{}
 U Palette(U i){return s.palette[i&255];}U Read32(U p){return Palette(p/4);}U Count(){return s.count;}
 U Read(U x,U y){return DibEng::ReadPixel(b,x,y);}void Write(U x,U y,U v){DibEng::WritePixel(b,x,y,v);}void Begin(Point){++s.events;}void End(){++s.events;}
};
extern "C" U pixel_driver(Input*s,U operation,U*out){Provider e{*s};if(operation==0)return DriverPixel(e,s->width,s->height,s->depth,s->flags,{int(s->x),int(s->y)},s->color,s->rop,s->write);if(operation==1)return DriverColor(e,s->depth,s->rgb565,s->color,*out,s->write,s->depth==1&&!s->flags);return NearestIndex(e,s->count,s->color);}
