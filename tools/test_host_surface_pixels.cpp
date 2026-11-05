// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_surface_pixels.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>
static unsigned Pack(unsigned mask, unsigned value)
{
    if (!mask) return 0;
    unsigned shift = 0, bits = 0;
    while (!(mask & 1)) { ++shift; mask >>= 1; }
    while (mask & 1) { ++bits; mask >>= 1; }
    return (bits >= 8 ? value << (bits - 8) : (value * ((1u << bits) - 1) + 127) / 255) << shift;
}
static void Reference(const Gpu::Surface &s, const unsigned *source)
{
    const unsigned bytes = s.bit_count / 8;
    for (int y = 0; y < s.height; ++y)
        for (int x = 0; x < s.width; ++x) {
            unsigned p = source[y * s.width + x];
            if (!(bytes == 4 && s.r_mask == 0xff0000 && s.g_mask == 0xff00 && s.b_mask == 0xff))
                p = Pack(s.r_mask, (p >> 16) & 255) | Pack(s.g_mask, (p >> 8) & 255) |
                    Pack(s.b_mask, p & 255) | Pack(s.a_mask, p >> 24);
            for (unsigned b = 0; b < bytes; ++b) s.bits[y * s.pitch + x * bytes + b] = p >> (b * 8);
        }
}
int main()
{
    struct Format { unsigned bits, r, g, b, a; };
    const Format formats[] = {
        {8,0xe0,0x1c,3,0}, {16,0xf800,0x7e0,0x1f,0}, {16,0x7c00,0x3e0,0x1f,0x8000},
        {16,0xf00,0xf0,0xf,0xf000}, {24,0xff0000,0xff00,0xff,0}, {24,0xff,0xff00,0xff0000,0},
        {32,0xff0000,0xff00,0xff,0}, {32,0xff0000,0xff00,0xff,0xff000000},
        {32,0xff,0xff00,0xff0000,0xff000000}, {32,0x3ff00000,0xffc00,0x3ff,0xc0000000},
        {16,0,0,0,0}
    };
    // Every possible eight-bit value occurs in every channel; odd widths and
    // padded, unaligned rows catch overwrites outside the guest pixel bytes.
    const int width = 137, height = 9;
    std::vector<unsigned> input(width * height);
    for (unsigned n = 0; n < input.size(); ++n)
        input[n] = ((n & 255) << 24) | (((n * 73) & 255) << 16) | (((n * 59) & 255) << 8) | ((n * 13) & 255);
    for (const auto &f : formats) {
        Gpu::Surface s; s.width=width; s.height=height; s.pitch=width * (f.bits / 8) + 7;
        s.bit_count=f.bits; s.r_mask=f.r; s.g_mask=f.g; s.b_mask=f.b; s.a_mask=f.a;
        std::vector<unsigned char> got(s.pitch * height + 2, 0xa5), expected=got;
        s.bits=got.data()+1; HostSurfacePixels::FromBgra(s,input.data());
        s.bits=expected.data()+1; Reference(s,input.data()); assert(got==expected);
    }
    Gpu::Surface s; s.width=800; s.height=600; s.pitch=1600; s.bit_count=16;
    s.r_mask=0xf800; s.g_mask=0x7e0; s.b_mask=0x1f;
    input.resize(800*600,0x7f987654);
    std::vector<unsigned char> pixels(800*600*2); s.bits=pixels.data();
    const auto start=std::chrono::steady_clock::now();
    for(int n=0;n<30;++n) HostSurfacePixels::FromBgra(s,input.data());
    const double milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/30;
    printf("HOST SURFACE PIXELS PASS: 11 formats, all channel values, row padding; RGB565 800x600 %.3f ms\n",milliseconds);
}
