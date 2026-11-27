// SPDX-License-Identifier: GPL-2.0-or-later
// Host unit test for src/capture_crop.h: the part of a camera picture that
// fills a capture format of another shape. Plain integer math, checked with
// ASan/UBSan.
#include "../src/capture_crop.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

static bool Is(CaptureCrop::Area a, unsigned x, unsigned y, unsigned width, unsigned height)
{
    return a.x == x && a.y == y && a.width == width && a.height == height;
}

int main()
{
    // The Mac's camera: 1280x720, which CopyFrame (tools/host_devices/
    // platform.h) makes 640x360, into the 640x480 Camera asks for. The middle
    // 480x360 is 4:3; before, the whole 640x360 was stretched to 640x480.
    assert(Is(CaptureCrop::Centred(640, 360, 640, 480), 80, 0, 480, 360));
    // The same shape: the whole picture.
    assert(Is(CaptureCrop::Centred(640, 480, 640, 480), 0, 0, 640, 480));
    assert(Is(CaptureCrop::Centred(320, 240, 640, 480), 0, 0, 320, 240));
    assert(Is(CaptureCrop::Centred(320, 240, 160, 120), 0, 0, 320, 240));
    // A 4:3 picture into a wide format: the top and the bottom go.
    assert(Is(CaptureCrop::Centred(640, 480, 320, 180), 0, 60, 640, 360));
    // A portrait picture (a phone as the camera) into 4:3.
    assert(Is(CaptureCrop::Centred(270, 480, 640, 480), 0, 139, 270, 202));
    // 1760x1328, scaled to 636x480, into 640x480: four columns of 640 do
    // not fit, and the height stays whole.
    {
        const auto a = CaptureCrop::Centred(636, 480, 640, 480);
        assert(a.x == 0 && a.width == 636 && a.height == 477 && a.y == 1);
    }
    // The smallest format of the driver, 16x12, and a picture of one line.
    assert(Is(CaptureCrop::Centred(640, 360, 16, 12), 80, 0, 480, 360));
    {
        const auto a = CaptureCrop::Centred(640, 1, 640, 480);
        assert(a.width == 1 && a.height == 1 && a.x + a.width <= 640);
    }
    // Every result lies inside the picture and has the shape of the format
    // to within a pixel.
    for (unsigned sw = 1; sw <= 640; sw += 37)
        for (unsigned sh = 1; sh <= 480; sh += 29)
            for (unsigned tw : {16u, 160u, 320u, 352u, 640u})
                for (unsigned th : {12u, 120u, 240u, 288u, 480u})
                {
                    const auto a = CaptureCrop::Centred(sw, sh, tw, th);
                    assert(a.width >= 1 && a.height >= 1);
                    assert(a.x + a.width <= sw && a.y + a.height <= sh);
                    assert(a.width == sw || a.height == sh);
                    const long long cross = (long long)a.width * th - (long long)a.height * tw;
                    // A width cut to the format loses less than a column of
                    // it, a height less than a row. A picture too thin to
                    // have the shape at all keeps one pixel.
                    if (sh * tw >= th && sw * th >= tw)
                        assert(cross > -(long long)th && cross < (long long)tw);
                }
    // Nothing to fit: the picture as it is.
    assert(Is(CaptureCrop::Centred(640, 360, 0, 480), 0, 0, 640, 360));
    puts("CAPTURE CROP PASS");
    return 0;
}
