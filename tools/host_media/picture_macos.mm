// SPDX-License-Identifier: GPL-2.0-or-later
#include "picture.h"
#import <CoreFoundation/CoreFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
namespace HostMediaPicture
{
bool Decode(const unsigned char *data, size_t size, std::vector<unsigned> &pixels, unsigned &width, unsigned &height)
{
    CFDataRef bytes = CFDataCreate(nullptr, data, CFIndex(size));
    if (!bytes) return false;
    CGImageSourceRef source = CGImageSourceCreateWithData(bytes, nullptr);
    CFRelease(bytes);
    if (!source) return false;
    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (!image) return false;
    width = unsigned(CGImageGetWidth(image));
    height = unsigned(CGImageGetHeight(image));
    bool okay = false;
    if (width && height && width <= 16384 && height <= 16384)
    {
        pixels.assign(size_t(width) * height, 0);
        // Little-endian 32-bit words with alpha first: 0xAARRGGBB.
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate(pixels.data(), width, height, 8, width * 4, space,
                                                     kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
        CGColorSpaceRelease(space);
        if (context)
        {
            CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
            CGContextRelease(context);
            okay = true;
        }
    }
    CGImageRelease(image);
    return okay;
}
}
