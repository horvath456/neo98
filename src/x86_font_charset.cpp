// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "win98_font_charset.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/font_charset_info.h"
#endif
#include <string.h>
namespace X86 {
namespace {
unsigned RasterCharset(unsigned dc,unsigned&reference){
    Gdi::TEXTMETRICA metrics{};if(!Gdi::GetTextMetricsA(dc,&metrics))return 1;
    struct E{unsigned&reference;void Update(){}bool Realize(){return true;}
        void Write32(unsigned,unsigned value){reference=value;}}e{reference};
    const Win98FontCharset::Face face{0,0,0,metrics.tmCharSet,0,0,0,0};
    return Win98FontCharset::TextCharset(e,face,1);
}
}
unsigned Application::FontCharsetApi(ApiName name,const unsigned*a){
    using U=unsigned;
    if(API_IS(name,"GetTextCharset")){U reference=0;return RasterCharset(a[0],reference);}
    if(API_IS(name,"GetFontLanguageInfo")){
        Gdi::TEXTMETRICA metrics{};if(!Gdi::GetTextMetricsA(a[0],&metrics))return 0;
        struct E{void Update(){}bool Realize(){return true;}bool KerningPairs(){return false;}}e;
        // Installed FNTs have neither an outline glyph provider nor a
        // shaping/kerning engine. Do not infer those capabilities from names.
        const Win98FontCharset::Face face{0,0,0,metrics.tmCharSet,0,0,0,0};
        return Win98FontCharset::LanguageInfo(e,face);
    }
    struct E{
        Application&app;
        bool Writable(U p,U bytes){return p&&app.virtual_memory.Access(p,bytes,true);}
        bool Readable(U p,U bytes){return p&&app.virtual_memory.Access(p,bytes,false);}
        U Read8(U p){unsigned char v=0;app.Read(p,&v,1);return v;}
        U Read16(U p){unsigned short v=0;app.Read(p,&v,2);return v;}
        U Read32(U p){return app.Read32(p);}void Write32(U p,U v){app.Write32(p,v);}
        U Charset(U dc){U ref=0;return RasterCharset(dc,ref);}
        U CharsetReference(U dc,U&ref){return RasterCharset(dc,ref);}
        U Table(U,U){return 0;} // FNT resources have no SFNT/OS2 reference.
        U Entry(U table,U index){return OriginalGdiCharsetInfo[table][index];}
        bool LocaleSignature(U locale,U*out){
            // Use the same KERNEL locale API/provider. The current locale
            // provider does not yet publish LOCALE_FONTSIGNATURE (0058).
            U temporary=app.Allocate(36);if(!temporary){app.last_error=8;return false;}
            const U args[]={locale,0x58,temporary,0x22};
            U result=app.LocaleInfo(args);bool ok=result&&app.Read(temporary,out,32);
            app.Free(temporary);return ok;
        }
    }e{*this};
    if(API_IS(name,"GetTextCharsetInfo"))return Win98FontCharset::Signature(e,a[0],a[1]);
    return Win98FontCharset::Translate(e,a[0],a[1],a[2]);
}
} // namespace X86
