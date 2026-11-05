// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98FontCharset {
using U=unsigned;
struct Face{U update,dirty,font_type,physical_charset,logical_charset,dc_flags,font_reference,language_flags;};
// GDI. Output is cleared only after successful realization.
template<class E> U TextCharset(E&e,const Face&f,U output){
    if(f.update)e.Update();if(f.dirty&&!e.Realize())return 1;
    if(output)e.Write32(output,0);
    if(!(f.font_type&0x20))return f.physical_charset&255;
    if(output&&(f.dc_flags&0x10))e.Write32(output,f.font_reference);
    return f.logical_charset&255;
}
// GDI. A failed realization's 8000 bit is removed by 103F.
template<class E> U LanguageInfo(E&e,const Face&f){
    if(f.update)e.Update();if(f.dirty&&!e.Realize())return 0;
    U flags=f.language_flags&65535,result=0;
    if(f.font_type&0x20){if(e.KerningPairs())flags|=8;result=0x40000;}
    return result|(flags&0x103f);
}
template<class E> U Big32(E&e,U p){return (e.Read8(p)<<24)|(e.Read8(p+1)<<16)|(e.Read8(p+2)<<8)|e.Read8(p+3);}
// GDI32. Range mapping validates the SFNT byte spans.
template<class E> U FindTable(E&e,U font,U tag){
    U bytes=e.Read32(font+12),table=e.FontRange(font,12,bytes);if(!table)return 0;
    U end=table+bytes;if(end<=table)return 0;
    for(U p=table;p<end;p+=16)if(e.Read32(p)==tag){
        if(!e.Read32(p+12))return 0;
        return e.FontRange(font,Big32(e,p+8),Big32(e,p+12));
    }
    return 0;
}
// GDI32. Flags are unused by the original export.
template<class E> U Signature(E&e,U dc,U output){
    if(!output)return e.Charset(dc);
    if(!e.Writable(output,24))return 1;
    U font=0,charset=e.CharsetReference(dc,font);
    if(charset==1||!font){for(U i=0;i<24;i+=4)e.Write32(output+i,0);return charset;}
    U table=e.Table(font,0x322f534f);
    if(table&&e.Read16(table)){
        for(U i=0;i<4;++i)e.Write32(output+i*4,Big32(e,table+42+i*4));
        for(U i=0;i<2;++i)e.Write32(output+16+i*4,Big32(e,table+78+i*4));
    }
    return charset;
}
// GDI32. The 14-row original tables hold charset,
// codepage and fsCsb[0]; matching a multi-bit mask is exact, not first-bit.
template<class E> U Translate(E&e,U source,U output,U mode){
    if(!e.Writable(output,32))return 0;
    e.Write32(output+28,0);for(U off=20;off>=8;off-=4)e.Write32(output+off,0);
    U key=source&65535,which=0,locale_mask=0;
    if(mode==1)which=0;
    else if(mode==2)which=1;
    else if(mode==3){if(!e.Readable(source,8)||e.Read32(source+4))return 0;key=e.Read32(source);which=2;}
    else if(mode==0x1000){U signature[8]{};if(!e.LocaleSignature(source&65535,signature))return 0;key=signature[4];locale_mask=signature[6];which=2;}
    else return 0;
    for(U i=0;i<14;++i)if(e.Entry(which,i)==key){
        e.Write32(output,e.Entry(0,i));e.Write32(output+4,e.Entry(1,i));
        e.Write32(output+24,mode==0x1000?locale_mask:e.Entry(2,i));return 1;
    }
    return 0;
}
} // namespace Win98FontCharset
