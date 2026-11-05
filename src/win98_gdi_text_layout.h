// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "dib_text.h"
namespace Win98GdiTextLayout {
using U=unsigned;
inline int Word(U n){return int(n&32767)-int(n&32768);}
struct Export{const char*name;U ordinal,argc;};
inline constexpr Export Exports[]={{"GetTextAlign",301,1},{"GetTextCharacterExtra",302,1},
 {"SetTextAlign",410,2},{"SetTextCharacterExtra",411,2},{"SetTextJustification",413,3}};
// GDI parameter gate and1:7B69 ->7B08 ordinary-DC WORD exchange.
inline bool ValidAlign(U value){return !(value&0xfee0);}
inline U SetAlign(U&state,U value){U before=state&65535;state=value&65535;return before;}
// GDI after native ordinary-DC access and LPtoDP provider.
// The original IDIV faults for a zero divisor or an overflowing quotient.
inline bool Justify(DibText::DrawMode&dm,int extra,int breaks){
 extra=Word(extra);breaks=Word(breaks);
 if(!(extra|breaks)){dm.total_break_extra=dm.break_extra=dm.break_error=dm.break_remainder=dm.break_count=0;return true;}
 if(!breaks||(extra==-32768&&breaks==-1))return false;
 dm.total_break_extra=extra;dm.break_extra=extra/breaks;
 dm.break_error=Word((breaks>>1)+1);dm.break_remainder=extra%breaks;dm.break_count=breaks;return true;
}
// GDI takes the absolute WORD difference after DPtoLP.
inline int ExtraQuery(int logical_difference){int value=Word(logical_difference);return value<0?Word(0u-U(value)):value;}
struct Point{int x,y;};
// GDI, unrotated physical font path. Metric/extent and final
// LPtoDP calls are supplied by the font/DC provider. All working words wrap.
inline Point Place(U align,Point input,Point current,int width,int height,int ascent,bool mirror_x=false,bool mirror_y=false){
 Point result=(align&1)?current:input;result={Word(result.x),Word(result.y)};
 int vertical=(align&8)?((align&16)?Word(ascent):Word(height)):0;
 int horizontal=(align&2)?Word(width):0;
 if((align&6)==6)horizontal/=2;
 if(mirror_x)horizontal=Word(0u-U(horizontal));if(mirror_y)vertical=Word(0u-U(vertical));
 return {Word(U(result.x)-U(horizontal)),Word(U(result.y)-U(vertical))};
}
// GDI. Center leaves CP unchanged; right advances backwards.
inline Point Advance(U align,Point current,int width){
 int amount=(align&4)?0:(align&2)?Word(0u-U(width)):Word(width);
 return {Word(U(current.x)+U(amount)),Word(current.y)};
}
}
