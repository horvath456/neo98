// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_paint.h"
extern "C" void paint_expand(const Win98Paint::Paint16 *in, Win98Paint::Paint32 *out)
{
    Win98Paint::Expand(*in, *out);
}
extern "C" void paint_narrow(const Win98Paint::Paint32 *in, Win98Paint::Paint16 *out)
{
    Win98Paint::Narrow(*in, *out);
}
