// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_draw_text.h"
#include "win98_draw_text_gdi.h"
#include "win98_draw_caption.h"
namespace X86 {
unsigned Application::DrawTextApi(ApiName name,const unsigned *a)
{
    using namespace Win98DrawText;
    unsigned flags=a[4], public_params[5]={}, parameter=0;
    Params params;Params *p=nullptr;
    // USER32: DrawTextA encodes its tab width in the second flag byte.
    if(API_IS(name,"DrawTextA")){
        if(flags&0x80){params.tab=(flags>>8)&255;flags&=~0xff00u;p=&params;}
    }else if((parameter=a[5])){
        if(!Read(parameter,public_params,sizeof public_params))return 0;
        // /2072..208A: DWORD size adjustment; signed WORD fields.
        params={public_params[0]-8,W(public_params[1]),W(public_params[2]),W(public_params[3]),public_params[4]&65535};p=&params;
    }
    Gdi::RECT rect={};if(!Read(a[3],&rect,sizeof rect))return 0;
    Rect r={W(rect.left),W(rect.top),W(rect.right),W(rect.bottom)};
    const unsigned count=a[2]&65535;
    auto finish=[&](unsigned result){
        rect={r.left,r.top,r.right,r.bottom};Store(a[3],&rect,sizeof rect);
        if(parameter){public_params[0]=params.size+8;public_params[1]=params.tab;public_params[2]=params.left;public_params[3]=params.right;public_params[4]=params.drawn&65535;Store(parameter,public_params,sizeof public_params);}
        return result;
    };
    if(p&&p->size!=12)return finish(0);
    if(!count)return finish(1);
    // Native unsupported providers. These are failures, not ignored layout flags.
    if((flags&~0xafffu)||(flags&0x1000)||((flags&0x8000)&&!(flags&0x800))){last_error=50;return 0;}
    if(!Gdi::ValidDC(a[0])){last_error=6;return 0;}
    char text[8192]={};unsigned length=count;
    if(count==65535){const char*s=ReadString(a[1]);length=strlen(s);if(length>=sizeof text){last_error=50;return 0;}memcpy(text,s,length);}
    else if(count){if(count>=sizeof text){last_error=50;return 0;}if(!Read(a[1],text,count))return 0;if(virtual_memory.Access(a[1]+count,1,false))Read(a[1]+count,text+count,1);}
    Win98DrawTextGdi::Environment e{last_error,a[0]};
    Gdi::TEXTMETRICA check={};if(count&&Gdi::GetTextMetricsA(a[0],&check)&&
        (check.tmCharSet==128||check.tmCharSet==129||check.tmCharSet==130||check.tmCharSet==134||check.tmCharSet==136)){last_error=50;return 0;}
    int saved=Gdi::SaveDC(a[0]);if(!saved)return 0;
    if(!(flags&0x100))Gdi::IntersectClipRect(a[0],r.left,r.top,r.right,r.bottom);
    // USER fit and1224:132F/1395 end-ellipsis assembly. This bounded
    // adapter supports single-line NOPREFIX; path/word ellipsis remain absent.
    // A single line takes the ellipsis here; several lines take it per line
    // inside Layout (Win98DrawText::EllipsisLine).
    if((flags&0x8000)&&(flags&0x20)){
        Gdi::TEXTMETRICA font{};Gdi::GetTextMetricsA(a[0],&font);
        int width=W(r.right-r.left-(p?p->left+p->right:0));
        if(!Win98Caption::EndEllipsis(e,text,length,sizeof text,width,font.tmOverhang)){
            Gdi::RestoreDC(a[0],saved);last_error=50;return 0;
        }
    }
    int result=Layout(e,text,length,count==0,r,(flags&0x20)?flags&~0x8000u:flags,p);
    Gdi::RestoreDC(a[0],saved);
    if(!(flags&0x400))User::DrawingChanged(a[0]);
    return finish(e.okay?unsigned(result):0);
}
}
