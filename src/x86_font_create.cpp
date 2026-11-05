// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "win98_font_create.h"
#include "gdi_font_catalog.h"
#include "gdi_truetype.h"
#include <stdlib.h>
#include <string.h>
#ifndef NEO98_RUNTIME_CONTENT
#include "../build/generated/marlett_glyphs.h"
#endif
namespace X86 {

// Fallback when the installed MARLETT.TTF is absent. The generated square
// cells are exposed as a Windows 2.x FNT; the normal path below uses the
// original TrueType driver and preserves full glyph metrics and overhangs.
#ifndef NEO98_RUNTIME_CONTENT
static unsigned char *MarlettFont(int height, unsigned &length)
{
    int size = height < 0 ? -height : height;
    if (!size) size = 13;
    if (size < (int)Win98Marlett::MinimumSize) size = Win98Marlett::MinimumSize;
    if (size > (int)Win98Marlett::MaximumSize) size = Win98Marlett::MaximumSize;
    const unsigned first = 0x20, last = 0x7a, count = last - first + 1;
    const unsigned stride = (unsigned)((size + 7) / 8), glyph = stride * size;
    unsigned glyphs = 0;
    for (const char *c = Win98Marlett::CellCodes; *c; ++c) if ((unsigned char)*c >= first && (unsigned char)*c <= last) ++glyphs;
    const unsigned table = 118, bits = table + (count + 1) * 4, face = bits + (glyphs + 1) * glyph;
    length = face + 8;
    auto *f = static_cast<unsigned char *>(calloc(length, 1));
    if (!f) return nullptr;
    auto word = [&](unsigned at, unsigned v) { f[at] = v & 255; f[at + 1] = (v >> 8) & 255; };
    auto dword = [&](unsigned at, unsigned v) { word(at, v & 0xffff); word(at + 2, v >> 16); };
    word(0, 0x200); dword(2, length); word(0x42, 0);           // dfVersion, dfSize, dfType raster
    word(0x44, (unsigned)(size * 72 / 96)); word(0x46, 96); word(0x48, 96);
    word(0x4a, (unsigned)size); word(0x4c, 0); word(0x4e, 0);   // ascent, leadings
    f[0x53] = 0x90; f[0x54] = 1; f[0x55] = 2;                  // weight 400, charset SYMBOL
    word(0x56, 0); word(0x58, (unsigned)size); f[0x5a] = 0x31;  // variable pitch, pixel height
    word(0x5b, (unsigned)size); word(0x5d, (unsigned)size);
    f[0x5f] = first; f[0x60] = last; f[0x61] = 0; f[0x62] = 0;  // default and break char: space
    word(0x63, stride); dword(0x65, 0); dword(0x69, face); dword(0x6d, 0); dword(0x71, bits);
    memcpy(f + face, "Marlett", 8);
    // Space and every unlisted code: zero width, an empty cell at the first slot.
    for (unsigned i = 0; i <= count; ++i) { word(table + i * 4, 0); word(table + i * 4 + 2, bits); }
    unsigned slot = 1;
    for (const char *c = Win98Marlett::CellCodes; *c; ++c)
    {
        const unsigned code = (unsigned char)*c;
        if (code < first || code > last) continue;
        const unsigned at = bits + slot++ * glyph;
        for (int row = 0; row < size; ++row)
        {
            const unsigned cell = Win98Marlett::Row(size, *c, row);
            for (int column = 0; column < size; ++column)
                if ((cell >> column) & 1) f[at + (column / 8) * size + row] |= 0x80 >> (column & 7);
        }
        unsigned advance = Win98Marlett::Advance(size, *c);
        if (!advance || advance > (unsigned)size) advance = (unsigned)size;
        word(table + (code - first) * 4, advance); word(table + (code - first) * 4 + 2, at);
    }
    return f;
}
#endif
namespace {
bool FaceEqual(const char*a,const char*b){
    for(;;){unsigned x=static_cast<unsigned char>(*a++),y=static_cast<unsigned char>(*b++);
        if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;
        if(x!=y)return false;if(!x)return true;}
}
}
unsigned Application::FontCreateApi(ApiName name,const unsigned*a){
    using U=unsigned;
    if(API_IS(name,"GetObjectA")){
        for(const auto&f:fonts)if(f.handle&&f.handle==a[0]&&Gdi::ObjectExists(f.handle)){
            U n=Win98FontCreate::QueryBytes(a[1],a[2]!=0);
            if(a[2]&&n&&!virtual_memory.Access(a[2],n,true))return 0;
            if(a[2]&&n&&!Store(a[2],f.logfont,n))return 0;
            if(!n)last_error=87;
            return n;
        }
        last_error=87;return 0;
    }
    // The existing guest string provider is bounded at 8191 bytes. Indirect
    // requests must terminate inside lfFaceName; Win98's out-of-structure
    // reads for a malformed nonterminated LOGFONT are not reproduced.
    unsigned char indirect[50]{};const char*face=nullptr;U length=0;
    const bool direct=API_IS(name,"CreateFontA");
    if(direct){face=ReadString(a[13]);if(state==Failed)return 0;length=strlen(face)+1;}
    else {
        unsigned char source[60];
        if(!a[0]||!virtual_memory.Access(a[0],60,false)){last_error=87;return 0;}
        if(!Read(a[0],source,60))return 0;
        Win98FontCreate::Narrow(source,indirect);
        for(;length<32&&indirect[18+length];++length){}
        if(length==32){last_error=87;return 0;}++length;face=reinterpret_cast<const char*>(indirect+18);
    }
    const U capacity=length+20;
    auto*logical=static_cast<unsigned char*>(calloc(capacity,1));
    if(!logical){last_error=8;return 0;}
    if(direct)Win98FontCreate::Construct(a,reinterpret_cast<const unsigned char*>(face),length,logical);
    else memcpy(logical,indirect,18+length);
    struct E {
        unsigned char*logical;U allocated=0,requested=0;
        U PendingDeletes(){return 0;}void PurgePendingFonts(){}
        U Allocate(const unsigned char*,U bytes){allocated=bytes;return 1;}
        // The native FNT catalogue has no Win16 global atoms or alias table.
        // Unknown names use the original atom-zero branch; no fabricated
        // Symbol/OEM/alias entries are installed here.
        U FindAtom(const unsigned char*){return 0;}U SpecialAtom(U){return 0;}
        U SpecialCharset(){return 0;}void Alias(U,U&,U&){}
        U Byte(U,U index){return logical[index];}void SetByte(U,U index,U v){logical[index]=v;}
        void RequestedCharset(U,U at,U value){logical[at]=value;requested=value;}
    }env{logical};
    if(!Win98FontCreate::Create(env,logical,length)){free(logical);return 0;}
    unsigned char data[60]{},legacy[50]{};
    const U copied=18+length<50?18+length:50;memcpy(legacy,logical,copied);
    Win98FontCreate::Expand(legacy,data);
    int height=static_cast<short>(Win98FontCreate::Word(logical));
    // Keep the existing physical raster provider's limits explicit. A logical
    // request for weight is retained, while metrics describe the actual FNT.
    // Italic, underline and strikeout go into the FNT header of the physical
    // font like GDI's copy for the driver; ExtTextOutA draws the two lines.
    // Win98 never refuses such a font. Super Collapse asks for a bold
    // underlined 14-pixel face while it loads, and with an answer of zero its
    // menu never came up. A width goes to the TrueType realization
    // ( scales the em); a raster face keeps its own width, the
    // choice GDI's mapper would weigh with its width penalty. Escapement and
    // orientation are supported for quarter-turn TrueType text below.
    const int escapement=static_cast<short>(Win98FontCreate::Word(logical+4));
    const int orientation=static_cast<short>(Win98FontCreate::Word(logical+6));
    if(escapement%900 || (orientation && orientation!=escapement)){
        free(logical);last_error=50;return 0;
    }
    const int width=static_cast<short>(Win98FontCreate::Word(logical+2));
    const bool italic=logical[10]!=0,underline=logical[11]!=0,strikeout=logical[12]!=0;
    if(auto*slot=fonts.Free()){
        auto&f=*slot;
        const char*physical=reinterpret_cast<const char*>(logical+18);
        bool simulate_bold=false;
        // Marlett is a hidden system font and need not occur in the public
        // Fonts registry list. Use its installed TTF through the same driver
        // as other outline fonts; a synthetic FNT cannot answer GetFontData
        // or retain glyph overhangs. Only standalone tests and the Pi keep cells.
        const bool marlett=FaceEqual(physical,"Marlett");
        if(marlett&&!GdiFonts::Installed("MARLETT.TTF"))AddFontResourceFile("MARLETT.TTF");
        const auto*face=GdiFonts::Match(physical,height,Win98FontCreate::Word(logical+8),italic,logical[13]);
        if(escapement && (!face || !face->truetype)){free(logical);last_error=50;return 0;}
        if(marlett&&!face){
#ifndef NEO98_RUNTIME_CONTENT
            unsigned bytes=0;auto*fnt=MarlettFont(height,bytes);
            f.handle=fnt?Gdi::CreateFontMemory(fnt,bytes):0;free(fnt);
#else
            f.handle=0;last_error=2; // Missing installed font; no bundled copy.
#endif
        }
        else if(face){
            if(face->truetype){
                // Win98's own driver realizes the face at the requested size,
                // width, weight and slant (src/gdi_truetype.cpp); the lines
                // and the slant are part of the realization.
                const auto*tt=GdiTrueType::Realize(face->truetype,height,width,Win98FontCreate::Word(logical+8),
                                                    italic,underline,strikeout,false,escapement);
                f.handle=tt?Gdi::CreateFontOutline(tt->view):0;
                if(f.handle){memcpy(f.logfont,data,60);f.requested_charset=env.requested;}
                else last_error=8;
                free(logical);return f.handle;
            }
            f.handle=Gdi::CreateFontView(face->font.data,face->font.length);
            // a raster face below FW_BOLD is made bold when
            // the request is 550 or more.
            const unsigned requested=Win98FontCreate::Word(logical+8);
            if(f.handle&&requested>=550&&Win98FontCreate::Word(face->font.data+0x53)<700)simulate_bold=true;
        }
        // A name no installed face carries. The former provider policy
        // stays: the Fixedsys names go to Fixedsys, everything else to the
        // MS Sans Serif size nearest to the request. GDI's own mapper
        // with its penalties is not implemented.
        else if(FaceEqual(physical,"Fixedsys")||FaceEqual(physical,"Courier")||
           FaceEqual(physical,"Courier New")||FaceEqual(physical,"Terminal")){
            const auto*face=GdiFonts::RoleFace(GdiFonts::FixedRole);
            f.handle=face?Gdi::CreateFontView(face->font.data,face->font.length):Gdi::CreateFontMemory(font_bytes,font_size);
        }
        else if(const auto*face=GdiFonts::Match("MS Sans Serif",height?height:13,400,false,1)){
            f.handle=Gdi::CreateFontView(face->font.data,face->font.length);
            if(f.handle&&Win98FontCreate::Word(logical+8)>=550&&Win98FontCreate::Word(face->font.data+0x53)<700)
                simulate_bold=true;
        }
        else {
            const int heights[]={13,16,20,24,29,37};U best=0;int distance=0x7fffffff,target=height;
            if(target<0)target=-target;if(!target)target=13;
            for(U i=0;i<6;++i){int d=heights[i]-target;if(d<0)d=-d;
                if(d<distance){distance=d;best=i;}}
            f.handle=Gdi::CreateFontResource(best);
        }
        if(f.handle&&(italic||underline||strikeout||simulate_bold)){
            const auto plain=f.handle;
            f.handle=Gdi::CreateFontVariant(plain,italic,underline,strikeout,simulate_bold);
            Gdi::DeleteObject(plain);
        }
        if(f.handle){memcpy(f.logfont,data,60);f.requested_charset=env.requested;}
        else last_error=8;
        free(logical);return f.handle;
    }
    free(logical);last_error=8;return 0;
}
} // namespace X86
