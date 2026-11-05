/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "win98_window_text.h"
#include <cstring>
using U=unsigned;
static unsigned char memory[512];static U trace[128],used,same,result;
struct E{
 bool Valid(U h){return h!=0;}
 U FirstChild(U){return 2;}U NextSibling(U h){return h==2?3:0;}U ControlID(U h){return h==2?0x12345678:7;}
 bool SameProcess(U){return same!=0;}
 void Clear(U p){memory[p&511]=0;}
 U invoke(U kind,U h,U m,U w,U p){trace[used++]=kind;trace[used++]=h;trace[used++]=m;trace[used++]=w;trace[used++]=p;trace[used++]=memory[0x100];return result;}
 U Send(U h,U m,U w,U p){return invoke(1,h,m,w,p);}U Default(U h,U m,U w,U p){return invoke(2,h,m,w,p);}
};
extern "C"{
U window_text_run(U op,U flags,U value,U id,U capacity,U*out){memset(memory,0x7b,sizeof memory);used=0;same=flags;result=value;E e;U r=0;
 if(op==0)r=Win98WindowText::Get(e,1,0x1280100,capacity);else if(op==1)r=Win98WindowText::Length(e,1);else if(op==2)r=Win98WindowText::Set(e,1,0x1280180);else if(op==3)r=Win98WindowText::GetItem(e,1,id,0x1280100,capacity);else if(op==4)r=Win98WindowText::SetItem(e,1,id,0x1280180);else r=Win98WindowText::Get(e,0,0x1280100,capacity);
 out[0]=memory[0x100];out[1]=used;memcpy(out+2,trace,used*4);return r;}
}
