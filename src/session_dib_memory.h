// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace SessionDibMemory
{
constexpr unsigned Alignment = 0x4000;

// The session maps only the aligned bitmap span. Keep the allocation base
// separately so it can be freed after the last process drops the mapping.
inline unsigned char *Allocate(unsigned bytes, void *&storage)
{
    storage = nullptr;
    if (!bytes || (bytes & (Alignment - 1))) return nullptr;
    unsigned char *bits;
    if (posix_memalign(&storage, Alignment, bytes)) return nullptr;
    bits = static_cast<unsigned char *>(storage);
    memset(bits, 0, bytes);
    return bits;
}
}
