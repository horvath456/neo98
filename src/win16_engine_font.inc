// SPDX-License-Identifier: GPL-2.0-or-later
// Driver-facing GDI engine fonts.
case 301:
{
    struct Memory
    {
        Application &app; unsigned base;
        unsigned Byte(unsigned at) { unsigned char b=0; app.Read(base+at,&b,1); return b; }
        unsigned Word(unsigned at) { return app.Win16Word(base+at); }
        unsigned Long(unsigned at) { return app.Read32(base+at); }
        void Word(unsigned at,unsigned v) { app.Win16Word(base+at,v); }
        void Long(unsigned at,unsigned v) { app.Write32(base+at,v); }
        void Free(unsigned handle) { app.Win16GlobalFree(handle); }
    } memory{*this,frame.Pointer(0)};
    result=Win98EngineFont::Delete(memory,frame.Long(0)&0xffff);
    return true;
}
case 302:
case 315:
{
    const bool extended=ordinal==315;
    const unsigned shift=extended?1:0;
    const unsigned logical=frame.Pointer(4+shift), output=frame.Pointer(shift), xform=frame.Pointer(2+shift);
    result=0;
    unsigned char lf[50]{};
    if (!GdiTrueType::Available() || !Read(logical,lf,sizeof lf)) return true;
    auto word=[](const unsigned char *p,unsigned at){return unsigned(p[at])|unsigned(p[at+1])<<8;};
    int height=short(word(lf,0));
    // Size queries precede DC validation, and the empty default
    // request is refused before the query.
    if (!lf[18] && !height) return true;
    if (!(frame.Long(shift)>>16)) { result=0xd3; return true; }
    // GDI.302 supplies the default display DC from DGROUP 3Ch.
    const unsigned hdc=extended?frame.Word(7):User::GetDC(0);
    if (!Gdi::ValidDC(hdc)) return true;
    const int dpi_x=Gdi::DeviceCaps(hdc,88),dpi_y=Gdi::DeviceCaps(hdc,90);
    if (!extended) User::ReleaseDC(0,hdc);
    if (!height)
    {
        height=(12*dpi_y+36)/72;
        Win16Word(logical,unsigned(height)); // Windows 98 writes the default back.
    }
    char name[33]{};memcpy(name,lf+18,32);
    const unsigned weight=word(lf,8);
    const auto *face=GdiFonts::Match(name,height,weight,lf[10]!=0,lf[13]);
    if (!face || !face->truetype) return true;
    const auto *font=GdiTrueType::Realize(face->truetype,height,short(word(lf,2)),weight,
        lf[10]!=0,lf[11]!=0,lf[12]!=0,false,short(word(lf,4)));
    if (!font) return true;
    unsigned char fi[0xd3]{};
    auto put=[&](unsigned at,unsigned v){fi[at]=v;fi[at+1]=v>>8;};
    // packed FONTINFO metrics; the TrueType implementation
    // already follows the same size search and VDMX adjustment.
    memcpy(fi,font->fnt+0x42,0x21);
    put(0,3);put(2,72*font->ppem/(dpi_y?dpi_y:96));put(4,dpi_y);put(6,dpi_x);
    // The caller owns FONTINFO and its two 40-byte name areas. No separate
    // bitmap allocation is needed by the existing TrueType rasterizer.
    put(0x27,((frame.Long(shift)&0xffff)+0x83)&0xffff);
    put(0x34,font->underline_top);put(0x36,font->underline_thickness);
    put(0x38,font->strike_top);put(0x3a,font->strike_thickness);
    unsigned face_index=0;
    while(face_index<GdiTrueType::FaceCount() && GdiTrueType::FaceAt(face_index)!=font->face)++face_index;
    put(0x3c,face_index);put(0x3e,font->face->upem);
    put(0x40,0xb505);put(0x44,0xb505);
    put(0x48,word(lf,4));put(0x4a,word(lf,6));put(0x4c,font->xscale);put(0x4e,font->ppem);
    put(0x50,font->face->symbol_base);put(0x52,font->face->glyphs);put(0x54,font->index[0x1f]);
    // Character-code input. The public FONTINFO carries ANSI bounds;
    // EngineDeleteFont restores the glyph count/default-index pair at 52h.
    fi[0x1d]=0x1f;fi[0x1e]=0xff;fi[0x1f]=0;fi[0x20]=1;
    fi[0x33]=font->italic?0x80:0;
    put(0x75,(font->face->glyphs>0x400?1:0)|((fi[0x18]&1)?4:0));
    put(0x77,font->average);put(0x79,font->maximum);
    // appends the family immediately after the first NUL,
    // rather than at the end of its forty-byte name reservation.
    const char *display_name=(SameProfile(name,font->face->family) ||
        SameProfile(name,font->face->full))?name:font->face->family;
    snprintf(reinterpret_cast<char *>(fi+0x83),40,"%s",display_name);
    const unsigned next_name=0x84+strlen(reinterpret_cast<const char *>(fi+0x83));
    snprintf(reinterpret_cast<char *>(fi+next_name),40,"%s",font->face->family);
    unsigned char xf[19]{};
    memcpy(xf,fi+0x16,2);memcpy(xf+2,fi+0x19,2);
    memcpy(xf+4,fi+0x48,2);memcpy(xf+6,fi+0x48,2);
    memcpy(xf+8,fi+0x11,2);memcpy(xf+10,fi+0xe,3);
    if (lf[11]) xf[11]=1;
    if (lf[12]) xf[12]=1;
    if (!Store(output,fi,sizeof fi) || !Store(xform,xf,sizeof xf)) return true;
    result=extended?(1|(font->xscale!=0x100?2:0)|(font->italic?0x80:0)):0xd3;
    return true;
}
case 299:
case 303:
case 316:
case 304:
case 305:
case 317:
{
    const bool string_widths=ordinal==316;
    const bool widths=ordinal==299 || ordinal==303 || string_widths, context=ordinal==304;
    const unsigned input=frame.Pointer(string_widths?5:widths?4:context?1:8);
    unsigned char fi[0x83]{};
    result=widths?0:~0u;
    auto word=[&](unsigned at){return unsigned(fi[at])|unsigned(fi[at+1])<<8;};
    if(!Read(input,fi,sizeof fi) || (word(0)&3)!=3)return true;
    const auto *face=GdiTrueType::FaceAt(word(0x3c));
    const auto *base=face?GdiTrueType::Realize(face,-int(word(0x4e)),0,word(0x11),fi[0xe]!=0,fi[0xf]!=0,fi[0x10]!=0):nullptr;
    if(!base)return true;
    // FONTINFO may change the transform; keep the shared realized font
    // immutable, and keep its glyph-pointer tables off the Win16 call stack.
    struct FontCopy { GdiTrueType::Font *p; ~FontCopy(){delete p;} } copy{new GdiTrueType::Font(*base)};
    auto &font=*copy.p;
    font.xscale=word(0x4c);font.italic=(fi[0x33]&0x80)!=0;
    if(context || widths)
    {
        result=unsigned(int(short(GdiTrueType::EngineContext(font,context?short(frame.Word(0)):0,
            word(6),word(4),word(0x40)|(word(0x42)<<16),word(0x44)|(word(0x46)<<16)))));
        if(context || result==~0u)return true;
    }
    const bool indices=(fi[0x33]&4)!=0;
    if(widths)
    {
        const unsigned first=string_widths?0:frame.Word(3),last=frame.Word(2),out=frame.Pointer(0);
        if(!string_widths && last<first){result=0;return true;}
        const unsigned count=string_widths?last:last-first+1;
        const unsigned text=string_widths?frame.Pointer(3):0;
        // converts an ANSI string only while FONTINFO is in its
        // public form. An already encoded font takes WORD glyph indices.
        const bool encoded=string_widths && (word(0)&0x20);
        result=1;
        for(unsigned i=0;i<count;++i)
        {
            unsigned c=first+i;
            if(string_widths)
            {
                if(encoded)c=Win16Word(text+2*i);
                else { unsigned char byte=0;Read(text+i,&byte,1);c=byte; }
            }
            int metrics[6]{};
            if(GdiTrueType::EngineGlyph(font,c,string_widths?encoded:indices,0,nullptr,0,metrics)==~0u){result=0;break;}
            Win16Word(out+i*2,unsigned((metrics[4]+0x8000)>>16)+(font.bold?1:0));
        }
        return true;
    }
    const unsigned size=frame.Long(2),out=frame.Pointer(4),metrics_out=frame.Pointer(0);
    auto *bytes=size && out?static_cast<unsigned char *>(malloc(size)):nullptr;
    int metrics[6]{};
    if(!size || !out || bytes)
        result=GdiTrueType::EngineGlyph(font,frame.Word(7),indices,frame.Word(6),bytes,bytes?size:0,metrics);
    if(result==~0u)memset(metrics,0,sizeof metrics);
    Store(metrics_out,metrics,sizeof metrics);
    if(bytes && result!=~0u)Store(out,bytes,result<size?result:size);
    free(bytes);
    return true;
}
