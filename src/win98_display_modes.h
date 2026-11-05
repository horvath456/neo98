#include "win98_gdi_palette_tables.h"
// SPDX-License-Identifier: GPL-2.0-or-later
// Display-driver modes, row stride and host-image conversion. Windows 98
// builds EnumDisplaySettings from driver modes and registry settings. Neo98
// supports linear framebuffers within its screen capacity, including 640x480,
// 800x600 and 1024x768 at 8, 16, 24 and 32 bits.
// Framebuffer dimensions are independent of the host image; Publish scales
// larger or smaller modes to the host display. Omit indexed 320x200 and
// 320x240 VGA/ModeX modes because they require VGA register and DirectDraw
// HAL behavior. Linear 320x240x16 does not require those registers. Use
// nearest-neighbor scaling, matching a fixed-pixel display.
#pragma once
namespace Win98DisplayModes
{
using U = unsigned;

struct Mode
{
    unsigned short width, height;
    unsigned char depth;
};

// Default desktop mode and host image size when no registry setting overrides
// them.
constexpr int NativeWidth = 800, NativeHeight = 600, NativeDepth = 32;

// Add the attached monitor's usable size to the mode list, as Windows 98
// display drivers do for reported panel sizes. Zero removes the mode.
struct Panel
{
    unsigned short width = 0, height = 0;
};
inline Panel &PanelMode()
{
    static Panel panel;
    return panel;
}
// A panel that is not square is offered upright and lying, as the driver of
// a display that can be turned does.
constexpr U FixedModes = 14, PanelModes = 8;
inline const Mode *Table(U &count)
{
    static Mode modes[FixedModes + PanelModes] = {
        {640, 480, 8},  {640, 480, 16}, {640, 480, 24}, {640, 480, 32},
        {800, 600, 8},  {800, 600, 16}, {800, 600, 24}, {800, 600, 32},
        {1024, 768, 8}, {1024, 768, 16}, {1024, 768, 24}, {1024, 768, 32},
        // Low-resolution RGB is an ordinary linear framebuffer, not VGA
        // Mode X. Keep the existing mode indices stable. Half-Life's default
        // fullscreen selection requires this mode; when absent it continues
        // drawing a 320x240 image into the previous 640x480 primary surface.
        {320, 240, 16},
        // Linear indexed SVGA mode. Original DDRAW validates SetDisplayMode
        // against this driver list before calling the HAL. Doom95 keeps
        // drawing indexed rows even when that validation rejects its mode.
        // Append to preserve the existing fixed mode indices.
        {640, 400, 8},
    };
    count = FixedModes;
    const Panel &panel = PanelMode();
    if (!panel.width || !panel.height) return modes;
    for (U i = 0; i < FixedModes; ++i)
        if (modes[i].width == panel.width && modes[i].height == panel.height) return modes;
    static const unsigned char depths[4] = {8, 16, 24, 32};
    for (U i = 0; i < 4; ++i) modes[count++] = {panel.width, panel.height, depths[i]};
    bool turned = panel.width != panel.height && panel.height >= 640 && panel.width >= 480;
    for (U i = 0; i < FixedModes; ++i)
        if (modes[i].width == panel.height && modes[i].height == panel.width) turned = false;
    for (U i = 0; turned && i < 4; ++i) modes[count++] = {panel.height, panel.width, depths[i]};
    return modes;
}
inline bool SetPanel(int width, int height)
{
    if (width < 0 || height < 0 || width > 16384 || height > 16384) return false;
    if ((width == 0) != (height == 0)) return false;
    if (width && (width < 640 || height < 480)) return false;
    PanelMode() = {(unsigned short)width, (unsigned short)height};
    return true;
}

// GDI aligns each row to a DWORD; the DIB engine uses that stride.
inline int Stride(int width, int depth) { return ((width * depth + 31) / 32) * 4; }

inline bool Supported(int width, int height, int depth)
{
    U count = 0;
    const Mode *modes = Table(count);
    for (U i = 0; i < count; ++i)
        if (modes[i].width == width && modes[i].height == height && modes[i].depth == depth)
            return true;
    return false;
}

inline bool Native(int width, int height, int depth)
{
    return width == NativeWidth && height == NativeHeight && depth == NativeDepth;
}

// Any listed mode can host the desktop. Compose at that size and scale
// non-800x600 modes into the host image when publishing.
inline bool DesktopCapable(int width, int height, int depth)
{
    return Supported(width, height, depth);
}

// Source screen surface. colors contains 0x00RRGGBB palette entries for
// indexed depths.
struct Surface
{
    const unsigned char *bits = nullptr;
    int width = 0, height = 0, stride = 0, depth = 32;
    const U *colors = nullptr;
};

// A pixel in 0x00RRGGBB form. Sixteen-bit BI_RGB uses 5-5-5 when no color
// masks are supplied.
inline U Pixel(const Surface &s, int x, int y)
{
    const unsigned char *row = s.bits + (long)y * s.stride;
    switch (s.depth)
    {
    case 8:
    {
        const U index = row[x];
        return s.colors ? (s.colors[index] & 0xffffff) : (index * 0x010101u);
    }
    case 16:
    {
        const U v = U(row[x * 2]) | (U(row[x * 2 + 1]) << 8);
        const U r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
        return (((r * 255 + 15) / 31) << 16) | (((g * 255 + 15) / 31) << 8) | ((b * 255 + 15) / 31);
    }
    case 24:
        return U(row[x * 3]) | (U(row[x * 3 + 1]) << 8) | (U(row[x * 3 + 2]) << 16);
    default:
        return U(row[x * 4]) | (U(row[x * 4 + 1]) << 8) | (U(row[x * 4 + 2]) << 16);
    }
}

// Convert the surface to a 32-bit host image of width by height pixels using
// nearest-neighbor row and column mapping.
inline void Publish(const Surface &s, unsigned char *out, int width, int height, int stride)
{
    if (!s.bits || !out || s.width <= 0 || s.height <= 0) return;
    // Avoid per-pixel division for frequently published 16-bit frames.
    // Precompute the same color conversion as Pixel, advance source columns
    // with a remainder and copy repeated source rows.
    static U sixteen[65536];
    static bool sixteen_ready = false;
    if (s.depth == 16 && !sixteen_ready)
    {
        for (U v = 0; v < 65536; ++v)
        {
            const U r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
            sixteen[v] = (((r * 255 + 15) / 31) << 16) | (((g * 255 + 15) / 31) << 8) | ((b * 255 + 15) / 31);
        }
        sixteen_ready = true;
    }
    const bool same_width = s.width == width;
    const int whole = same_width ? 1 : s.width / width, part = same_width ? 0 : s.width % width;
    int last_sy = -1;
    for (int y = 0; y < height; ++y)
    {
        const int sy = s.height == height ? y : int((long long)y * s.height / height);
        unsigned char *row = out + (long)y * stride;
        const unsigned char *source = s.bits + (long)sy * s.stride;
        if (sy == last_sy)
        {
            const unsigned char *above = row - stride;
            for (int i = 0; i < width * 4; ++i) row[i] = above[i];
            continue;
        }
        last_sy = sy;
        if (s.depth == 32 && same_width)
        {
            for (int i = 0; i < width * 4; ++i) row[i] = source[i];
            continue;
        }
        if (s.depth == 16 && same_width)
        {
            for (int x = 0; x < width; ++x)
            {
                const U pixel = sixteen[U(source[x * 2]) | (U(source[x * 2 + 1]) << 8)];
                row[x * 4] = pixel & 255;
                row[x * 4 + 1] = (pixel >> 8) & 255;
                row[x * 4 + 2] = (pixel >> 16) & 255;
                row[x * 4 + 3] = 0;
            }
            continue;
        }
        // Source column floor(x * s.width / width); rest tracks the division
        // remainder.
        int sx = 0, rest = 0;
        for (int x = 0; x < width; ++x)
        {
            U pixel;
            switch (s.depth)
            {
            case 8:
            {
                const U index = source[sx];
                pixel = s.colors ? (s.colors[index] & 0xffffff) : (index * 0x010101u);
                break;
            }
            case 16:
                pixel = sixteen[U(source[sx * 2]) | (U(source[sx * 2 + 1]) << 8)];
                break;
            case 24:
                pixel = U(source[sx * 3]) | (U(source[sx * 3 + 1]) << 8) | (U(source[sx * 3 + 2]) << 16);
                break;
            default:
                pixel = U(source[sx * 4]) | (U(source[sx * 4 + 1]) << 8) | (U(source[sx * 4 + 2]) << 16);
                break;
            }
            row[x * 4] = pixel & 255;
            row[x * 4 + 1] = (pixel >> 8) & 255;
            row[x * 4 + 2] = (pixel >> 16) & 255;
            row[x * 4 + 3] = 0;
            sx += whole;
            rest += part;
            if (rest >= width) { rest -= width; ++sx; }
        }
    }
}

// Default eight-bit palette: twenty fixed Windows colors in slots 0..9 and
// 246..255, with other slots black. GDI installs it on mode change before an
// application realizes a palette.
inline void DefaultPalette(U *colors)
{
    U fixed[20];
    for (U i = 0; i < 20; ++i) {
        U bgr = Win98GdiPalette::DefaultColors[i];
        fixed[i] = ((bgr & 255) << 16) | (bgr & 0xff00) | ((bgr >> 16) & 255);
    }
    for (U i = 0; i < 256; ++i) colors[i] = 0;
    for (U i = 0; i < 10; ++i) colors[i] = fixed[i];
    for (U i = 0; i < 10; ++i) colors[246 + i] = fixed[10 + i];
}
} // namespace Win98DisplayModes
