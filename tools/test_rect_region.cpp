// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/rect_region.h"
#include <cassert>
#include <cstdio>
int main()
{
    unsigned seed = 0x98a117;
    auto random = [&]()
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };
    unsigned failures = 0;
    for (unsigned trial = 0; trial < 20; ++trial)
    {
        RectRegion region;
        bool pixels[32][32] = {};
        for (unsigned n = 0; n < 150; ++n)
        {
            int x = random() % 33, y = random() % 33, x2 = random() % 33, y2 = random() % 33;
            Win98::Rect r = {x < x2 ? x : x2, y < y2 ? y : y2, x < x2 ? x2 : x, y < y2 ? y2 : y};
            bool add = random() & 1;
            auto previous = region;
            bool changed = add ? region.Add(r) : region.Subtract(r);
            if (changed)
                for (int row = r.top; row < r.bottom; ++row)
                    for (int col = r.left; col < r.right; ++col)
                        pixels[row][col] = add;
            else
            {
                ++failures;
                assert(previous.count == region.count);
                for (unsigned i = 0; i < region.count; ++i)
                    assert(Win98::EqualRect(&region.rectangles[i], &previous.rectangles[i]));
            }
            int minx = 32, miny = 32, maxx = 0, maxy = 0;
            for (int row = 0; row < 32; ++row)
                for (int col = 0; col < 32; ++col)
                {
                    unsigned hits = 0;
                    for (unsigned i = 0; i < region.count; ++i)
                        hits += Win98::PtInRect(&region.rectangles[i], col, row);
                    assert(hits == unsigned(pixels[row][col]));
                    if (hits)
                    {
                        if (col < minx)
                            minx = col;
                        if (row < miny)
                            miny = row;
                        if (col + 1 > maxx)
                            maxx = col + 1;
                        if (row + 1 > maxy)
                            maxy = row + 1;
                    }
                }
            const auto bounds = region.Bounds();
            if (minx == 32)
                assert(Win98::IsRectEmpty(&bounds));
            else
                assert(bounds.left == minx && bounds.top == miny && bounds.right == maxx &&
                       bounds.bottom == maxy);
        }
    }
    RectRegion full;
    for (int i = 0; i < 64; ++i)
        assert(full.Add({i * 2, 0, i * 2 + 1, 10}));
    assert(full.Add({200, 0, 201, 10}) && full.count == 65);
    assert(full.Subtract({0, 4, 1, 5}) && full.count == 66);
    puts("NATIVE REGIONS PASS: 3000 union/subtraction steps, independent pixel model, disjointness, bounds, "
         "growth beyond 64 rectangles");
}
