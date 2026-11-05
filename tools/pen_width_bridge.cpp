// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_pen_width.h"
extern "C" unsigned pen_width(unsigned width,unsigned viewport,unsigned window,unsigned shift)
{
    return Win98PenWidth::Device(width,viewport,window,shift);
}
