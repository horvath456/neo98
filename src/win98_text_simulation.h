// SPDX-License-Identifier: GPL-2.0-or-later
// GDI's text simulations for a font that lacks the attribute itself: italic,
// underline and strikeout in Windows 98. The display
// driver (DIBENG DIB_EXTTEXTOUT) knows none of them; GDI calls it once per band
// for italic and draws the two lines itself with a one-pixel pen afterwards.
//
#pragma once
#include "dib_text.h"
namespace Win98TextSimulation
{
inline int Word(const unsigned char *p, unsigned at) { return int(p[at] | unsigned(p[at + 1]) << 8); }
inline int Signed(const unsigned char *p, unsigned at) { return short(Word(p, at)); }
// A realized TrueType font (src/gdi_truetype.cpp) marks its FNT header with
// "TT" in dfReserved1 and carries the line rows of behind it.
inline bool TrueTypeStrike(const DibText::Font &f)
{
    return f.data && f.length >= 148 && Word(f.data, 0) == 0x300 && f.data[0x84] == 'T' && f.data[0x85] == 'T';
}
// the realization gives a simulated italic raster font an
// overhang of (height - 1) / 2, unsigned shift.
inline int ItalicOverhang(const DibText::Font &f)
{
    return f.data && f.data[80] && !TrueTypeStrike(f) ? int((f.Height() - 1) >> 1) : 0;
}
// a simulated bold raster font (Gdi::CreateFontVariant, marked "SB"
// in dfReserved1) has one pixel of overhang.
inline int BoldOverhang(const DibText::Font &f)
{
    return f.data && f.length >= 148 && Word(f.data, 0) == 0x300 && f.data[0x84] == 'S' && f.data[0x85] == 'B'
               ? f.data[0x8e] : 0;
}
// italic is drawn in bands of two rows from the top of the
// cell; band `row` (even) is moved right by (height - row - 1) / 2. The first
// band reaches up to the clip top, the last one down to the clip bottom.
inline bool ItalicBands(const DibText::Font &f) { return f.data && f.data[80] && !TrueTypeStrike(f); }
inline int ItalicShift(int height, int row) { return (height - row - 1) / 2; }
// for one line. The thickness is height / 12, at least one. A raster underline sits one row below the ascent plus half the
// thickness, but moves up so that it stays inside the cell.
// The strikeout lies two thirds of the way from the internal leading down to
// the baseline. A TrueType font brings its own rows
// (the realized font metrics). `top` is relative to the cell.
inline bool LineMetrics(const DibText::Font &f, bool strikeout, int &top, int &thickness)
{
    if (!f.data)
        return false;
    if (TrueTypeStrike(f))
    {
        top = Signed(f.data, strikeout ? 0x8a : 0x86);
        thickness = Signed(f.data, strikeout ? 0x8c : 0x88);
        return thickness > 0;
    }
    const int height = int(f.Height()), ascent = int(f.Ascent()), leading = Word(f.data, 0x4c);
    thickness = height > 12 ? height / 12 : 1;
    int position;
    if (!strikeout)
        position = ascent + thickness + 1 > height ? height + (-1 - thickness) / 2
                                                   : thickness / 2 + ascent + 1;
    else
        position = leading + (2 * (ascent - leading)) / 3;
    top = position - thickness / 2;
    return true;
}
// an italic line starts where its row of the slanted text
// starts: (height - top) / 2 to the right.
inline int LineShift(const DibText::Font &f, int top)
{
    return ItalicBands(f) ? (int(f.Height()) - top) / 2 : 0;
}
} // namespace Win98TextSimulation
