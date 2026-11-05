// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// The GDI side of the implemented DrawText layout (src/win98_draw_text.h):
// extents, metrics, mapping signs, text output and the accelerator underline.
// DrawTextA and DrawTextExA use it (src/x86_draw_text.inc), and so does the
// native STATIC control, which lays its text out with DrawText as USER does.
#include "gdi.h"
#include "win98_draw_text.h"
#include <string.h>
namespace Win98DrawTextGdi {
using namespace Win98DrawText;
struct Environment {
        unsigned &last_error;unsigned dc;bool okay=true;Gdi::TEXTMETRICA metrics={};Metrics m={};
        int Extent(const char*s,unsigned n){Gdi::SIZE size={};if(!Gdi::GetTextExtentPoint32A(dc,s,n,&size))okay=false;return W(size.cx);}
        Metrics MetricsFor(unsigned flags){
            if(!Gdi::GetTextMetricsA(dc,&metrics)){okay=false;return {};}
            int average=metrics.tmAveCharWidth;
            if((flags&0x2000)&&(metrics.tmPitchAndFamily&1))average=(Extent("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",52)/26+1)/2;
            struct IO final:Gdi::BitmapIO {
                Gdi::POINT value={};bool Read(unsigned,void*,unsigned)override{return false;}
                bool Write(unsigned,const void*p,unsigned n)override{if(n!=sizeof value)return false;memcpy(&value,p,n);return true;}
                bool Probe(unsigned,bool)override{return true;}void Error(unsigned)override{}void Fault(const char*)override{}
            }io;
            unsigned args[]={dc,1};Gdi::MapApi("GetWindowExtEx",args,io);auto window=io.value;
            Gdi::MapApi("GetViewportExtEx",args,io);auto viewport=io.value;
            m={W(metrics.tmHeight),W(metrics.tmExternalLeading),W(average),W(metrics.tmOverhang),((window.x^viewport.x)&0x8000)?-1:1,((window.y^viewport.y)&0x8000)?-1:1};
            return m;
        }
        void Draw(int x,int y,const char*s,unsigned n,unsigned flags){
            if(!okay)return;
            if(flags&0x800){if(!Gdi::TextOutA(dc,x,y,s,n))okay=false;return;}
            // USER has a 256-byte prefix buffer. Long prefixed runs stay an
            // explicit provider limit instead of reproducing its uninitialized tail.
            if(n>256){last_error=50;okay=false;return;}
            char stripped[257]={};auto prefix=Strip(s,n,stripped,256);
            if(prefix.count!=n-prefix.removed){last_error=50;okay=false;return;}
            if(!Gdi::TextOutA(dc,x,y,stripped,prefix.count)){okay=false;return;}
            if(prefix.underline<0||unsigned(prefix.underline)>=prefix.count)return;
            // USER: last accelerator, advance minus overhang, ascent
            // plus border and glyph width minus half-overhang. Native border=1.
            if(prefix.underline)x=W(x+Extent(stripped,prefix.underline)-m.overhang);
            int width=W(Extent(stripped+prefix.underline,1)-m.overhang/2);
            y=W(y+(metrics.tmAscent+1)*m.ysign);
            Gdi::RECT underline={x,y,W(x+width),W(y+m.ysign)};
            unsigned color=Gdi::SetTextColor(dc,0);Gdi::SetTextColor(dc,color);
            unsigned old=Gdi::SetBkColor(dc,color);
            if(!Gdi::ExtTextOutA(dc,x,y,Gdi::ETO_OPAQUE,&underline,"",0))okay=false;
            Gdi::SetBkColor(dc,old);
        }
        void NoProgress(){last_error=50;okay=false;}
};
}
