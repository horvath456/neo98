// SPDX-License-Identifier: GPL-2.0-or-later
// The part of a camera picture that fills a capture format of another shape.
//
// A program of Windows 98 asks NEO98CAP.DRV for a size, 640x480 at most and
// mostly 4:3, and the camera of the host sends what it has, on a Mac mostly
// 16:9. Scaled to the size as it is, the picture was squeezed: faces long and
// narrow. The driver takes the largest part of the picture with the shape of
// the format, from its middle, as a camera of 4:3 sees the middle of a wide
// scene, and scales that (src/win16_capture.cpp). Pixels are square on both
// sides.
#pragma once

namespace CaptureCrop
{
struct Area
{
    unsigned x, y, width, height;
};

// The centred part of a source_width x source_height picture with the shape
// of target_width x target_height. Both are at most a few thousand pixels, so
// the products stay within 32 bits.
inline Area Centred(unsigned source_width, unsigned source_height, unsigned target_width, unsigned target_height)
{
    if (!source_width || !source_height || !target_width || !target_height)
        return {0, 0, source_width, source_height};
    // Wider than the format: the sides go.
    if (source_width * target_height > source_height * target_width)
    {
        unsigned width = source_height * target_width / target_height;
        if (!width) width = 1;
        return {(source_width - width) / 2, 0, width, source_height};
    }
    // Taller than the format: the top and the bottom go.
    unsigned height = source_width * target_height / target_width;
    if (!height) height = 1;
    return {0, (source_height - height) / 2, source_width, height};
}
} // namespace CaptureCrop
