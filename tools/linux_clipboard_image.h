// SPDX-License-Identifier: GPL-2.0-or-later
// Packed Win98 DIBs and the RGB images used by the Linux clipboard.
#pragma once
#include <cstdint>
#include <cstring>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <vector>
namespace LinuxHost
{
inline unsigned ClipboardLittle(const unsigned char *bytes, unsigned count)
{
    unsigned value = 0;
    for (unsigned i = 0; i < count; ++i)
        value |= unsigned(bytes[i]) << (i * 8);
    return value;
}
inline GdkPixbuf *ClipboardPixbuf(const unsigned char *dib, size_t bytes)
{
    if (!dib || bytes < 40)
        return nullptr;
    const unsigned header = ClipboardLittle(dib, 4), bits = ClipboardLittle(dib + 14, 2),
                   compression = ClipboardLittle(dib + 16, 4);
    const int width = int(ClipboardLittle(dib + 4, 4)), signed_height = int(ClipboardLittle(dib + 8, 4));
    if (signed_height == INT32_MIN)
        return nullptr;
    const int height = signed_height < 0 ? -signed_height : signed_height;
    if (header < 40 || header > bytes || width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        uint64_t(width) * height > (64u << 20))
        return nullptr;
    if (compression != 0 && !(compression == 3 && (bits == 16 || bits == 32)))
        return nullptr;
    if (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32)
        return nullptr;
    unsigned colours = bits <= 8 ? ClipboardLittle(dib + 32, 4) : 0;
    if (bits <= 8 && (!colours || colours > (1u << bits)))
        colours = 1u << bits;
    unsigned masks[3] = {0x7c00, 0x3e0, 0x1f};
    if (bits == 32)
    {
        masks[0] = 0xff0000;
        masks[1] = 0xff00;
        masks[2] = 0xff;
    }
    size_t at = header;
    if (compression == 3)
    {
        if (bytes < 52)
            return nullptr;
        for (unsigned i = 0; i < 3; ++i)
            masks[i] = ClipboardLittle(dib + 40 + i * 4, 4);
        if (header == 40)
            at += 12;
    }
    const size_t palette = at;
    at += size_t(colours) * 4;
    const size_t stride = (size_t(width) * bits + 31) / 32 * 4;
    if (at > bytes || stride * height > bytes - at)
        return nullptr;
    GdkPixbuf *image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, width, height);
    if (!image)
        return nullptr;
    auto channel = [](unsigned pixel, unsigned mask) -> unsigned char {
        if (!mask)
            return 0;
        const unsigned shift = unsigned(__builtin_ctz(mask)), maximum = mask >> shift;
        return static_cast<unsigned char>(uint64_t((pixel & mask) >> shift) * 255 / maximum);
    };
    for (int y = 0; y < height; ++y)
    {
        const unsigned char *row = dib + at + stride * (signed_height < 0 ? y : height - 1 - y);
        unsigned char *out = gdk_pixbuf_get_pixels(image) + size_t(y) * gdk_pixbuf_get_rowstride(image);
        for (int x = 0; x < width; ++x, out += 3)
        {
            if (bits <= 8)
            {
                unsigned index = bits == 8   ? row[x]
                                 : bits == 4 ? (row[x / 2] >> (x & 1 ? 0 : 4)) & 15
                                             : (row[x / 8] >> (7 - (x & 7))) & 1;
                if (index >= colours)
                    index = 0;
                const unsigned char *entry = dib + palette + index * 4;
                out[0] = entry[2];
                out[1] = entry[1];
                out[2] = entry[0];
            }
            else if (bits == 24)
            {
                out[0] = row[x * 3 + 2];
                out[1] = row[x * 3 + 1];
                out[2] = row[x * 3];
            }
            else
            {
                unsigned pixel = ClipboardLittle(row + x * (bits / 8), bits / 8);
                for (unsigned i = 0; i < 3; ++i)
                    out[i] = channel(pixel, masks[i]);
            }
        }
    }
    return image;
}
inline bool ClipboardDib(GdkPixbuf *image, std::vector<unsigned char> &dib)
{
    if (!image)
        return false;
    const int width = gdk_pixbuf_get_width(image), height = gdk_pixbuf_get_height(image);
    const int channels = gdk_pixbuf_get_n_channels(image), row_bytes = gdk_pixbuf_get_rowstride(image);
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        uint64_t(width) * height > (64u << 20) || channels < 3)
        return false;
    const size_t stride = (size_t(width) * 3 + 3) & ~size_t(3);
    dib.assign(40 + stride * height, 0);
    auto put = [&](unsigned offset, unsigned value, unsigned count) {
        for (unsigned i = 0; i < count; ++i)
            dib[offset + i] = static_cast<unsigned char>(value >> (i * 8));
    };
    put(0, 40, 4);
    put(4, width, 4);
    put(8, height, 4);
    put(12, 1, 2);
    put(14, 24, 2);
    put(20, stride * height, 4);
    const unsigned char *source = gdk_pixbuf_read_pixels(image);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const unsigned char *pixel = source + size_t(y) * row_bytes + x * channels;
            unsigned char *out = dib.data() + 40 + stride * (height - 1 - y) + size_t(x) * 3;
            const unsigned alpha = channels == 4 ? pixel[3] : 255;
            for (unsigned i = 0; i < 3; ++i)
                out[i] = static_cast<unsigned char>(
                    (unsigned(pixel[2 - i]) * alpha + 255 * (255 - alpha) + 127) / 255);
        }
    return true;
}
} // namespace LinuxHost
