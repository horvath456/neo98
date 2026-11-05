// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "dibeng.h"
#include "win98_rect.h"

namespace DibText {
using Rect = Win98::Rect;
struct Font {
    const unsigned char *data = nullptr;
    unsigned length = 0;
    // A realized TrueType font (src/gdi_truetype.h) whose header and widths
    // `data` is; null for a raster FNT.
    const void *outline = nullptr;
    bool Load(const unsigned char *, unsigned bytes);
    bool FaceName(char (&output)[32]) const;
    unsigned Height() const;
    unsigned Points() const;
    unsigned Ascent() const;
    unsigned Index(unsigned char code) const;
    unsigned Width(unsigned index) const;
    unsigned Bits(unsigned index) const;
};
// Byte layout of Win98's driver DRAWMODE, with physical pixel colors.
struct DrawMode {
    unsigned short rop2 = 13, background_mode = 1;
    unsigned background = 0xffffff, foreground = 0;
    short total_break_extra = 0, break_extra = 0, break_error = 0,
          break_remainder = 0, break_count = 0, char_extra = 0;
    unsigned logical_background = 0, logical_foreground = 0;
};
static_assert(sizeof(DrawMode) == 32, "Win98 DRAWMODE layout");
// Native implementation of DIB_EXTTEXTOUT's raster-font path. A negative count
// measures only and returns height:width; drawing returns zero. Unsupported or
// invalid native inputs return 0x80000000. Extents retain 16-bit arithmetic.
unsigned ExtTextOut(DibEng::Bitmap *, int x, int y, const Rect &clip,
                    const unsigned char *text, int count, const Font &, DrawMode &,
                    const short *advances = nullptr, const Rect *opaque = nullptr, unsigned options = 0);
// The text output of a font with an outline (src/gdi_truetype.cpp), which the
// TrueType realization installs when it starts; same contract as ExtTextOut.
using OutlineText = unsigned (*)(DibEng::Bitmap *, int x, int y, const Rect &clip, const unsigned char *text,
                                 int count, const Font &, DrawMode &, const short *advances, const Rect *opaque,
                                 unsigned options);
void SetOutlineText(OutlineText);
unsigned FontCount();
Font OriginalFont(unsigned index);
unsigned OriginalTestCount();
unsigned OriginalSelfTest(unsigned *actual = nullptr);
}
