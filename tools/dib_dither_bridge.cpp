// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_dib_dither.h"
extern "C" void dib_dither(unsigned color, unsigned char *out)
{
    unsigned char pattern[64];
    Win98DibDither::Pattern(color, pattern);
    for (unsigned i = 0; i < 64; ++i) out[i] = pattern[i];
}
extern "C" unsigned dib_dither_static(unsigned color, unsigned *slot)
{
    return Win98DibDither::StaticSlot(color, *slot) ? 1 : 0;
}
