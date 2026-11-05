// SPDX-License-Identifier: GPL-2.0-or-later
// CPU readback of Metal BGRA pixels into the guest's unchanged pixel layout.
#pragma once
#include "../src/gpu_backend.h"
#include <cstddef>
#include <cstring>
namespace HostSurfacePixels
{
inline void PackChannel(unsigned mask, unsigned (&out)[256])
{
    unsigned shift = 0, bits = 0;
    if (mask) {
        while (!(mask & 1)) { mask >>= 1; ++shift; }
        while (mask & 1) { mask >>= 1; ++bits; }
    }
    for (unsigned v8 = 0; v8 < 256; ++v8) {
        const unsigned value = !bits ? 0 : bits >= 8 ? v8 << (bits - 8) :
                               (v8 * ((1u << bits) - 1) + 127) / 255;
        out[v8] = value << shift;
    }
}
inline void FromBgra(const Gpu::Surface &s, const unsigned *in)
{
    const unsigned bytes = s.bit_count / 8;
    if (bytes == 4 && s.r_mask == 0xff0000 && s.g_mask == 0xff00 && s.b_mask == 0xff) {
        for (int y = 0; y < s.height; ++y)
            std::memcpy(s.bits + size_t(y) * size_t(s.pitch), in + size_t(y) * size_t(s.width), size_t(s.width) * 4);
        return;
    }
    // Masks and quantization stay constant across the picture. Move their
    // shifts and rounding out of the per-pixel loop without dropping bits.
    unsigned red[256], green[256], blue[256], alpha[256];
    PackChannel(s.r_mask, red); PackChannel(s.g_mask, green);
    PackChannel(s.b_mask, blue); PackChannel(s.a_mask, alpha);
    for (int y = 0; y < s.height; ++y) {
        unsigned char *row = s.bits + size_t(y) * size_t(s.pitch);
        const unsigned *src = in + size_t(y) * size_t(s.width);
        for (int x = 0; x < s.width; ++x) {
            const unsigned p = src[x];
            const unsigned pixel = blue[p & 255] | green[(p >> 8) & 255] | red[(p >> 16) & 255] | alpha[p >> 24];
            switch (bytes) {
            case 1: row[x] = (unsigned char)pixel; break;
            case 2: row[x * 2] = pixel & 255; row[x * 2 + 1] = (pixel >> 8) & 255; break;
            case 3: row[x * 3] = pixel & 255; row[x * 3 + 1] = (pixel >> 8) & 255; row[x * 3 + 2] = (pixel >> 16) & 255; break;
            default: row[x * 4] = pixel & 255; row[x * 4 + 1] = (pixel >> 8) & 255; row[x * 4 + 2] = (pixel >> 16) & 255; row[x * 4 + 3] = pixel >> 24; break;
            }
        }
    }
}
} // namespace HostSurfacePixels
