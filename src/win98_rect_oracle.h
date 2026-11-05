// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_rect.h"
namespace Win98 {
struct OriginalVector {
    unsigned operation;
    Rect before[3];
    int pointers[3];
    int args[4];
    unsigned result;
    Rect after[3];
};
const OriginalVector &OriginalCase(unsigned index);
}
