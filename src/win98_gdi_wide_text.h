// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98GdiWideText {
using U=unsigned;
inline U Count(U value){value&=65535;return value<8192?value:8192;}
inline int SignedWord(U value){return int(value&32767)-int(value&32768);}
// GDI. TextOutW supplies zero flags/rect/dx.
// Updating, metafile recording, font conversion and the ANSI engine are providers.
template<class E> U Draw(E&e,U text,U count,U flags){
    if(e.NeedsUpdate())e.Update();
    if(e.InvalidHandle())return 0;
    if((e.Signature()&255)==0x4d&&e.Metafile()){
        U result=e.Record(0x54)&65535;if(result!=1)return result;
    }
    count=Count(count);flags&=65535;U converted=0,draw_text=text;
    if(text&&count&&!(flags&0x10)){
        U conversion_flags=0;converted=e.Convert(text,count,1,conversion_flags);
        if(!converted)return 0;draw_text=converted;flags|=conversion_flags;
    }
    const bool ordinary=(e.Signature()&0x5fff)!=0x4f51;
    U previous=0;if(ordinary){previous=e.Metafile();e.SetMetafile(0);}
    U result=e.Draw(draw_text,count,flags)&65535;
    if(ordinary)e.SetMetafile(previous);
    if(converted)e.Free(converted);
    return result;
}
// GDI. Mapping is the original two MulDiv providers followed
// by the signed absolute/saturation sequence also used by GetCharWidthW.
inline U ExtentWord(U v){v&=65535;return v&32768?(v==32768?32767:65536-v):v;}
template<class E> U Extent(E&e,U text,U count){
    if(e.NeedsUpdate())e.Update();count=Count(count);
    U flags=0,converted=e.Convert(text,count,1,flags);if(!converted)return 0;
    U extent=e.Measure(converted,count,flags);
    if(e.Mapped()){
        U x=ExtentWord(e.Map(extent&65535,0));
        U y=ExtentWord(e.Map(extent>>16,1));extent=x|(y<<16);
    }
    e.Free(converted);return extent;
}
// GDI: zero length avoids conversion, and the public SIZE
// wrapper returns TRUE even when conversion leaves the packed extent zero.
template<class E> U ExtentPoint(E&e,U text,U count,U&extent){
    extent=(count&65535)?Extent(e,text,count):0;return 1;
}
}
