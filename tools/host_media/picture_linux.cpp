// SPDX-License-Identifier: GPL-2.0-or-later
#include "picture.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
namespace HostMediaPicture
{
bool Decode(const unsigned char *data, size_t size, std::vector<unsigned> &pixels, unsigned &width, unsigned &height)
{
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    const bool written = gdk_pixbuf_loader_write(loader, data, size, nullptr);
    const bool closed = gdk_pixbuf_loader_close(loader, nullptr);
    GdkPixbuf *image = written && closed ? gdk_pixbuf_loader_get_pixbuf(loader) : nullptr;
    bool okay = false;
    if (image && gdk_pixbuf_get_bits_per_sample(image) == 8 && gdk_pixbuf_get_colorspace(image) == GDK_COLORSPACE_RGB)
    {
        width = unsigned(gdk_pixbuf_get_width(image));
        height = unsigned(gdk_pixbuf_get_height(image));
        const unsigned channels = unsigned(gdk_pixbuf_get_n_channels(image)), stride = unsigned(gdk_pixbuf_get_rowstride(image));
        const unsigned char *bytes = gdk_pixbuf_read_pixels(image);
        if (width && height && width <= 16384 && height <= 16384 && channels >= 3)
        {
            pixels.resize(size_t(width) * height);
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < width; ++x)
                {
                    const unsigned char *p = bytes + size_t(y) * stride + size_t(x) * channels;
                    // Premultiplied, as ImageIO gives it on the Mac.
                    const unsigned a = channels == 4 ? p[3] : 255;
                    pixels[size_t(y) * width + x] = a << 24 | (p[0] * a / 255) << 16 | (p[1] * a / 255) << 8 | p[2] * a / 255;
                }
            okay = true;
        }
    }
    g_object_unref(loader);
    return okay;
}
}
