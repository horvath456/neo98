// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../src/gpu_backend.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
namespace HostVulkan::Texture
{
inline unsigned Channel(unsigned v, unsigned mask, unsigned fallback = 0)
{
    if (!mask)
        return fallback;
    unsigned shift = __builtin_ctz(mask), max = mask >> shift;
    return unsigned((uint64_t((v & mask) >> shift) * 255 + max / 2) / max);
}
inline unsigned Signed(unsigned v, unsigned mask)
{
    if (!mask)
        return 128;
    unsigned shift = __builtin_ctz(mask), m = mask >> shift, bits = 32 - __builtin_clz(m);
    int64_t n = (v & mask) >> shift;
    if (n & (1ull << (bits - 1)))
        n -= int64_t(1ull << bits);
    const int64_t maximum = std::max<int64_t>(1, (1ull << (bits - 1)) - 1);
    return unsigned(std::clamp<int64_t>(128 + n * 127 / maximum, 0, 255));
}
inline void Rgb565(unsigned v, unsigned char *c)
{
    c[0] = static_cast<unsigned char>(Channel(v, 0xf800));
    c[1] = static_cast<unsigned char>(Channel(v, 0x7e0));
    c[2] = static_cast<unsigned char>(Channel(v, 0x1f));
    c[3] = 255;
}
// Fixed DirectDraw formats keep the divisors constant, so conversion does
// not need integer division for each channel of each pixel.
template <unsigned GreenBits>
inline void Decode16(const Gpu::Surface &s, unsigned char *out)
{
    constexpr unsigned green_mask = (1u << GreenBits) - 1;
    for (int y = 0; y < s.height; ++y)
        for (int x = 0; x < s.width; ++x)
        {
            unsigned short v;
            memcpy(&v, s.bits + y * s.pitch + x * 2, 2);
            auto *q = out + (size_t(y) * s.width + x) * 4;
            q[0] = (((v >> (5 + GreenBits)) & 31) * 255u + 15) / 31;
            q[1] = (((v >> 5) & green_mask) * 255u + green_mask / 2) / green_mask;
            q[2] = ((v & 31) * 255u + 15) / 31;
            q[3] = 255;
        }
}
template <unsigned GreenBits>
inline void Encode16(const unsigned char *input, const Gpu::Surface &s)
{
    constexpr unsigned green_mask = (1u << GreenBits) - 1;
    for (int y = 0; y < s.height; ++y)
        for (int x = 0; x < s.width; ++x)
        {
            const auto *p = input + (size_t(y) * s.width + x) * 4;
            const unsigned short v = (((unsigned(p[0]) * 31 + 127) / 255) << (5 + GreenBits)) |
                                     (((unsigned(p[1]) * green_mask + 127) / 255) << 5) |
                                     ((unsigned(p[2]) * 31 + 127) / 255);
            memcpy(s.bits + y * s.pitch + x * 2, &v, 2);
        }
}
inline bool Decode(const Gpu::Surface &s, std::vector<unsigned char> &out)
{
    if (!s.bits || s.width <= 0 || s.height <= 0 || s.width > 4096 || s.height > 4096 || s.pitch <= 0)
        return false;
    out.resize(size_t(s.width) * s.height * 4);
    if (s.fourcc)
    {
        const unsigned version = (s.fourcc >> 24) & 255;
        if ((s.fourcc & 0xffffff) != 0x545844 || version < '1' || version > '5')
            return false;
        const unsigned bytes = version == '1' ? 8 : 16;
        if (unsigned(s.pitch) < unsigned((s.width + 3) / 4) * bytes)
            return false;
        for (int by = 0; by < s.height; by += 4)
            for (int bx = 0; bx < s.width; bx += 4)
            {
                const unsigned char *block = s.bits + (by / 4) * s.pitch + (bx / 4) * bytes,
                                    *rgb = block + (bytes == 16 ? 8 : 0);
                unsigned short c0, c1;
                memcpy(&c0, rgb, 2);
                memcpy(&c1, rgb + 2, 2);
                unsigned colors = 0;
                memcpy(&colors, rgb + 4, 4);
                unsigned char palette[4][4];
                Rgb565(c0, palette[0]);
                Rgb565(c1, palette[1]);
                for (unsigned c = 0; c < 3; ++c)
                {
                    palette[2][c] = static_cast<unsigned char>(
                        (unsigned(palette[0][c]) * (c0 > c1 || bytes == 16 ? 2 : 1) + palette[1][c]) /
                        (c0 > c1 || bytes == 16 ? 3 : 2));
                    palette[3][c] = c0 > c1 || bytes == 16
                                        ? static_cast<unsigned char>((palette[0][c] + 2u * palette[1][c]) / 3)
                                        : 0;
                }
                palette[2][3] = 255;
                palette[3][3] = c0 > c1 || bytes == 16 ? 255 : 0;
                unsigned char alphas[8] = {};
                uint64_t alpha_indices = 0;
                if (version == '4' || version == '5')
                {
                    alphas[0] = block[0];
                    alphas[1] = block[1];
                    if (alphas[0] > alphas[1])
                        for (unsigned i = 2; i < 8; ++i)
                            alphas[i] =
                                static_cast<unsigned char>(((8 - i) * alphas[0] + (i - 1) * alphas[1]) / 7);
                    else
                    {
                        for (unsigned i = 2; i < 6; ++i)
                            alphas[i] =
                                static_cast<unsigned char>(((6 - i) * alphas[0] + (i - 1) * alphas[1]) / 5);
                        alphas[6] = 0;
                        alphas[7] = 255;
                    }
                    for (unsigned i = 0; i < 6; ++i)
                        alpha_indices |= uint64_t(block[i + 2]) << (8 * i);
                }
                for (unsigned y = 0; y < 4; ++y)
                    for (unsigned x = 0; x < 4; ++x)
                    {
                        unsigned n = y * 4 + x;
                        if (bx + int(x) >= s.width || by + int(y) >= s.height)
                            continue;
                        auto *p = out.data() + (size_t(by + y) * s.width + bx + x) * 4;
                        memcpy(p, palette[(colors >> (2 * n)) & 3], 4);
                        if (version == '2' || version == '3')
                            p[3] = static_cast<unsigned char>(((block[n / 2] >> ((n & 1) * 4)) & 15) * 17);
                        else if (version == '4' || version == '5')
                            p[3] = alphas[(alpha_indices >> (n * 3)) & 7];
                    }
            }
        return true;
    }
    unsigned stride = (s.bit_count + 7) / 8;
    if (stride < 1 || stride > 4 || s.pitch < s.width * int(stride) || (s.indexed && !s.palette))
        return false;
    if (!s.indexed && !s.bump && !s.color_key && stride == 2 && s.b_mask == 0x1f && !s.a_mask)
    {
        if (s.r_mask == 0xf800 && s.g_mask == 0x7e0)
        {
            Decode16<6>(s, out.data());
            return true;
        }
        if (s.r_mask == 0x7c00 && s.g_mask == 0x3e0)
        {
            Decode16<5>(s, out.data());
            return true;
        }
    }
    if (!s.indexed && !s.bump && !s.color_key && (stride == 3 || stride == 4) && s.r_mask == 0xff0000 &&
        s.g_mask == 0xff00 && s.b_mask == 0xff && (!s.a_mask || s.a_mask == 0xff000000))
    {
        for (int y = 0; y < s.height; ++y)
            for (int x = 0; x < s.width; ++x)
            {
                const auto *p = s.bits + y * s.pitch + x * stride;
                auto *q = out.data() + (size_t(y) * s.width + x) * 4;
                q[0] = p[2];
                q[1] = p[1];
                q[2] = p[0];
                q[3] = stride == 4 && s.a_mask ? p[3] : 255;
            }
        return true;
    }
    for (int y = 0; y < s.height; ++y)
        for (int x = 0; x < s.width; ++x)
        {
            unsigned value = 0;
            memcpy(&value, s.bits + y * s.pitch + x * stride, stride);
            auto *p = out.data() + (size_t(y) * s.width + x) * 4;
            if (s.indexed)
            {
                unsigned c = s.palette[value & 255];
                p[0] = (c >> 16) & 255;
                p[1] = (c >> 8) & 255;
                p[2] = c & 255;
                p[3] = s.palette_alpha ? c >> 24 : 255;
            }
            else if (s.bump)
            {
                p[0] = static_cast<unsigned char>(Signed(value, s.r_mask));
                p[1] = static_cast<unsigned char>(Signed(value, s.g_mask));
                p[2] = static_cast<unsigned char>(Channel(value, s.b_mask, 255));
                p[3] = 255;
            }
            else
            {
                p[0] = static_cast<unsigned char>(Channel(value, s.r_mask));
                p[1] = static_cast<unsigned char>(Channel(value, s.g_mask));
                p[2] = static_cast<unsigned char>(Channel(value, s.b_mask));
                p[3] = static_cast<unsigned char>(Channel(value, s.a_mask, 255));
            }
            if (s.color_key && value >= s.key_low && value <= s.key_high)
                p[3] = 0;
        }
    return true;
}
inline bool DecodeVolume(const Gpu::Surface &s, std::vector<unsigned char> &out)
{
    if (s.slices <= 0)
        return Decode(s, out);
    if (!s.bits || s.width <= 0 || s.height <= 0 || s.pitch <= 0 ||
        uint64_t(s.width) * s.height * s.slices > 64 * 1024 * 1024)
        return false;
    const size_t slice_bytes = size_t(s.pitch) * (s.fourcc ? (s.height + 3) / 4 : s.height);
    const size_t converted_bytes = size_t(s.width) * s.height * 4;
    out.resize(converted_bytes * s.slices);
    Gpu::Surface slice = s;
    slice.slices = 0;
    std::vector<unsigned char> decoded;
    for (int z = 0; z < s.slices; ++z)
    {
        slice.bits = s.bits + z * slice_bytes;
        if (!Decode(slice, decoded))
            return false;
        memcpy(out.data() + z * converted_bytes, decoded.data(), converted_bytes);
    }
    return true;
}
} // namespace HostVulkan::Texture
