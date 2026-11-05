// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "win98_font_query.h"
#include "gdi_font_catalog.h"
#include "gdi_truetype.h"
#include <string.h>
namespace X86 {
namespace {
bool SameInstalledFace(const char*a,const char*b){
    for(;;){unsigned x=static_cast<unsigned char>(*a++),y=static_cast<unsigned char>(*b++);
        if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;
        if(x!=y)return false;if(!x)return true;
    }
}
}
unsigned Application::FontQueryApi(ApiName name,const unsigned*a)
{
    using U=unsigned;
    // GDI, reached through the thunk table with CL 16h. The whole answer comes out of three words of
    // GDI's own data: the size 6, the flags and the language. The flags are
    // TT_AVAILABLE from the rasterizer's own segment at DGROUP +2Ah and
    // TT_ENABLED from bit 0 of the word at +34h, and a GDI whose start-up load
    // of the rasterizer failed keeps both at zero ( jumps past them).
    // Without a TrueType file this port reports zero, like a GDI whose
    // rasterizer did not load. A size of
    // zero or less answers FALSE without writing anything, and the
    // original copies exactly cb bytes out of its six-byte record with no
    // upper bound; this port stops at the record.
    if(API_IS(name,"GetRasterizerCaps")){
        const int size=int(a[1]);
        if(size<=0)return 0;
        if(!a[0]||!virtual_memory.Access(a[0],unsigned(size)<6?unsigned(size):6u,true))return 0;
        const unsigned none[1]={0};
        // TT_AVAILABLE and TT_ENABLED once Win98's font driver runs and has
        // installed a file (src/gdi_truetype.cpp).
        bool outline=false;for(U i=0;i<GdiFonts::Count();++i)outline|=GdiFonts::At(i)->type==GdiFonts::TrueType;
        const unsigned short record[3]={6,(unsigned short)(outline?3:0),(unsigned short)Api("GetSystemDefaultLangID",none)};
        if(!Store(a[0],record,unsigned(size)<6?unsigned(size):6u))return 0;
        return 1;
    }
    if(API_IS(name,"GetCharWidthA")){
        const U count=a[2]-a[1]+1,first=a[1]&65535,last=a[2]&65535;
        if(count>256){last_error=87;return 0;} // Native raster/ANSI provider limit.
        bool result=false;
        if(a[3]&&count&&first<=last&&last<=255){
            int widths[256]{};result=Gdi::GetCharWidth32A(a[0],first,last,widths);
            if(result){unsigned short words[256];for(U i=0;i<count;++i)words[i]=widths[i];if(!Store(a[3],words,count*2))return 0;}
        }
        struct E{Application&app;U Read16(U p){unsigned short v=0;app.Read(p,&v,2);return v;}void Write32(U p,U v){app.Write32(p,v);}}e{*this};
        Win98FontQuery::ExpandWidths(e,a[3],count);return result;
    }
    if(API_IS(name,"GetTextFaceA")){
        // The native handle carries generation bits above the Win16 WORD.
        // Only original value fields are narrowed; the GDI provider needs the
        // complete native handle to distinguish live objects from stale ones.
        char physical[32]{};auto font=Gdi::SelectedFont(a[0]);
        if(!a[2] || !font.FaceName(physical))return 0;
        struct E {
            Application&app;const char*name;
            bool Realize(){return true;}U FindAtom(U){return 0;}
            void Alias(U,U&alias,U&charset){alias=0;charset=1;}
            bool SpecialAtom(U){return false;}U ExpectedVersion(){return 0x400;}
            U AtomName(U,U,U){return 0;}
            U Read8(U p){return static_cast<unsigned char>(name[p-1]);}
            void Write8(U p,U v){const unsigned char c=v;app.Store(p,&c,1);}
        }e{*this,physical};
        // Native raster resources have a realized face string. No font atom
        // catalogue or compatibility alias is claimed by this provider.
        const Win98FontQuery::Face face{0x400,0,1,1,0,0,0};
        return Win98FontQuery::TextFace(e,face,a[1],a[2]);
    }
    const U mode=API_IS(name,"EnumFontsA")?0:API_IS(name,"EnumFontFamiliesA")?1:2;
    U face=a[1],charset=0;
    if(mode==2){unsigned char lf[60];if(!Read(a[1],lf,sizeof lf))return 0;charset=lf[23];face=a[1]+28;}
    struct E {
        Application&app;
        U Snapshot(U dc,U face,U charset,U mode,U&count,U&records,U&lease){
            if(!Gdi::SelectedFont(dc).data)return 0;
            char filter[32]{};if(face){const char*s=app.ReadString(face);if(app.state==Failed)return 0;U i=0;for(;i<31&&s[i];++i)filter[i]=s[i];if(s[i])return 1;}
            const bool named=face&&(mode!=2||filter[0]);
            // Every face of the catalogue (src/gdi_font_catalog.cpp): each size
            // of a raster family, one record per TrueType file with
            // TRUETYPE_FONTTYPE (4); GDI enumerates an outline face once, not
            // per size.
            struct Row{unsigned char bytes[264];};
            const U installed=GdiFonts::Count();
            // A DC over a printer driver lists the driver's device fonts first
            // (EnumDFonts, GDI) and then GDI's TrueType faces only; a
            // raster face needs TC_RA_ABLE in dpText, which a
            // printer driver such as PSCRIPT does not carry.
            const bool printer=app.PrinterDC(dc);
            constexpr U DeviceRows=256;
            Row*rows=new Row[(installed?installed:1)+(printer?DeviceRows:0)]();U*seen=new U[installed*2+2](),seen_count=0;
            if(printer)count=app.PrintEnumDeviceFonts(dc,named?filter:nullptr,rows[0].bytes,DeviceRows);
            for(U i=0;i<installed;++i){
                const auto&entry=*GdiFonts::At(i);const auto&font=entry.font;
                if(printer&&entry.type!=GdiFonts::TrueType)continue;
                char physical[32]{};Gdi::TEXTMETRICA metrics{};if(!font.FaceName(physical)||!Gdi::FontMetrics(font,&metrics))continue;
                if(named&&!SameInstalledFace(filter,physical))continue;
                if(mode==2&&charset!=1&&charset!=metrics.tmCharSet)continue;
                bool repeated=false;
                if(entry.type==GdiFonts::TrueType)for(U j=0;j<i;++j)if(GdiFonts::SameFace(GdiFonts::At(j)->file,entry.file))repeated=true;
                if(repeated)continue;
                U atom=1;for(U j=0;j<i;++j){char other[32]{};if(GdiFonts::At(j)->font.FaceName(other)&&SameInstalledFace(physical,other)){atom=j+1;break;}atom=j+2;}
                if(!named){if(!Win98FontQuery::FamilyCandidate(atom,metrics.tmCharSet,charset,mode!=2||charset==1,seen,seen_count))continue;seen[seen_count*2]=atom;seen[seen_count*2+1]=metrics.tmCharSet;++seen_count;}
                auto *p=rows[count++].bytes;auto put=[&](U at,U value){memcpy(p+at,&value,4);};
                put(0,metrics.tmHeight);put(4,metrics.tmAveCharWidth);put(16,metrics.tmWeight);
                p[20]=metrics.tmItalic;p[21]=metrics.tmUnderlined;p[22]=metrics.tmStruckOut;p[23]=metrics.tmCharSet;p[27]=metrics.tmPitchAndFamily;
                memcpy(p+28,physical,strlen(physical)+1);memcpy(p+60,physical,strlen(physical)+1);memcpy(p+188,&metrics,sizeof metrics);put(260,entry.type);
                // A TrueType file names its full name and its style (name IDs 4
                // and 2), and NEWTEXTMETRIC carries NTM_ITALIC (1), NTM_BOLD
                // (20h) or NTM_REGULAR (40h); COMMDLG's font dialog lists the
                // styles by these. An FNT supplies no style
                // string, and neither supplies a font signature or outline
                // metrics; those fields stay empty.
                if(const auto*outline=entry.type==GdiFonts::TrueType?entry.truetype:nullptr){
                    auto bounded=[](const char*text,U limit){U n=0;while(n<limit&&text[n])++n;return n;};
                    if(outline->full[0]){memset(p+60,0,64);memcpy(p+60,outline->full,bounded(outline->full,63));}
                    memcpy(p+124,outline->style,bounded(outline->style,31));
                    const U bold=outline->weight>=700?0x20u:0u,italic=outline->italic?1u:0u;
                    put(188+56,bold|italic?bold|italic:0x40u);
                }
                // ENUMLOGFONTEX.elfScript: GDI.EXE string 1000 plus the
                // character set ("Western", "Central European",...), string
                // 1256 "Unknown" for a set without one.
                if(mode==2)app.GdiScriptName(metrics.tmCharSet,reinterpret_cast<char*>(p+156),32);
            }
            delete[]seen;
            // The original loop accepts a successful empty snapshot and frees it.
            // One allocation per call keeps nested enumeration and early-stop
            // callbacks independent of the selected font and live object table.
            lease=app.Allocate(count?count*264:4);if(!lease){delete[]rows;app.last_error=8;return 0;}records=lease;
            if(count&&!app.Store(records,rows,count*264)){delete[]rows;app.Free(lease);lease=0;return 0;}
            delete[]rows;return 1;
        }
        U Read32(U p){return app.Read32(p);}
        U Callback(U proc,U lf,U tm,U type,U param){
            // A 16-bit procedure of GDI.70 or GDI.330 arrives as a mark
            // (src/win16_user.cpp); it gets the 16-bit records.
            // COMMDLG's font dialog of the 16-bit side, built natively
            // (src/win16_choose_font.inc), takes the 32-bit record.
            if(proc==Application::Win16FontEnumToken)return app.Win16FontEnumCallback(lf,type);
            if(app.Win16IsMark(proc)){unsigned char record[264];if(!app.Read(lf,record,sizeof record))return 0;(void)tm;return app.Win16FontCallback(app.Win16MarkPointer(proc),record,type,param);}
            const U args[]={lf,tm,type,param};return app.Call(proc,args,4);}
        void Release(U lease){if(lease)app.Free(lease);}
    }e{*this};
    return Win98FontQuery::Enumerate(e,a[0],face,charset,a[2],a[3],mode);
}
} // namespace X86
