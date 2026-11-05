// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98FontCreate {
using U=unsigned;
inline U Word(const unsigned char*p){return p[0]|U(p[1])<<8;}
inline void PutWord(unsigned char*p,U v){p[0]=v;p[1]=v>>8;}
// GDI32: LOGFONTA's five DWORDs become WORDs;
// the eight byte attributes and all 32 face bytes are copied unchanged.
inline void Narrow(const unsigned char*source,unsigned char*out){
    for(U i=0;i<5;++i){out[i*2]=source[i*4];out[i*2+1]=source[i*4+1];}
    for(U i=0;i<40;++i)out[10+i]=source[20+i];
}
// GDI, following the public CreateFontA WORD argument thunk.
// The caller supplies the original string including NUL, or one NUL for NULL.
inline void Construct(const U*a,const unsigned char*face,U length,unsigned char*out){
    for(U i=0;i<5;++i)PutWord(out+i*2,a[i]);
    for(U i=0;i<8;++i)out[10+i]=a[5+i];
    for(U i=0;i<length;++i)out[18+i]=face[i];
}
// GDI. Memory allocation, atom lookup/aliases and deferred
// deletion enumeration are providers; this preserves the original order.
// handle is deliberately not WORD-masked: the native object allocator owns it.
template<class E> U Create(E&e,const unsigned char*source,U string_length){
    if(e.PendingDeletes()>=16)e.PurgePendingFonts();
    const U requested=source[13];
    if(string_length==65535)return 0;
    const U bytes=(string_length+20)&65534;
    const U handle=e.Allocate(source,bytes);
    if(!handle)return 0;
    const U atom=e.FindAtom(source+18);
    if(atom){
        U charset=0;bool replace=true;
        if(atom==e.SpecialAtom(0)||atom==e.SpecialAtom(1)||atom==e.SpecialAtom(2))charset=2;
        else if(atom==e.SpecialAtom(3)||atom==e.SpecialAtom(4))charset=e.SpecialCharset();
        else if(atom==e.SpecialAtom(5)){
            e.SetByte(handle,17,(e.Byte(handle,17)&252)|2);charset=e.SpecialCharset();
        }else{
            charset=e.Byte(handle,13);
            if(charset==1)replace=false;
            else {U mapped=charset,alias=0;e.Alias(atom,mapped,alias);
                if(alias&&mapped!=1)charset=mapped;}
        }
        if(replace)e.SetByte(handle,13,charset);
    }else{
        bool zero=true;for(U i=0;i<18;++i)if(source[i])zero=false;
        if(zero)e.SetByte(handle,13,1);
    }
    e.SetByte(handle,15,e.Byte(handle,15)&0xdf); // lfClipPrecision private bit.
    e.RequestedCharset(handle,string_length+18,requested);
    return handle;
}
// GDI. Same signed expansion as USER's SPI LOGFONT adapter.
inline void Expand(const unsigned char*source,unsigned char*out){
    for(U i=0;i<5;++i){U v=Word(source+i*2);if(v&0x8000)v|=0xffff0000;
        for(U j=0;j<4;++j)out[i*4+j]=v>>(8*j);}
    for(U i=0;i<40;++i)out[20+i]=source[10+i];
}
// GDI: NULL output is a size query regardless of capacity.
inline U QueryBytes(U capacity,bool output){return output?(capacity<60?capacity:60):60;}
} // namespace Win98FontCreate
