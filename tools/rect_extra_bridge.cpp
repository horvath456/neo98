// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_rect.h"
extern "C" int rect_extra(unsigned function, Win98::Rect *out, const Win98::Rect *a, const Win98::Rect *b)
{
    return function ? Win98::UnionRect(out, a, b) : Win98::SubtractRect(out, a, b);
}
