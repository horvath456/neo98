// SPDX-License-Identifier: GPL-2.0-or-later
// Host bridge for the implemented scrollbar track and thumb body,
// USER.EXE with its fill helper.
#include "../src/win98_scroll_family.h"
#include "../src/user_draw.h"

namespace {
struct Probe {
    unsigned char *pixels = nullptr;
    int width = 96, height = 80;
    unsigned trace = 2166136261u;
    const unsigned *colors = nullptr;
    unsigned brush = 0, dither = 0, highlight = 0;
    unsigned background = 0xffffff, foreground = 0;
    unsigned vertical = 0;
    int cross_start = 0, cross_end = 0;
    Win98Draw::Painter painter{};
};

void Record(Probe &p, const unsigned *values, unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        for (unsigned b = 0; b < 4; ++b)
            p.trace = (p.trace ^ ((values[i] >> (b * 8)) & 255)) * 16777619u;
}
void Plot(Probe &p, int x, int y, unsigned rgb)
{
    if (!p.pixels || x < 0 || y < 0 || x >= p.width || y >= p.height)
        return;
    unsigned char *pixel = p.pixels + (y * p.width + x) * 4;
    for (unsigned b = 0; b < 4; ++b)
        pixel[b] = rgb >> (b * 8);
}
void ProbeFill(void *context, int x, int y, int w, int h, unsigned rgb)
{
    auto &p = *static_cast<Probe *>(context);
    const unsigned values[] = {0x464c4c46u, (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, rgb};
    Record(p, values, 6);
    if (w <= 0 || h <= 0)
        return;
    for (int row = y; row < y + h; ++row)
        for (int col = x; col < x + w; ++col)
            Plot(p, col, row, rgb);
}
void ProbePattern(void *context, int x, int y, int w, int h, unsigned background, unsigned foreground)
{
    auto &p = *static_cast<Probe *>(context);
    const unsigned values[] = {0x54544150u, (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h,
                               background, foreground};
    Record(p, values, 7);
    if (w <= 0 || h <= 0)
        return;
    for (int row = y; row < y + h; ++row)
        for (int col = x; col < x + w; ++col)
            Plot(p, col, row, (row + col) & 1 ? background : foreground);
}
void ProbeInvert(Probe &p, int x, int y, int w, int h)
{
    const unsigned values[] = {0x54564e49u, (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h};
    Record(p, values, 5);
    if (w <= 0 || h <= 0)
        return;
    for (int row = y; row < y + h; ++row)
        for (int col = x; col < x + w; ++col) {
            if (!p.pixels || col < 0 || row < 0 || col >= p.width || row >= p.height)
                continue;
            unsigned char *pixel = p.pixels + (row * p.width + col) * 4;
            unsigned value = 0;
            for (unsigned b = 0; b < 4; ++b)
                value |= (unsigned)pixel[b] << (b * 8);
            value ^= 0xffffffu;
            for (unsigned b = 0; b < 4; ++b)
                pixel[b] = value >> (b * 8);
        }
}

// USER as the scrollbar reaches it: a color index plus one, or a raw
// brush handle. The recorded environment maps 0x100+index onto the same colors
// and keeps one dither brush, exactly as the original comparison does.
void FillBrush(Probe &p, const Gdi::RECT &r)
{
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (p.brush == p.dither) {
        ProbePattern(&p, r.left, r.top, w, h, p.background, p.foreground);
        return;
    }
    const unsigned index = p.brush - 1;
    const unsigned rgb = index <= 0x1du ? p.colors[index] : p.colors[p.brush - 0x100];
    ProbeFill(&p, r.left, r.top, w, h, rgb);
}

struct Provider {
    Probe &p;
    Gdi::RECT Span(int a, int z) const
    {
        return p.vertical ? Gdi::RECT{p.cross_start, a, p.cross_end, z}
                          : Gdi::RECT{a, p.cross_start, z, p.cross_end};
    }
    void Track(int a, int z)
    {
        Gdi::RECT r = Span(a, z);
        unsigned edge = 0, flags = 0;
        if (Win98Scroll::TrackEdge(p.brush, p.highlight, p.dither, p.vertical, edge, flags))
            Win98Draw::DrawEdge(p.painter, &r, edge, flags);
        FillBrush(p, r);
    }
    void Thumb(int a, int z)
    {
        Gdi::RECT r = Span(a, z);
        Win98Draw::ButtonFrame(p.painter, &r, 0, 0);
    }
    void Invert(const Win98Scroll::Press &press)
    {
        ProbeInvert(p, press.left, press.top, press.right - press.left,
                    press.bottom - press.top);
    }
};
}

extern "C" unsigned scroll_paint_probe(unsigned char *pixels, const int *geometry,
                                       unsigned disabled, unsigned vertical, unsigned brush,
                                       unsigned window, int *press, const unsigned *colors,
                                       int depth, int border_width, int border_height,
                                       unsigned background, unsigned foreground, unsigned *trace)
{
    Probe p;
    p.pixels = pixels;
    p.colors = colors;
    p.brush = brush;
    p.dither = 0x380;
    p.highlight = 0x100 + 20;      // the recorded COLOR_BTNHIGHLIGHT brush handle
    p.background = background;
    p.foreground = foreground;
    p.vertical = vertical;
    p.cross_start = geometry[2];
    p.cross_end = geometry[3];
    p.painter.context = &p;
    p.painter.fill = ProbeFill;
    p.painter.pattern = ProbePattern;
    p.painter.depth = depth;
    p.painter.border_width = border_width;
    p.painter.border_height = border_height;
    p.painter.window = colors[5];
    p.painter.frame = colors[6];
    p.painter.face = colors[15];
    p.painter.shadow = colors[16];
    p.painter.highlight = colors[20];
    p.painter.dark_shadow = colors[21];
    p.painter.light = colors[22];
    Win98Scroll::Geometry g{geometry[0], geometry[1], geometry[2], geometry[3], 0,
                            geometry[4], geometry[5], geometry[6], geometry[7]};
    Win98Scroll::Press state;
    state.part = (unsigned)press[0];
    state.window = (unsigned)press[1];
    state.vertical = (unsigned)press[2];
    state.left = press[3];
    state.top = press[4];
    state.right = press[5];
    state.bottom = press[6];
    Provider provider{p};
    Win98Scroll::DrawTrack(provider, g, disabled, vertical, window, &state);
    press[3] = state.left;
    press[4] = state.top;
    press[5] = state.right;
    press[6] = state.bottom;
    *trace = p.trace;
    return 1;
}
