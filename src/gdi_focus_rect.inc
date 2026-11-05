// SPDX-License-Identifier: GPL-2.0-or-later
// Native DC/bitmap boundary for the original USER frame and brush algorithms.
bool DrawFocusRect(HDC handle,const RECT*rect)
{
    auto*c=DC(handle);if(!c||!rect||c->signature!=0x4f4d)return false;
#if defined(__APPLE__) || defined(__linux__)
    if (getenv("NEO98_TRACE_PAINT")) {
        const auto origin = DevicePoint(c->current, 0, 0);
        fprintf(stderr, "FOCUS dc=%x rect=%d,%d,%d,%d origin=%d,%d colors=%06x,%06x\n",
                handle, rect->left, rect->top, rect->right, rect->bottom,
                origin.x, origin.y, c->current.text_mode.foreground, c->current.text_mode.background);
    }
#endif
    auto*b=Find(c->current.bitmap,BitmapObject);if(!b||!DibEng::Valid(b->bitmap))return false;
    unsigned pixels[64];
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
        const auto bit=Win98FocusRect::PatternBit(x+unsigned(c->current.surface_origin.x),y+unsigned(c->current.surface_origin.y));
        const auto color=Win98PatternBrush::MonoColor(bit,c->current.text_mode.foreground,c->current.text_mode.background);
        if(!RealizeDeviceColor(*b,Pixel(color),pixels[y*8+x]))return false;
    }
    struct Provider {
        Context&dc;DibEng::Brush brush;bool okay=true;unsigned selected=1;
        // The temporary system brush is private to this operation. Existing
        // guest-selected brush ownership and SaveDC references stay intact.
        unsigned Select(unsigned value){const auto old=selected;selected=value;return old;}
        Win98FocusRect::Extent Viewport(){auto p=dc.current.mapping.viewport_extent;return{p.x,p.y};}
        Win98FocusRect::Extent Window(){auto p=dc.current.mapping.window_extent;return{p.x,p.y};}
        void Pat(int x,int y,int w,int h,unsigned rop){
            const auto a=DevicePoint(dc.current,x,y),b=DevicePoint(dc.current,Win98FocusRect::Word(unsigned(x)+unsigned(w)),Win98FocusRect::Word(unsigned(y)+unsigned(h)));
            const RECT r={a.x<b.x?a.x:b.x,a.y<b.y?a.y:b.y,a.x>b.x?a.x:b.x,a.y>b.y?a.y:b.y};
            if(!Paint(dc,r,rop,brush))okay=false;
        }
    } provider{*c,{0,pixels}};
    Win98FocusRect::Frame(provider,Win98FocusRect::Thunk({rect->left,rect->top,rect->right,rect->bottom}),2);
    return provider.okay;
}
