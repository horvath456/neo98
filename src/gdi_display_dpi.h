// SPDX-License-Identifier: GPL-2.0-or-later
// The font size of the display, in dots per inch.
// GDI.EXE takes four numbers from HKEY_CURRENT_CONFIG\Display\Settings after
// the display driver has filled its GDIINFO: DPILogicalX and DPILogicalY
// replace dpLogPixelsX and dpLogPixelsY (GetDeviceCaps 88 and 90, the size of
// every font asked for in points), DPIPhysicalX and DPIPhysicalY give the
// size of the screen in millimetres (dpHorzSize = MulDiv(dpHorzRes, 254,
// DPIPhysicalX) / 10) and the viewport extents of the metric and English
// mapping modes. A value may be a string of digits, a DWORD or binary; a
// missing value keeps the driver's number, 96. When one of the four is not
// between 1 and 1000, all four are 96. "Small Fonts" of Display Properties
// writes 96 to all four, "Large Fonts" 120, a custom size its own number.
#pragma once
namespace Gdi
{
struct DisplayDpi
{
    int logical_x = 96, logical_y = 96, physical_x = 96, physical_y = 96;
};
inline DisplayDpi display_dpi;
inline const DisplayDpi &Dpi() { return display_dpi; }
inline void SetDpi(int logical_x, int logical_y, int physical_x, int physical_y)
{
    const int values[4] = {logical_x, logical_y, physical_x, physical_y};
    for (int value : values)
        if (value < 1 || value > 1000)
        {
            display_dpi = DisplayDpi{};
            return;
        }
    display_dpi = {logical_x, logical_y, physical_x, physical_y};
}
// MulDiv as GDI and USER use it for sizes: rounded to the nearest, halves
// away from zero.
inline int DpiMulDiv(int value, int numerator, int denominator)
{
    if (!denominator)
        return -1;
    long long product = (long long)value * numerator;
    const bool negative = (product < 0) != (denominator < 0);
    if (product < 0) product = -product;
    const long long divisor = denominator < 0 ? -(long long)denominator : denominator;
    const long long result = (product + divisor / 2) / divisor;
    return int(negative ? -result : result);
}
// A size in points as a font height in pixels: -MulDiv(points, DPILogicalY, 72).
inline int PointsToPixels(int points) { return DpiMulDiv(points, display_dpi.logical_y, 72); }
// A length in twips (1/1440 inch), as USER stores the sizes of a window's parts.
inline int TwipsToPixels(int twips) { return DpiMulDiv(twips, display_dpi.logical_y, 1440); }
inline int PixelsToTwips(int pixels) { return DpiMulDiv(pixels, 1440, display_dpi.logical_y); }
} // namespace Gdi
