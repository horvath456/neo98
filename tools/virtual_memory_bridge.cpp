// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_virtual.h"
#include <initializer_list>
extern "C" unsigned original_virtual(unsigned operation, unsigned *args, const unsigned *config,
                                     unsigned *out)
{
    struct Environment
    {
        const unsigned *config;
        unsigned *out, count = 0;
        void Event(unsigned kind, unsigned a = 0, unsigned b = 0, unsigned c = 0, unsigned d = 0,
                   unsigned f = 0)
        {
            for (auto v : {kind, a, b, c, d, f})
                out[2 + count++] = v;
        }
        void Error(unsigned value)
        {
            out[0] = value;
            Event(0, value);
        }
        unsigned Reserve(unsigned page, unsigned pages, unsigned flags)
        {
            Event(1, page, pages, flags);
            return config[0];
        }
        unsigned Commit(unsigned page, unsigned pages, unsigned pager, unsigned handle, unsigned flags)
        {
            Event(2, page, pages, pager, handle, flags);
            return config[1];
        }
        unsigned Release(unsigned base, unsigned flags)
        {
            Event(3, base, flags);
            return config[2];
        }
        unsigned Decommit(unsigned page, unsigned pages, unsigned flags)
        {
            Event(4, page, pages, flags);
            return config[3];
        }
    } e{config, out};
    const auto value = operation == 0   ? Win98Virtual::Allocate(e, args[0], args[1], args[2], args[3])
                       : operation == 1 ? Win98Virtual::Free(e, args[0], args[1], args[2])
                                        : Win98Virtual::Protection(e, args[0]);
    out[1] = e.count;
    return value;
}
