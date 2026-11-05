// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_gdi_palette.h"
using namespace Win98GdiPalette;
extern "C" unsigned palette_run(unsigned op,unsigned count,unsigned stock,unsigned a,unsigned b,unsigned*input,unsigned*colors,unsigned*output){
 Entry entries[4096]{};Palette p{entries,count,stock!=0,false};for(unsigned i=0;i<count;++i){entries[i].color=colors[i];entries[i].mapping[0]=17;entries[i].mapping[1]=29;entries[i].mapping[2]=41;}
 unsigned result=0;
 if(op==0){result=CreateValid(a,count);if(result)for(unsigned i=0;i<count;++i){entries[i]=Entry{};entries[i].color=input[i]=Normalize(input[i]);}}
 if(op==1)result=Get(p,a,b,stock?nullptr:output+3+count*4);
 if(op==2)result=Set(p,a,b,input);
 if(op==3)result=Nearest(p,a);
 if(op==4)result=Tagged(p,a,b!=0);
 if(op==5){struct E{unsigned fail;bool Resize(Palette&p,unsigned n){if(fail)return false;for(unsigned i=p.count;i<n;++i)p.entries[i]=Entry{};return true;}}e{b};result=Resize(e,p,a);}
 if(op==6){Animate(p,a,b,input);result=1;}
 output[0]=p.count;output[1]=p.dirty;output[2]=result;
 for(unsigned i=0;i<p.count;++i){output[3+i*4]=entries[i].mapping[0];output[4+i*4]=entries[i].mapping[1];output[5+i*4]=entries[i].mapping[2];output[6+i*4]=entries[i].color;}return result;
}
