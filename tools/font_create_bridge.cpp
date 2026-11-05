// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_font_create.h"
#include <string.h>
using U=unsigned;
extern "C" void font_narrow(const unsigned char*in,unsigned char*out){Win98FontCreate::Narrow(in,out);}
extern "C" void font_construct(const U*a,const unsigned char*face,U length,unsigned char*out){Win98FontCreate::Construct(a,face,length,out);}
extern "C" void font_expand(const unsigned char*in,unsigned char*out){Win98FontCreate::Expand(in,out);}
extern "C" U font_create(const U*a,const unsigned char*in,unsigned char*out,U*trace){
 struct E{
  const U*a;const unsigned char*in;unsigned char*out;U*trace;U at=0;
  void Event(U k,U x=0,U y=0){trace[++at]=k;trace[++at]=x;trace[++at]=y;trace[0]=at;}
  U PendingDeletes(){return a[0];}void PurgePendingFonts(){Event(1);}
  U Allocate(const unsigned char*,U size){Event(2,size,0x4f49);if(a[1])memcpy(out,in,size);return a[1];}
  U FindAtom(const unsigned char*){Event(3);return a[2];}
  U SpecialAtom(U i){return a[3+i];}U SpecialCharset(){return a[9];}
  void Alias(U atom,U&charset,U&alias){Event(4,atom,charset);charset=a[10];alias=a[11];}
  U Byte(U,U i){return out[i];}void SetByte(U,U i,U v){out[i]=v;}
  void RequestedCharset(U,U at,U value){out[at]=value;out[201]=value;Event(5,at,value);}
 }e{a,in,out,trace};return Win98FontCreate::Create(e,in,a[12]);
}
