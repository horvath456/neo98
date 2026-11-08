// SPDX-License-Identifier: GPL-2.0-or-later
#include "picture.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <climits>
#include <cstring>
#include <memory>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_MAX_DIMENSIONS 16384
#include "../../third_party/stb/stb_image.h"
namespace HostMediaPicture
{
static bool Copy(const unsigned char *bytes, unsigned width, unsigned height, unsigned channels,
                 unsigned stride, std::vector<unsigned> &pixels)
{
    if (!width || !height || width > 16384 || height > 16384 || channels < 3) return false;
    pixels.resize(size_t(width) * height);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
        {
            const unsigned char *p = bytes + size_t(y) * stride + size_t(x) * channels;
            // Premultiplied, as ImageIO gives it on the Mac.
            const unsigned a = channels == 4 ? p[3] : 255;
            pixels[size_t(y) * width + x] = a << 24 | (p[0] * a / 255) << 16 | (p[1] * a / 255) << 8 | p[2] * a / 255;
        }
    return true;
}
bool Decode(const unsigned char *data, size_t size, std::vector<unsigned> &pixels, unsigned &width, unsigned &height)
{
    // GdkPixbuf can delegate even PNG/JPEG to an external Glycin helper.
    // Decode catalogue posters in-process so a stuck helper cannot hold the
    // picture worker, and consequently the host's shutdown, indefinitely.
    const bool png = size >= 8 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8);
    const bool jpeg = size >= 2 && data[0] == 0xff && data[1] == 0xd8;
    if (png || jpeg)
    {
        if (size > INT_MAX) return false;
        int w = 0, h = 0, channels = 0;
        std::unique_ptr<stbi_uc, void (*)(void *)> image(
            stbi_load_from_memory(data, int(size), &w, &h, &channels, 4), stbi_image_free);
        if (!image) return false;
        width = unsigned(w); height = unsigned(h);
        return Copy(image.get(), width, height, 4, width * 4, pixels);
    }
    // Retain the platform's additional formats, including WebP.
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    const bool written = gdk_pixbuf_loader_write(loader, data, size, nullptr);
    const bool closed = gdk_pixbuf_loader_close(loader, nullptr);
    GdkPixbuf *image = written && closed ? gdk_pixbuf_loader_get_pixbuf(loader) : nullptr;
    bool okay = false;
    if (image && gdk_pixbuf_get_bits_per_sample(image) == 8 && gdk_pixbuf_get_colorspace(image) == GDK_COLORSPACE_RGB)
    {
        width = unsigned(gdk_pixbuf_get_width(image));
        height = unsigned(gdk_pixbuf_get_height(image));
        okay = Copy(gdk_pixbuf_read_pixels(image), width, height, unsigned(gdk_pixbuf_get_n_channels(image)),
                    unsigned(gdk_pixbuf_get_rowstride(image)), pixels);
    }
    g_object_unref(loader);
    return okay;
}
}
