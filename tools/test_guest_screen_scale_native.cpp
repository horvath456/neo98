// SPDX-License-Identifier: GPL-2.0-or-later
// Host unit test for tools/guest_screen_scale.h, independent of SDL and of
// Application: plain integer math, checked with ASan/UBSan.
#include "guest_screen_scale.h"
#include <cassert>
#include <cstdio>
#include <utility>

int main()
{
    // The native 800x600 mode is the identity in both directions: the vast
    // majority of the time nobody has switched modes, and this path must add
    // no conversion at all.
    for (int x : {0, 1, 399, 400, 799})
        for (int y : {0, 1, 299, 300, 599})
        {
            int gx = x, gy = y;
            GuestScreenScale::CanvasToGuest(gx, gy, 800, 600);
            assert(gx == x && gy == y);
            int cx = x, cy = y;
            GuestScreenScale::GuestToCanvas(cx, cy, 800, 600);
            assert(cx == x && cy == y);
        }

    // Moorhuhn's mode: the centre of the 640x480 screen has to land
    // on the centre of the 800x600 canvas, because that is where the
    // compositor actually draws it (Win98DisplayModes::Publish stretches the
    // whole mode onto the canvas). Before this header existed,
    // tools/neo98_host.cpp warped the real pointer straight to the guest's
    // (320, 240) without this conversion, landing 80 right and 60 below the
    // mode's own visual centre, matching the reported ~100 pixel offset
    // exactly (80*80 + 60*60 == 100*100).
    {
        int x = 320, y = 240;
        GuestScreenScale::GuestToCanvas(x, y, 640, 480);
        if (x != 400 || y != 300)
            fprintf(stderr, "GuestToCanvas(320,240,640,480) = %d,%d, expected 400,300\n", x, y);
        assert(x == 400 && y == 300);
        const int unconverted_dx = 400 - 320, unconverted_dy = 300 - 240;
        assert(unconverted_dx == 80 && unconverted_dy == 60);
        assert(unconverted_dx * unconverted_dx + unconverted_dy * unconverted_dy == 100 * 100);
    }
    // And the inverse: the canvas centre reads back as the mode's own centre.
    {
        int x = 400, y = 300;
        GuestScreenScale::CanvasToGuest(x, y, 640, 480);
        assert(x == 320 && y == 240);
    }
    // A larger mode (1024x768, Win98DisplayModes::Table) shrinks onto the
    // canvas instead of stretching; the centre still has to round-trip.
    {
        int x = 512, y = 384;
        GuestScreenScale::GuestToCanvas(x, y, 1024, 768);
        assert(x == 400 && y == 300);
        GuestScreenScale::CanvasToGuest(x, y, 1024, 768);
        assert(x == 512 && y == 384);
    }
    // Every corner of every mode stays inside that mode's own bounds; a
    // guest pointer position must never claim a pixel past its own screen.
    for (auto mode : {std::pair<int, int>{640, 480}, {1024, 768}, {800, 600}})
    {
        for (int cx : {0, 799})
            for (int cy : {0, 599})
            {
                int x = cx, y = cy;
                GuestScreenScale::CanvasToGuest(x, y, mode.first, mode.second);
                assert(x >= 0 && x < mode.first && y >= 0 && y < mode.second);
            }
        for (int gx : {0, mode.first - 1})
            for (int gy : {0, mode.second - 1})
            {
                int x = gx, y = gy;
                GuestScreenScale::GuestToCanvas(x, y, mode.first, mode.second);
                assert(x >= 0 && x < 800 && y >= 0 && y < 600);
            }
    }
    // An inactive or unknown mode (width or height 0) must not divide by
    // zero; the point is left exactly where it was.
    {
        int x = 42, y = 17;
        GuestScreenScale::CanvasToGuest(x, y, 0, 0);
        assert(x == 42 && y == 17);
        GuestScreenScale::GuestToCanvas(x, y, 0, 0);
        assert(x == 42 && y == 17);
    }

    // The canvas follows the desktop mode: a 1024x768 desktop is the
    // identity on a 1024x768 canvas, and a 640x480 game on it is stretched.
    {
        int x = 1023, y = 767;
        GuestScreenScale::CanvasToGuest(x, y, 1024, 768, 1024, 768);
        assert(x == 1023 && y == 767);
        GuestScreenScale::GuestToCanvas(x, y, 1024, 768, 1024, 768);
        assert(x == 1023 && y == 767);
        x = 320; y = 240;
        GuestScreenScale::GuestToCanvas(x, y, 640, 480, 1024, 768);
        assert(x == 512 && y == 384);
        GuestScreenScale::CanvasToGuest(x, y, 640, 480, 1024, 768);
        assert(x == 320 && y == 240);
        x = 639; y = 479;
        GuestScreenScale::CanvasToGuest(x, y, 800, 600, 640, 480);
        assert(x == 798 && y == 598);
    }
    // The window for a canvas: scale 2 while it fits the usable screen,
    // then scale 1, then fitted with 4:3 kept.
    {
        int w = 0, h = 0;
        GuestScreenScale::WindowForCanvas(640, 480, 2, 1440, 875, w, h);
        assert(w == 640 && h == 480);
        GuestScreenScale::WindowForCanvas(640, 480, 2, 1440, 1000, w, h);
        assert(w == 1280 && h == 960);
        GuestScreenScale::WindowForCanvas(1024, 768, 2, 1440, 875, w, h);
        assert(w == 1024 && h == 768);
        GuestScreenScale::WindowForCanvas(1024, 768, 1, 1280, 700, w, h);
        assert(w == 933 && h == 700);
        GuestScreenScale::WindowForCanvas(1024, 768, 1, 900, 1000, w, h);
        assert(w == 900 && h == 675);
        GuestScreenScale::WindowForCanvas(800, 600, 1, 0, 0, w, h);
        assert(w == 800 && h == 600);
        GuestScreenScale::WindowForCanvas(800, 600, 2, 0, 0, w, h);
        assert(w == 1600 && h == 1200);
    }

    printf("GUEST SCREEN SCALE native 800x600 is the identity, Moorhuhn's 640x480 "
           "centre (320,240) becomes the canvas centre (400,300), the unconverted "
           "gap is 80,60 (sqrt 100) matching every corner stays on its own "
           "screen, the canvas follows the desktop size, the window fits the Mac screen\n");
    return 0;
}
