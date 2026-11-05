// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_kernel.h"
#include <cassert>
#include <cstring>
extern "C" unsigned version_ex(unsigned char *output, unsigned flags, unsigned pack, const char *suffix,
                               unsigned length, unsigned *trace)
{
    struct Environment
    {
        unsigned char *out;
        unsigned flags, pack;
        const char *suffix;
        unsigned length, *trace, count = 0;
        unsigned char *At(unsigned p)
        {
            assert(p >= 0x104000 && p < 0x104000 + 160);
            return out + p - 0x104000;
        }
        unsigned Read(unsigned p)
        {
            unsigned n;
            memcpy(&n, At(p), 4);
            return n;
        }
        void Write(unsigned p, unsigned n) { memcpy(At(p), &n, 4); }
        void Word(unsigned p, unsigned n)
        {
            unsigned short v = n;
            memcpy(At(p), &v, 2);
        }
        void Byte(unsigned p, unsigned n) { *At(p) = n; }
        unsigned Open()
        {
            trace[++count] = 1;
            return flags & 1 ? 0 : 98;
        }
        bool ServicePack(unsigned, unsigned &value)
        {
            trace[++count] = 2;
            value = pack;
            return !(flags & 2);
        }
        bool Suffix(unsigned, unsigned p, unsigned n)
        {
            trace[++count] = 3;
            if (flags & 4 || length > n)
                return false;
            if (length)
                memcpy(At(p), suffix, length);
            return true;
        }
        void Close(unsigned) { trace[++count] = 4; }
        void Zero(unsigned p, unsigned n)
        {
            trace[++count] = 5;
            memset(At(p), 0, n);
        }
    } e{output, flags, pack, suffix, length, trace};
    const auto result = Win98::VersionEx(e, 0x104000);
    trace[0] = e.count;
    return result;
}
