// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gdi.h"

namespace Win98Draw {
// Native representations of USER's initialized display data and GDI boundary.
struct Painter {
    void *context;
    void (*fill)(void *, int x, int y, int width, int height, unsigned rgb);
    unsigned window = 0xffffff, frame = 0, face = 0xc0c0c0, shadow = 0x808080,
             highlight = 0xffffff, dark_shadow = 0x404040, light = 0xc0c0c0;
    int border_width = 1, border_height = 1, depth = 32;
    void (*pattern)(void *,int x,int y,int width,int height,unsigned background,unsigned foreground)=nullptr;
};
// Behavior compatible with USER.EXE and diagonal helpers.
bool DrawEdge(Painter &, Gdi::RECT *, unsigned edge, unsigned flags);
bool DrawEdge(Gdi::HDC, Gdi::RECT *, unsigned edge, unsigned flags);
// Original shared button-frame helper at USER.EXE.
bool ButtonFrame(Painter &,Gdi::RECT *,unsigned state,unsigned flags);
bool ButtonFrame(Gdi::HDC,Gdi::RECT *,unsigned state,unsigned flags);
unsigned OriginalSelfTest();
unsigned OriginalTestCount();
}
