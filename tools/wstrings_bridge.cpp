// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_wstrings.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using U = unsigned;
constexpr U Bytes = 8192, Base = 0x04000000;
struct Environment
{
    unsigned char *data;
    U *out, readable, writable;
    bool Access(U address, U mask)
    {
        return address >= Base && address - Base < Bytes - 1 &&
               (mask & (1u << ((address-Base) >> 12))) &&
               (mask & (1u << ((address-Base+1) >> 12)));
    }
    bool Read16(U a, U &v)
    {
        if (!Access(a, readable)) return false;
        v = data[a-Base] | U(data[a-Base+1]) << 8;
        return true;
    }
    bool Write16(U a, U v)
    {
        if (!Access(a, writable)) return false;
        data[a-Base] = v;
        data[a-Base+1] = v >> 8;
        return true;
    }
    void Error(U n) { out[1] = n; ++out[2]; }
};
extern "C" void wstrings(const U *c, unsigned char *bytes, U *out)
{
    out[0] = out[2] = 0; out[1] = c[6];
    Environment e{bytes, out, c[4], c[5]};
    switch (c[0])
    {
    case 0: out[0] = Win98WStrings::Length(e,c[2]); break;
    case 1: out[0] = Win98WStrings::Copy(e,c[1],c[2]); break;
    case 2: out[0] = Win98WStrings::Cat(e,c[1],c[2]); break;
    case 3: out[0] = Win98WStrings::UnsupportedCopyN(e,c[1],c[2],c[3]); break;
    default: assert(false);
    }
}
#ifdef WSTRINGS_MAIN
int main(int argc, char **argv)
{
    assert(argc == 2); FILE *f = std::fopen(argv[1],"rb"); assert(f);
    U c[7], expected[3], out[3], count = 0;
    unsigned char before[Bytes], after[Bytes];
    while (std::fread(c,sizeof c,1,f))
    {
        assert(std::fread(before,sizeof before,1,f)==1);
        assert(std::fread(expected,sizeof expected,1,f)==1);
        assert(std::fread(after,sizeof after,1,f)==1);
        wstrings(c,before,out);
        assert(!std::memcmp(out,expected,sizeof out));
        assert(!std::memcmp(before,after,sizeof before)); ++count;
    }
    assert(std::feof(f)); std::fclose(f);
    std::printf("WSTRINGS NATIVE PASS %u original cases\n",count);
}
#endif
