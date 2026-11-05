// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_resource_write.h"
#include "../src/win98_exceptions.h"
struct Environment
{
    const unsigned *row;
    unsigned *out;
    bool Query(unsigned address, unsigned &allocation, unsigned &protection)
    {
        out[1] = address; allocation = row[2]; protection = row[3];
        return row[1] == 28;
    }
    unsigned ResourceDirectory(unsigned allocation, unsigned &size)
    {
        out[2] = allocation; size = row[5]; return row[4];
    }
    bool MakeWritable(unsigned address) { out[3] = address; return row[6] != ~0u; }
};
extern "C" void resource_write(const unsigned *row, unsigned *out)
{
    for (unsigned i = 0; i < 4; ++i) out[i] = 0;
    Environment e{row, out}; out[0] = Win98ResourceWrite::Repair(e, row[0]) ? ~0u : 0;
}
extern "C" void thread_start(unsigned flags, unsigned filter, unsigned *out)
{
    struct StartEnvironment {
        unsigned flags, filter, *out;
        unsigned Read(unsigned) { return flags; }
        unsigned Unhandled(unsigned, unsigned) { ++out[1]; return filter; }
        void ThreadTerminate(unsigned, unsigned, unsigned) { out[2] = 1; }
    } e{flags, filter, out};
    out[0] = out[1] = out[2] = 0;
    out[0] = Win98Exceptions::ThreadStart(e, 0, 0, 0);
}
