// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_dib_palette.h"
#include "../src/win98_gdi_pixel.h"
extern "C" unsigned dib_palette_table(unsigned mode,unsigned depth,unsigned requested,unsigned*used,unsigned count,const unsigned*colors,const unsigned short*indices,unsigned*out,unsigned*maps){
 Win98GdiPalette::Entry entries[256]{};for(unsigned i=0;i<count;++i){entries[i].color=colors[i];entries[i].mapping[1]=29;}
 Win98GdiPalette::Palette palette{entries,count,false,false};
 struct E{const unsigned short*indices;unsigned*out;unsigned Index(unsigned i){return indices[i];}void Color(unsigned i,unsigned c){out[i]=c;}}e{indices,out};
 unsigned result=requested;if(mode)Win98DibPalette::Transfer(e,palette,requested);else result=Win98DibPalette::Construct(e,palette,depth,requested,*used,indices!=nullptr);
 for(unsigned i=0;i<count;++i)maps[i]=entries[i].mapping[1];return result;
}
extern "C" unsigned dib_palette_physical(unsigned depth,unsigned color,unsigned*physical){
 struct E{unsigned Count(){return 256;}unsigned Palette(unsigned){return 0;}unsigned Read32(unsigned){return 0;}}e;
 return Win98GdiPixel::DriverColor(e,depth,false,color,*physical,true,false);
}
