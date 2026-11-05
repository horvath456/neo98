// SPDX-License-Identifier: GPL-2.0-or-later
// Native guest-memory/FNT providers for original GDI Unicode wrappers.
unsigned Application::WideTextApi(ApiName name,const unsigned*a){
    using U=unsigned;const bool extent=!strncmp(name,"GetTextExtentPoint",18);
    const bool extended=API_IS(name,"ExtTextOutW");
    const U source=extent?a[1]:extended?a[5]:a[3];
    const U count=Win98GdiWideText::Count(extent?a[2]:extended?a[6]:a[4]);
    const U flags=extended?(a[3]&65535):0;
    if(!Gdi::ValidDC(a[0]))return 0;
    const auto *outline=GdiTrueType::FontOf(Gdi::SelectedFont(a[0]));
    if(count>4096||(flags&~0x16u)){last_error=50;return 0;}
    if(count&&(!source||!virtual_memory.Access(source,count*2,false))){last_error=87;return 0;}
    Gdi::TEXTMETRICA metrics{};
    if(count&&(!Gdi::GetTextMetricsA(a[0],&metrics)||(metrics.tmCharSet!=0&&metrics.tmCharSet!=2))){last_error=50;return 0;}
    Gdi::RECT rect{};int advances[4096];const Gdi::RECT*rectangle=nullptr;const int*dx=nullptr;
    if(extended&&a[4]){int input[4];if(!Read(a[4],input,16))return 0;
        rect={Win98GdiWideText::SignedWord(input[0]),Win98GdiWideText::SignedWord(input[1]),Win98GdiWideText::SignedWord(input[2]),Win98GdiWideText::SignedWord(input[3])};rectangle=&rect;}
    if(extended&&a[7]&&count){if(!Read(a[7],advances,count*4))return 0;
        for(U i=0;i<count;++i)advances[i]=Win98GdiWideText::SignedWord(advances[i]);dx=advances;}
    struct E{
        Application&app;U dc;int x,y;const Gdi::RECT*rect;const int*dx;
        const GdiTrueType::Font *outline;U charset;
        char bytes[4096];unsigned short glyphs[4096];
        U mapped_source=0;bool failed=false,converted_glyphs=false;
        bool NeedsUpdate(){return false;}void Update(){}bool InvalidHandle(){return false;}
        U Signature(){return 0x4f4d;}U Metafile(){return 0;}void SetMetafile(U){}U Record(U){return 0;}
        bool Realize(){return true;}U Allocate(U){return 1;}void Free(U){}
        U Glyphs(U source,U,U bytes){
            if(!outline)return ~0u;
            if(!app.Read(source,glyphs,bytes)){failed=true;return ~0u;}
            for(U i=0;i<bytes/2;++i)glyphs[i]=GdiTrueType::UnicodeIndex(*outline,glyphs[i]);
            converted_glyphs=true;return 0;
        }
        U CharsetTable(U charset){return charset==0?0x10000:0;}
        U Read16(U p){if(p>=0x10000)return OriginalGdiAnsiCharset[(p-0x10000)/2];
            unsigned short ch=0;if(!app.Read(mapped_source+p,&ch,2))failed=true;return ch;}
        void Write8(U p,U value){bytes[p-1]=char(value);}
        U Convert(U charset,U n,U input,U output,U options){mapped_source=input;
            return Win98FontWidth::ConvertBytes(*this,charset,n,0,output,options);}
        U Convert(U input,U n,U options,U&draw_flags){
            const Win98FontWidth::RunState state{0,0x4f4d,outline?0x100u:0u,outline?0x10u:0u,0,outline?0x20u:0u,outline?4u:0u,charset};
            return Win98FontWidth::ConvertRun(*this,state,input,n,options,draw_flags);}
        U Draw(U source,U n,U options){
            if(failed)return 0;
            if(options&0x10){
                if(!converted_glyphs&&n&&!app.Read(source,glyphs,n*2))return 0;
                if(outline){
                    if(app.RecordingMetafile(dc)){app.last_error=50;return 0;}
                    return Gdi::ExtTextOutGlyphs(dc,x,y,options&~0x10u,rect,glyphs,n,dx);
                }
                // Installed raster fonts use byte character codes as glyph
                // indices. The W call supplies one WORD per character.
                for(U i=0;i<n;++i)bytes[i]=char(glyphs[i]);
                options&=~0x10u;
            }
            if(app.RecordingMetafile(dc)){
                // WMF records byte strings. Use the same Unicode conversion
                // as screen output, then the existing recorder (including dx
                // and the clipping/opaque rectangle) instead of drawing now.
                const U aligned=(n+3)&~3u,block=app.Allocate(aligned+16+n*4);
                if(!block)return 0;
                bool ok=app.Store(block,bytes,n);
                if(rect)ok=ok&&app.Store(block+aligned,rect,16);
                if(dx)ok=ok&&app.Store(block+aligned+16,dx,n*4);
                const U args[]={dc,U(x),U(y),options,rect?block+aligned:0,block,n,dx?block+aligned+16:0};
                const U result=ok?app.GdiApi("ExtTextOutA",args):0;
                app.Free(block);return result;
            }
            return Gdi::ExtTextOutA(dc,x,y,options,rect,bytes,n,dx);
        }
        U Measure(U,U n,U options){Gdi::SIZE size{};if(failed||!((options&0x10)?Gdi::GetTextExtentGlyphs(dc,glyphs,n,&size):Gdi::GetTextExtentPoint32A(dc,bytes,n,&size))){failed=true;return 0;}
            return (U(size.cx)&65535)|((U(size.cy)&65535)<<16);}
        bool Mapped(){return false;}U Map(U v,U){return v;}
    }e{*this,a[0],extent?0:Win98GdiWideText::SignedWord(a[1]),extent?0:Win98GdiWideText::SignedWord(a[2]),rectangle,dx,outline,metrics.tmCharSet};
    if(extent){U packed=0;U result=Win98GdiWideText::ExtentPoint(e,source,count,packed);
        Gdi::SIZE size{Win98GdiWideText::SignedWord(packed),Win98GdiWideText::SignedWord(packed>>16)};
        if(!a[3]||!Store(a[3],&size,8))return 0;return e.failed?0:result;}
    U result=Win98GdiWideText::Draw(e,source,count,flags);
    if(result)User::DrawingChanged(a[0]);return U(Win98GdiWideText::SignedWord(result));
}
