// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_dc_attributes.h"
extern "C" unsigned dc_state_query(unsigned signature,unsigned operation,unsigned mode,unsigned bg,unsigned fg,unsigned*out){
 struct E{unsigned mode,bg,fg,*trace;
  unsigned Read16(unsigned offset){trace[0]=offset;trace[1]=2;return mode;}
  unsigned Read32(unsigned offset){trace[0]=offset;trace[1]=4;return offset==0x66?bg:fg;}
 }e{mode,bg,fg,out};out[0]=out[1]=0;
 return operation==0?Win98DcAttributes::BkMode(e,signature):operation==1?Win98DcAttributes::BkColor(e,signature):Win98DcAttributes::TextColor(e,signature);
}
