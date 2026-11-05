// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_interlocked.h"
struct Provider
{
    unsigned value, count = 0, events[6]{};
    unsigned Read32(unsigned address)
    {
        events[count++] = 1;
        events[count++] = address;
        events[count++] = value;
        return value;
    }
    void Write32(unsigned address, unsigned updated)
    {
        events[count++] = 2;
        events[count++] = address;
        events[count++] = updated;
        value = updated;
    }
};
extern "C" unsigned interlocked(unsigned operation, unsigned address, unsigned initial, unsigned exchange,
                                unsigned compare, unsigned *out)
{
    Provider e{initial};
    unsigned result = 0;
    switch (operation)
    {
    case 0: result = Win98Interlocked::Exchange(e, address, exchange); break;
    case 1: result = Win98Interlocked::CompareExchange(e, address, exchange, compare); break;
    case 2: result = Win98Interlocked::Increment(e, address); break;
    case 3: result = Win98Interlocked::Decrement(e, address); break;
    case 4: result = Win98Interlocked::ExchangeAdd(e, address, exchange); break;
    }
    out[0] = e.value;
    out[1] = e.count;
    for (unsigned i = 0; i < e.count; ++i)
        out[2 + i] = e.events[i];
    return result;
}
