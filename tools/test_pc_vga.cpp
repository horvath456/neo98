// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/pc_vga.h"
#include <assert.h>
#include <stdio.h>

int main()
{
    static PcVga::Device v;
    v.Mode(0x13);
    assert(v.Width() == 320 && v.Height() == 200);
    assert(v.Pixel(0, 0) == 0);
    v.Write(0xa0000, 15); assert(v.Pixel(0, 0) == 0xffffff);
    v.Mode(0x93); assert(v.Read(0xa0000) == 15);
    v.Mode(0x13); assert(v.Read(0xa0000) == 0);
    // Chained CPU addresses select successive planes.
    for (unsigned i = 0; i < 8; ++i) v.Write(0xa0000 + i, i + 1);
    for (unsigned i = 0; i < 8; ++i) assert(v.Read(0xa0000 + i) == i + 1);
    v.Output(0x3c4, 4); v.Output(0x3c5, 6);
    v.Output(0x3c4, 2); v.Output(0x3c5, 5);
    v.Write(0xa0100, 0xa5);
    for (unsigned p = 0; p < 4; ++p)
    {
        v.Output(0x3ce, 4); v.Output(0x3cf, p);
        assert(v.Read(0xa0100) == ((p == 0 || p == 2) ? 0xa5 : 0));
    }
    // Read latches drive write mode 1, independent of the CPU's byte.
    v.Output(0x3c4, 2); v.Output(0x3c5, 15);
    v.Output(0x3ce, 5); v.Output(0x3cf, 0x41);
    v.Write(0xa0101, 0xff);
    for (unsigned p = 0; p < 4; ++p)
    {
        v.Output(0x3ce, 4); v.Output(0x3cf, p);
        assert(v.Read(0xa0101) == ((p == 0 || p == 2) ? 0xa5 : 0));
    }
    // Mode 2 expands four CPU bits and preserves the unmasked latch bits.
    v.Output(0x3ce, 5); v.Output(0x3cf, 0x42);
    v.Output(0x3ce, 8); v.Output(0x3cf, 0x0f);
    v.Write(0xa0102, 0x0a);
    for (unsigned p = 0; p < 4; ++p)
    {
        v.Output(0x3ce, 4); v.Output(0x3cf, p);
        assert(v.Read(0xa0102) == ((p == 0 || p == 2) ? 0xa0 : 0x0f));
    }
    v.Output(0x3c8, 255);
    v.Output(0x3c9, 63); v.Output(0x3c9, 2); v.Output(0x3c9, 3);
    v.Output(0x3c9, 4);
    v.Output(0x3c7, 255);
    assert(v.Input(0x3c7, 0) == 3);
    assert(v.Input(0x3c9, 0) == 63 && v.Input(0x3c9, 0) == 2);
    assert(v.Input(0x3c9, 0) == 3 && v.Input(0x3c9, 0) == 4);
    // Status read resets the attribute address/data flip-flop.
    v.Output(0x3c0, 0x32); v.Input(0x3da, 8);
    v.Output(0x3c0, 0x33); v.Output(0x3c0, 7);
    assert(v.attr[0x13] == 7 && v.attr[0x12] == 15);
    // DISPDIB's 320x400 changes max scan line, leaving the vertical end.
    v.Mode(0x13); v.Output(0x3d4, 9); v.Output(0x3d5, 0x40);
    assert(v.Width() == 320 && v.Height() == 400);
    v.Output(0x3d4, 0x11); v.Output(0x3d5, 0x80);
    v.Output(0x3d4, 1); v.Output(0x3d5, 3);
    assert(v.Width() == 320);
    puts("PASS VGA chain-4, plane mask, latches, write modes, DAC wrap, attribute flip-flop, CRTC protection and dimensions");
}
