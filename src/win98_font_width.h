// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98FontWidth {
using U=unsigned;
// GDI. The table belongs to the font charset, not the
// process ANSI best-fit conversion service. A zero count includes the NUL.
template<class E> U ConvertBytes(E&e,U charset,U count,U source,U output,U flags){
    U table=e.CharsetTable(charset&65535);if(!table)table=e.CharsetTable(0);
    count&=65535;if(!count){U at=source;do{++count;}while(e.Read16(at+2*(count-1)));}
    for(U i=0;i<count;++i){const U character=e.Read16(source+i*2);U byte=character;
        if(character>=256||e.Read16(table+character*2)!=character){
            byte=256;for(U j=128;j<256;++j)if(e.Read16(table+j*2)==character){byte=j;break;}
            if(byte==256)for(U j=0;j<128;++j)if(e.Read16(table+j*2)==character){byte=j;break;}
            if(byte==256){if(flags&1)return 0;byte=0x1f;}
        }
        e.Write8(output+i,byte);
    }
    return count;
}
struct RunState{U dirty,signature,caps,dc_flags,font_flags,font_type,font_features,charset;};
// GDI. Allocation and engine conversion remain providers.
template<class E> U ConvertRun(E&e,const RunState&s,U source,U count,U flags,U&draw_flags){
    draw_flags=0;if(s.dirty&&!e.Realize())return 0;
    count&=65535;U output=e.Allocate(count*2);if(!output)return 0;
    const bool glyph=(s.signature&0x5fff)!=0x4f51&&(s.caps&0x100)&&
        ((s.dc_flags&0x10)||(s.font_flags&0x40))&&(s.font_type&0x20)&&(s.font_features&4);
    if(!glyph)flags=0;
    if(!glyph||((s.charset<0x80||s.charset>0x88)&&(flags&1))){
        U result=e.Convert(s.charset,count,source,output,flags);
        if(!(flags&1)||result)return output;
    }
    if(e.Glyphs(source,output,count*2)!=0xffffffffu){draw_flags=0x10;return output;}
    e.Free(output);return 0;
}
// GDI, including per-character allocation, partial success,
// driver callback and mapping order. Caller bounds the original WORD loop.
template<class E> U Widths(E&e,U first,U last,U output){
    U result=0;first&=65535;last&=65535;if(e.NeedsUpdate())e.Update();
    do{U flags=0,lease=e.ConvertCharacter(first,flags);if(!lease)return result;
        U width=e.Measure(lease,flags)&65535;
        if(e.Mapped()){width=e.MapWidth(width)&65535;
            if(width&0x8000)width=width==0x8000?0x7fff:0x10000-width;
        }
        e.Write16(output,width);output+=2;result=1;e.Free(lease);first=(first+1)&65535;
    }while(first<=last);return result;
}
struct AbcState{U update,dirty,enabled,dc_flags,font_type,glyph_handle,font_flags;};
// GDI. The bearing/cache engine after C31 is an explicit
// provider boundary; no ABC data exists for the installed raster fonts.
template<class E> U AbcQuery(E&e,const AbcState&s){
    if(s.update)e.Update();if(s.dirty&&!e.Realize())return 0;if(!(s.enabled&1))return 0;
    if(!(s.dc_flags&0x10)){
        if(!(s.dc_flags&4)||(s.font_type&3)!=3)return 0;
        if(!s.glyph_handle&&!e.RealizeOutline())return 0;
    }
    if(e.FontFlags()&0x80)return 0;return e.OutlineQuery();
}
// GDI32: signed A/C, unsigned B. This also runs after
// a failed underlying query, widening the caller's existing packed words.
template<class E> void ExpandAbc(E&e,U output,U count){
    if(!output||!count||count>0x15555555)return;
    for(U i=count;i;--i){U src=output+(i-1)*6,dst=output+(i-1)*12;
        U a=e.Read16(src),b=e.Read16(src+2),c=e.Read16(src+4);
        e.Write32(dst+8,(c&0x8000)?c|0xffff0000u:c);e.Write32(dst+4,b);
        e.Write32(dst,(a&0x8000)?a|0xffff0000u:a);
    }
}
} // namespace Win98FontWidth
