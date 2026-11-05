// SPDX-License-Identifier: GPL-2.0-or-later
// USER32; USER;
// USER.
#pragma once
namespace Win98FocusRect {
inline int Word(unsigned n) { return int(n&0x7fff)-int(n&0x8000); }
struct Rect { int left,top,right,bottom; };
struct Extent { int x,y; };
inline Rect Thunk(Rect r) { return {Word(r.left),Word(r.top),Word(r.right),Word(r.bottom)}; }
inline unsigned PatternBit(unsigned x,unsigned y) { return ((0x55u<<(y&1))>>(7-(x&7)))&1; }
// Providers model SelectObject, GetViewportExt/GetWindowExt and PatBlt. USER
// ignores individual PatBlt results, restoring the selected brush even when
// dimensions reject drawing. The native provider separately reports failures.
template<class E> unsigned Frame(E&e,Rect r,unsigned brush,unsigned rop=0x005a0049) {
    const unsigned previous=e.Select(brush);
    const int width=Word(unsigned(r.right)-unsigned(r.left));
    const int height=Word(unsigned(r.bottom)-unsigned(r.top));
    // JLE follows the final SUB in USER, including signed overflow.
    if(r.bottom>r.top) {
        const auto viewport=e.Viewport(),window=e.Window();
        const int dx=Word(unsigned(viewport.x)^unsigned(window.x))<0?-1:1;
        const int dy=Word(unsigned(viewport.y)^unsigned(window.y))<0?-1:1;
        if(Word(unsigned(width)^unsigned(dx))>=0&&Word(unsigned(height)^unsigned(dy))>=0) {
            e.Pat(r.left,r.top,width,dy,rop);
            e.Pat(r.left,Word(unsigned(r.bottom)-unsigned(dy)),width,dy,rop);
            const int y=Word(unsigned(r.top)+unsigned(dy));
            const int h=Word(unsigned(height)-unsigned(dy)-unsigned(dy));
            e.Pat(r.left,y,dx,h,rop);
            e.Pat(Word(unsigned(r.right)-unsigned(dx)),y,dx,h,rop);
        }
    }
    return previous?e.Select(previous):0;
}
}
