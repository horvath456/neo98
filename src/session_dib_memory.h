// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace SessionDibMemory
{
constexpr unsigned Alignment = 0x4000;

// The session maps only the aligned bitmap span. Keep the allocation base
// separately so Circle can free it after the last process drops the mapping.
inline unsigned char *Allocate(unsigned bytes, void *&storage)
{
    storage = nullptr;
    if (!bytes || (bytes & (Alignment - 1))) return nullptr;
    unsigned char *bits;
#ifdef __circle__
    if (size_t(bytes) > SIZE_MAX - (Alignment - 1)) return nullptr;
    storage = malloc(size_t(bytes) + Alignment - 1);
    if (!storage) return nullptr;
    const uintptr_t base = reinterpret_cast<uintptr_t>(storage);
    bits = reinterpret_cast<unsigned char *>((base + Alignment - 1) & ~uintptr_t(Alignment - 1));
#else
    if (posix_memalign(&storage, Alignment, bytes)) return nullptr;
    bits = static_cast<unsigned char *>(storage);
#endif
    memset(bits, 0, bytes);
    return bits;
}
}
