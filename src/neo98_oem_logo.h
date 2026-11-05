// SPDX-License-Identifier: GPL-2.0-or-later
// OEMLOGO.BMP: the picture the original System Properties control panel shows
// on its General page under "Manufactured and supported by:", clipped to 180
// by 114 pixels (SYSDM does not scale it). It is the icon of Neo98.app (tools/mac_app.py, draw_icon):
// a Win98-grey window with a blue title bar and "98" on a teal rounded square,
// here on the grey of a dialog instead of transparency. Drawn with rectangles
// at four times the size and averaged down, so the thin edges stay visible.
#pragma once
namespace Neo98OemLogo
{
constexpr unsigned Width = 180, Height = 114, Scale = 4, BigW = Width * Scale, BigH = Height * Scale;
constexpr unsigned RowBytes = Width * 3, Header = 54, Bytes = Header + RowBytes * Height;

struct Canvas
{
    unsigned char rgb[BigW * BigH * 3];
    void Span(int y, double x0, double x1, unsigned char r, unsigned char g, unsigned char b)
    {
        if (y < 0 || y >= int(BigH)) return;
        int a = x0 < 0 ? 0 : int(x0), z = x1 > BigW ? int(BigW) : int(x1);
        for (int x = a; x < z; ++x)
        {
            unsigned char *p = rgb + (unsigned(y) * BigW + unsigned(x)) * 3;
            p[0] = r, p[1] = g, p[2] = b;
        }
    }
    void Rect(double x0, double y0, double x1, double y1, unsigned char r, unsigned char g, unsigned char b)
    {
        for (int y = int(y0); y < int(y1); ++y) Span(y, x0, x1, r, g, b);
    }
};

inline double Root(double v)
{
    if (v <= 0) return 0;
    double x = v > 1 ? v : 1;
    for (int i = 0; i < 40; ++i) x = (x + v / x) / 2;
    return x;
}

// Fills `out` (Bytes long) with a 24-bit bottom-up BMP file.
inline void Draw(unsigned char *out)
{
    static Canvas c;
    // The teal square (824 of the icon's 1024 units) 110 pixels high, in the
    // middle of the picture; the icon's transparent margin is cut off.
    const double u = 110.0 * Scale / 824, ox = (BigW - 1024 * u) / 2, oy = (BigH - 1024 * u) / 2;
    auto X = [&](double v) { return ox + v * u; };
    auto Y = [&](double v) { return oy + v * u; };
    c.Rect(0, 0, BigW, BigH, 192, 192, 192);
    // The rounded square, 824 of 1024 with a radius of 185, teal from top to bottom.
    const double rx0 = X(100), ry0 = Y(100), rx1 = X(924), ry1 = Y(924), radius = 185 * u;
    for (int y = int(ry0); y < int(ry1); ++y)
    {
        const double t = (y - ry0) / (ry1 - ry0 - 1);
        double inset = 0;
        if (y < ry0 + radius) { const double d = ry0 + radius - y - 0.5; inset = radius - Root(radius * radius - d * d); }
        else if (y >= ry1 - radius) { const double d = y - (ry1 - radius) + 0.5; inset = radius - Root(radius * radius - d * d); }
        c.Span(y, rx0 + inset, rx1 - inset, 0, (unsigned char)(150 - 50 * t), (unsigned char)(150 - 46 * t));
    }
    const double x0 = X(212), y0 = Y(262), x1 = X(812), y1 = Y(762), b = 10 * u;
    c.Rect(x0, y0, x1, y1, 0, 0, 0);
    c.Rect(x0, y0, x1 - b, y1 - b, 255, 255, 255);
    c.Rect(x0 + b, y0 + b, x1 - b, y1 - b, 128, 128, 128);
    c.Rect(x0 + b, y0 + b, x1 - 2 * b, y1 - 2 * b, 192, 192, 192);
    // The title bar with the Win98 gradient and three buttons.
    const double tx0 = x0 + 3 * b, ty0 = y0 + 3 * b, tx1 = x1 - 4 * b, ty1 = y0 + 3 * b + 72 * u;
    for (int x = int(tx0); x < int(tx1); ++x)
    {
        const double t = (x - tx0) / (tx1 - tx0 - 1);
        c.Rect(x, ty0, x + 1, ty1, (unsigned char)(16 * t), (unsigned char)(132 * t), (unsigned char)(128 + 80 * t));
    }
    const double side = 52 * u;
    for (int index = 0; index < 3; ++index)
    {
        const double bx1 = tx1 - 10 * u - index * (side + 8 * u), bx0 = bx1 - side, by0 = ty0 + 10 * u;
        c.Rect(bx0, by0, bx1, by0 + side, 0, 0, 0);
        c.Rect(bx0, by0, bx1 - 5 * u, by0 + side - 5 * u, 255, 255, 255);
        c.Rect(bx0 + 5 * u, by0 + 5 * u, bx1 - 5 * u, by0 + side - 5 * u, 192, 192, 192);
    }
    // The client area, sunken and white, with "98" in block digits.
    const double cx0 = tx0, cy0 = ty1 + 2 * b, cx1 = tx1, cy1 = y1 - 4 * b;
    c.Rect(cx0, cy0, cx1, cy1, 255, 255, 255);
    c.Rect(cx0, cy0, cx1 - b / 2, cy1 - b / 2, 128, 128, 128);
    c.Rect(cx0 + b / 2, cy0 + b / 2, cx1 - b / 2, cy1 - b / 2, 255, 255, 255);
    static const char *const digits[2][7] = {
        {"#####", "#...#", "#...#", "#####", "....#", "....#", "#####"},
        {"#####", "#...#", "#...#", "#####", "#...#", "#...#", "#####"}};
    const double cell = 40 * u, left = (cx0 + cx1 - 11 * cell) / 2, top = (cy0 + cy1 - 7 * cell) / 2;
    for (int position = 0; position < 2; ++position)
        for (int row = 0; row < 7; ++row)
            for (int column = 0; column < 5; ++column)
                if (digits[position][row][column] == '#')
                {
                    const double gx = left + (position * 6 + column) * cell, gy = top + row * cell;
                    c.Rect(gx, gy, gx + cell + 1, gy + cell + 1, 0, 0, 128);
                }
    // BITMAPFILEHEADER and BITMAPINFOHEADER, then the rows bottom up in BGR.
    for (unsigned i = 0; i < Header; ++i) out[i] = 0;
    auto put = [&](unsigned at, unsigned value, unsigned size) { for (unsigned i = 0; i < size; ++i) out[at + i] = (value >> (8 * i)) & 255; };
    out[0] = 'B', out[1] = 'M';
    put(2, Bytes, 4), put(10, Header, 4), put(14, 40, 4), put(18, Width, 4), put(22, Height, 4);
    put(26, 1, 2), put(28, 24, 2), put(34, RowBytes * Height, 4), put(38, 2835, 4), put(42, 2835, 4);
    for (unsigned y = 0; y < Height; ++y)
        for (unsigned x = 0; x < Width; ++x)
            for (unsigned k = 0; k < 3; ++k)
            {
                unsigned sum = 0;
                for (unsigned dy = 0; dy < Scale; ++dy)
                    for (unsigned dx = 0; dx < Scale; ++dx)
                        sum += c.rgb[((y * Scale + dy) * BigW + x * Scale + dx) * 3 + k];
                out[Header + (Height - 1 - y) * RowBytes + x * 3 + (2 - k)] = (unsigned char)(sum / (Scale * Scale));
            }
}
} // namespace Neo98OemLogo
