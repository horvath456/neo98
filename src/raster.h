// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "win98_rect.h"

// Drawing primitives use the implemented USER32 geometry routines.
struct Surface {
    unsigned *pixels;
    int width;
    int height;
    int stride;
};

using Rect = Win98::Rect;

void FillRect(Surface &surface, Rect rect, unsigned color);
void DrawEdge(Surface &surface, Rect rect, bool pressed);
bool RasterSelfTest();
