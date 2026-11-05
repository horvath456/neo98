// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_arrays.h"
#include <string.h>
// New address-space adapter: native controls reuse original DPA merge sorting.
namespace NativeSort
{
inline bool Sort(unsigned *values, unsigned count, void *context, int (*compare)(void *, unsigned, unsigned))
{
    if (count > 512)
        return false;
    struct Memory final : Win98Arrays::Memory
    {
        unsigned header[5], scratch[512] = {};
        unsigned *values;
        void *context;
        int (*compare)(void *, unsigned, unsigned);
        Memory(unsigned *v, unsigned n, void *c, int (*fn)(void *, unsigned, unsigned))
            : header{n, 0x200, 1, n, 8}, values(v), context(c), compare(fn)
        {
        }
        void *Address(unsigned p, unsigned n)
        {
            if (p >= 0x100 && p <= 0x114 && n <= 0x114 - p)
                return (unsigned char *)header + p - 0x100;
            if (p >= 0x200 && p <= 0x200 + header[0] * 4 && n <= 0x200 + header[0] * 4 - p)
                return (unsigned char *)values + p - 0x200;
            if (p >= 0x2000 && p <= 0x2800 && n <= 0x2800 - p)
                return (unsigned char *)scratch + p - 0x2000;
            return nullptr;
        }
        bool Read(unsigned p, void *out, unsigned n) override
        {
            void *a = Address(p, n);
            if (!a)
                return false;
            memcpy(out, a, n);
            return true;
        }
        bool Write(unsigned p, const void *in, unsigned n) override
        {
            void *a = Address(p, n);
            if (!a)
                return false;
            memcpy(a, in, n);
            return true;
        }
        unsigned Resize(unsigned heap, unsigned old, unsigned n) override
        {
            return heap == 1 && !old && n <= 2048 ? 0x2000 : 0;
        }
        bool Free(unsigned heap, unsigned p) override { return heap == 1 && p == 0x2000; }
        int Compare(unsigned, unsigned a, unsigned b, unsigned) override { return compare(context, a, b); }
    } memory(values, count, context, compare);
    unsigned args[] = {0x100, 1, 0};
    return Win98Arrays::Call(memory, 338, args) != 0;
}
inline int CompareText(const char *a, const char *b)
{
    for (;; ++a, ++b)
    {
        unsigned x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'a' && x <= 'z')
            x -= 32;
        if (y >= 'a' && y <= 'z')
            y -= 32;
        if (x != y)
            return x < y ? -1 : 1;
        if (!x)
            return 0;
    }
}
} // namespace NativeSort
