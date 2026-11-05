// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "win98_font_width.h"
#include "win98_font_query.h"
#include "gdi_truetype.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/font_charset_tables.h"
#endif
#include <string.h>
namespace X86 {
unsigned Application::FontWidthApi(ApiName name,const unsigned*a){
    using U=unsigned;const U count=a[2]-a[1]+1,first=a[1]&65535,last=a[2]&65535;
    // Guest memory and the native raster provider bound the original WORD
    // loop. In particular last==FFFF would wrap indefinitely in GDI.
    if(count>65535){last_error=87;return 0;}
    struct Memory{Application&app;
        U Read16(U p){unsigned short v=0;app.Read(p,&v,2);return v;}
        void Write16(U p,U v){const unsigned short w=v;app.Store(p,&w,2);}
        void Write32(U p,U v){app.Write32(p,v);}
    }memory{*this};
    U result=0;
    if(API_IS(name,"GetCharABCWidthsA")){
        // A TrueType font passes and gets its A, B and C from the
        // glyph metrics of the driver (src/gdi_truetype.cpp),
        // stored as the Win16 ABC that GDI32 widens below. Codes above FFh
        // have no ANSI glyph in this port and keep the raster answer.
        if(const auto*tt=GdiTrueType::FontOf(Gdi::SelectedFont(a[0]))){
            if(first>last||last>0xff||!a[3]){last_error=87;return 0;}
            int*abc=new int[count*3];
            if(GdiTrueType::AbcWidths(*tt,first,last,abc)){
                result=1;
                for(U i=0;i<count&&result;++i){
                    const unsigned short w[3]={(unsigned short)abc[i*3],(unsigned short)abc[i*3+1],(unsigned short)abc[i*3+2]};
                    if(!Store(a[3]+i*6,w,6))result=0;
                }
            }
            delete[]abc;
            Win98FontWidth::ExpandAbc(memory,a[3],count);return result;
        }
        if(Gdi::SelectedFont(a[0]).data){
            struct E{void Update(){}bool Realize(){return true;}bool RealizeOutline(){return false;}
                U FontFlags(){return 0;}U OutlineQuery(){return 0;}}e;
            // Real FNT resources are raster fonts. Their DC has neither the
            // outline flag nor a realized outline object/bearing engine.
            const Win98FontWidth::AbcState state{0,0,1,0,0,0,0};
            result=Win98FontWidth::AbcQuery(e,state);
        }
        Win98FontWidth::ExpandAbc(memory,a[3],count);return result;
    }
    int probe=0;Gdi::TEXTMETRICA metrics{};
    if(a[3]&&count&&first<=last&&last!=65535&&Gdi::GetTextMetricsA(a[0],&metrics)&&
       metrics.tmCharSet==0&&Gdi::GetCharWidth32A(a[0],0,0,&probe)){
        struct E:Memory{
            U dc;
            bool NeedsUpdate(){return false;}void Update(){}bool Realize(){return true;}
            U Allocate(U size){return app.Allocate(size);}void Free(U lease){app.Free(lease);}
            U Glyphs(U,U,U){return 0xffffffffu;}
            U Convert(U charset,U count,U source,U output,U flags){
                struct C{Application&app;U character;
                    U CharsetTable(U charset){return charset==0?0x100:0;}
                    U Read16(U p){return p==0?character:OriginalGdiAnsiCharset[(p-0x100)/2];}
                    void Write8(U p,U v){const unsigned char b=v;app.Store(p,&b,1);}
                }c{app,source};
                return Win98FontWidth::ConvertBytes(c,charset,count,0,output,flags);
            }
            U ConvertCharacter(U character,U&flags){
                const Win98FontWidth::RunState state{0,0,0,0,0,0,0,0};
                return Win98FontWidth::ConvertRun(*this,state,character,1,1,flags);
            }
            U Measure(U lease,U){unsigned char ch=0;app.Read(lease,&ch,1);int width=0;
                Gdi::GetCharWidth32A(dc,ch,ch,&width);return width;}
            bool Mapped(){return false;}U MapWidth(U value){return value;}
        }e{{*this},a[0]};
        result=Win98FontWidth::Widths(e,first,last,a[3]);
    }
    Win98FontQuery::ExpandWidths(memory,a[3],count);return result;
}
} // namespace X86
