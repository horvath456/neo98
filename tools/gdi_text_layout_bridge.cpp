// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_gdi_text_layout.h"
using namespace Win98GdiTextLayout;
extern "C" unsigned text_core(unsigned mode,const unsigned*a,unsigned*out){
 if(mode==0){U state=a[0];U result=SetAlign(state,a[1]);out[0]=state;return result;}
 if(mode==1){DibText::DrawMode dm;dm.total_break_extra=a[2];dm.break_extra=a[3];dm.break_error=a[4];dm.break_remainder=a[5];dm.break_count=a[6];
  bool okay=Justify(dm,int(a[0]),int(a[1]));out[0]=unsigned(short(dm.total_break_extra));out[1]=unsigned(short(dm.break_extra));out[2]=unsigned(short(dm.break_error));out[3]=unsigned(short(dm.break_remainder));out[4]=unsigned(short(dm.break_count));return okay;}
 if(mode==5){out[0]=U(Word(a[1]));return U(ExtraQuery(a[0]));}
 if(mode==2){auto p=Place(a[0],{int(a[1]),int(a[2])},{int(a[3]),int(a[4])},a[5],a[6],a[7]);out[0]=p.x;out[1]=p.y;return 0;}
 if(mode==3){auto p=Advance(a[0],{int(a[1]),int(a[2])},a[3]);out[0]=p.x;out[1]=p.y;return 0;}
 return unsigned(ExtraQuery(int(a[0])));
}
