// SPDX-License-Identifier: GPL-2.0-or-later
// Win98DisplayModes::Publish against the plain rule it implements: every
// picture element of the host image is the one of the nearest source column
// and row, converted by Pixel. tools/test_display_publish_host.py.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../src/win98_display_modes.h"

using namespace Win98DisplayModes;

static void Reference(const Surface &s, unsigned char *out, int width, int height, int stride)
{
    for (int y = 0; y < height; ++y)
    {
        const int sy = int((long long)y * s.height / height);
        // A surface of 32 bits and the width of the image is copied as it
        // is, with its fourth byte.
        if (s.depth == 32 && s.width == width)
        {
            for (int i = 0; i < width * 4; ++i) out[(long)y * stride + i] = s.bits[(long)sy * s.stride + i];
            continue;
        }
        for (int x = 0; x < width; ++x)
        {
            const int sx = int((long long)x * s.width / width);
            const U pixel = Pixel(s, sx, sy);
            unsigned char *to = out + (long)y * stride + x * 4;
            to[0] = pixel & 255; to[1] = (pixel >> 8) & 255; to[2] = (pixel >> 16) & 255; to[3] = 0;
        }
    }
}

int main()
{
    static const int sizes[][2] = {{640, 480}, {800, 600}, {1024, 768}, {320, 200}, {320, 240}, {640, 400}, {1, 1}, {1470, 919}, {801, 599}, {3, 7}};
    static const int depths[] = {8, 16, 24, 32};
    unsigned seed = 12345;
    auto next = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    U colors[256];
    for (U &c : colors) c = next();
    int checks = 0, failures = 0;
    for (auto &from : sizes)
        for (auto &to : sizes)
            for (int depth : depths)
            {
                const int stride = (from[0] * depth / 8 + 3) & ~3;
                std::vector<unsigned char> bits(size_t(stride) * from[1]);
                for (auto &b : bits) b = (unsigned char)next();
                Surface s;
                s.bits = bits.data(); s.width = from[0]; s.height = from[1]; s.stride = stride; s.depth = depth;
                s.colors = (checks & 1) ? colors : nullptr;
                const int out_stride = to[0] * 4 + 8;
                std::vector<unsigned char> a(size_t(out_stride) * to[1], 0x5a), b(a);
                Publish(s, a.data(), to[0], to[1], out_stride);
                Reference(s, b.data(), to[0], to[1], out_stride);
                ++checks;
                if (a != b) { ++failures; std::printf("FAIL %dx%dx%d -> %dx%d\n", from[0], from[1], depth, to[0], to[1]); }
            }
    // What one picture costs: 640x480x16 onto the host image of 800x600.
    std::vector<unsigned char> screen(640 * 480 * 2), image(800 * 600 * 4);
    for (auto &b : screen) b = (unsigned char)next();
    Surface s;
    s.bits = screen.data(); s.width = 640; s.height = 480; s.stride = 1280; s.depth = 16;
    for (int reference = 0; reference < 2; ++reference)
    {
        const auto began = std::chrono::steady_clock::now();
        for (int i = 0; i < 200; ++i) (reference ? Reference : Publish)(s, image.data(), 800, 600, 3200);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count() / 200;
        std::printf("%s 640x480x16 -> 800x600: %.3f ms\n", reference ? "reference" : "publish", ms);
    }
    std::printf("PUBLISH checks=%d failures=%d\n", checks, failures);
    return failures != 0;
}
