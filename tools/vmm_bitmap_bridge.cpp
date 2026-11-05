// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_vmm_bitmap.h"
extern "C"
{
    unsigned vmm_bitmap_claim(unsigned *words, unsigned count) { return Win98VmmBitmap::Claim(words, count); }
    void vmm_bitmap_release(unsigned *words, unsigned identifier)
    {
        Win98VmmBitmap::Release(words, identifier);
    }
    unsigned vmm_bitmap_last(const unsigned *words, unsigned count)
    {
        return Win98VmmBitmap::Last(words, count);
    }
}
