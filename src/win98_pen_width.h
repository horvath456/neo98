// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_dc_map.h"
namespace Win98PenWidth {
// GDI: realize logical width through the horizontal extents,
// saturate the absolute WORD result, then apply the DC's internal shift.
inline unsigned Device(unsigned width,unsigned viewport,unsigned window,unsigned shift)
{
    const int scaled=Win98DcMap::MultiplyDivide(width,viewport,window).value;
    return unsigned(Win98DcMap::AbsoluteSaturated(scaled))>>(shift&31);
}
}
