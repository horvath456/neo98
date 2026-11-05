// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_szdd.h"
#include <cassert>
#include <cstring>
#include <cstdio>

int main()
{
    unsigned char packed[] = {'S','Z','D','D',0x88,0xf0,0x27,0x33,'A','t',
                              9,0,0,0,7,'A','B','C',0xf0,0xf3};
    unsigned char output[32] = {};
    unsigned produced = 99;
    assert(!Win98Szdd::DecodeAvailable(packed,sizeof packed,output,9,produced));
    assert(produced == 9 && !std::memcmp(output,"ABCABCABC",9));
    // A declared size beyond the compressed stream preserves its decoded prefix.
    packed[10] = 20;
    assert(Win98Szdd::DecodeAvailable(packed,sizeof packed,output,20,produced) == -3);
    assert(produced == 9 && !std::memcmp(output,"ABCABCABC",9));
    assert(Win98Szdd::Decode(packed,sizeof packed,output,20) == -3);
    // Every truncation is bounded, including the flag byte and half a match.
    for (unsigned n = 0; n < sizeof packed; ++n) {
        std::memset(output,0xa5,sizeof output);
        assert(Win98Szdd::DecodeAvailable(packed,n,output,20,produced) == -3);
        assert(produced <= 9);
        for (unsigned i = produced; i < sizeof output; ++i) assert(output[i] == 0xa5);
    }
    packed[8] = 'B';
    assert(Win98Szdd::DecodeAvailable(packed,sizeof packed,output,20,produced) == -8);
    assert(!produced);
    std::puts("PASS: SZDD prefix preservation, overlapping matches and every truncated boundary");
}
