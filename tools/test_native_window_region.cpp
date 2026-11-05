// SPDX-License-Identifier: GPL-2.0-or-later
// Native window regions: painting, client DCs, hit testing and redraw.
#include "gdi.h"
#include "user.h"
#include "gdi_truetype.h"
#include <cstdlib>
namespace GdiTrueType { bool AbcWidths(const Font &, unsigned, unsigned, int *) { std::abort(); } }
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
static unsigned failures = 0;
static void check(bool okay, const char *name)
{
    printf("%s: %s\n", name, okay ? "PASS" : "FAIL");
    failures += !okay;
}
static const int Left = 120, Top = 90, Width = 121, Height = 101;
static int HalfWidth(int y)
{
    const double t = (y - Height / 2) / double(Height / 2);
    const double v = 1.0 - t * t;
    return v <= 0 ? 0 : int((Width / 2) * std::sqrt(v));
}
// An ellipse built from per-scanline rectangles. Native GDI has no
// CreateEllipticRgn; the original boolean core assembles the bands.
static unsigned EllipticRegion()
{
    unsigned region = Gdi::CreateRegionRect(0, 0, 0, 0);
    if (!region) return 0;
    for (int y = 0; y < Height; ++y) {
        const int half = HalfWidth(y);
        if (half <= 0) continue;
        const unsigned row = Gdi::CreateRegionRect(Width / 2 - half, y, Width / 2 + half, y + 1);
        if (!row) return 0;
        const int result = Gdi::CombineRegions(region, region, row, 2);
        Gdi::DeleteObject(row);
        if (!result) return 0;
    }
    return region;
}
static unsigned Pixel(int x, int y)
{
    const unsigned char *p = User::Pixels() + (y * 800 + x) * 4;
    return unsigned(p[0]) | unsigned(p[1]) << 8 | unsigned(p[2]) << 16;
}
int main()
{
    assert(User::Initialize());
    assert(User::RegisterClass("RegionWindow", 1, 0, 0));
    const auto window = User::CreateWindow("RegionWindow", "", 0x90000000u, Left, Top,
                                           Width, Height, 0, 0, 0);
    assert(window);
    User::CompleteCreation(window);
    Gdi::RECT client = {};
    assert(User::GetClientRect(window, &client));
    const unsigned objects_before = Gdi::LiveObjects();

    // Rejections first: they must not change any state.
    check(User::SetWindowRegion(User::GetDesktopWindow(), 0, true) == 0,
          "desktop rejects SetWindowRgn");
    const unsigned brush = Gdi::CreateSolidBrush(Gdi::RGB(255, 0, 0));
    assert(brush);
    check(User::SetWindowRegion(window, brush, true) == 0, "non-region handle rejected");
    check(User::SetWindowRegion(0x12345, 0, true) == 0, "invalid window rejected");
    check(User::WindowRegion(window) == 0, "no region after rejections");

    const unsigned region = EllipticRegion();
    assert(region);
    Gdi::RECT bounds = {};
    assert(Gdi::RegionBounds(region, &bounds));
    check(bounds.left == Width / 2 - HalfWidth(Height / 2) && bounds.right == Width / 2 + HalfWidth(Height / 2),
          "elliptic bounds");
    const unsigned rectangles = Gdi::RegionRectangles(region, nullptr, 0, 0, 0);
    check(rectangles > 40, "complex region has many bands");

    check(User::SetWindowRegion(window, region, true) != 0, "SetWindowRgn succeeds");
    check(User::WindowRegion(window) == region, "window adopted the handle");
    Gdi::RECT update = {};
    check(User::GetUpdateRect(window, &update), "bRedraw invalidated the window");

    // GetWindowRgn answers in window coordinates again.
    const unsigned destination = Gdi::CreateRegionRect(0, 0, 1, 1);
    assert(destination);
    check(User::GetWindowRegion(window, destination) != 0, "GetWindowRgn succeeds");
    Gdi::RECT returned = {};
    assert(Gdi::RegionBounds(destination, &returned));
    check(returned.left == bounds.left && returned.top == bounds.top &&
          returned.right == bounds.right && returned.bottom == bounds.bottom,
          "GetWindowRgn returns window coordinates");
    check(Gdi::RegionRectangles(destination, nullptr, 0, 0, 0) == rectangles,
          "GetWindowRgn returns the same bands");
    check(User::GetWindowRegion(window, brush) == 0, "GetWindowRgn rejects a non-region destination");

    // The client DC is clipped to the region.
    const auto dc = User::GetDC(window);
    assert(dc);
    const Gdi::RECT whole = {0, 0, client.right, client.bottom};
    assert(Gdi::FillRect(dc, &whole, brush));
    assert(User::ReleaseDC(window, dc));
    assert(User::Render());
    const int middle = Top + Height / 2;
    const unsigned background = Pixel(Left - 4, Top - 4), fill = Pixel(Left + Width / 2, middle);
    check(fill != background, "region centre painted");
    check(Pixel(Left + 1, Top + 1) == background, "corner outside the region keeps the desktop");
    check(Pixel(Left + Width / 2, Top + 1) == fill, "top centre is inside the region");
    const int half = HalfWidth(Height / 2);
    check(Pixel(Left + Width / 2 + half - 1, middle) == fill, "last pixel inside the widest band");
    check(Pixel(Left + Width / 2 + half + 1, middle) == background, "first pixel outside the band");

    // Hit testing follows the same region.
    const unsigned centre_hit = User::PointWindow(0, 0, Left + Width / 2, middle, 0, nullptr, nullptr, 1);
    const unsigned corner_hit = User::PointWindow(0, 0, Left + 1, Top + 1, 0, nullptr, nullptr, 1);
    check(centre_hit == window, "WindowFromPoint inside the region");
    check(corner_hit != window, "WindowFromPoint outside the region");
    check(User::WindowRegionContains(window, Left + Width / 2, middle), "point provider inside");
    check(!User::WindowRegionContains(window, Left + 1, Top + 1), "point provider outside");

    // The region follows the window.
    assert(User::MoveWindow(window, Left + 200, Top, Width, Height));
    check(User::PointWindow(0, 0, Left + 200 + Width / 2, middle, 0, nullptr, nullptr, 1) == window,
          "moved window hits inside the region");
    check(User::PointWindow(0, 0, Left + 201, Top + 1, 0, nullptr, nullptr, 1) != window,
          "moved window misses outside the region");
    assert(User::MoveWindow(window, Left, Top, Width, Height));

    // bRedraw = FALSE leaves the update region alone.
    User::ValidateRect(window, nullptr);
    const unsigned second = EllipticRegion();
    assert(second);
    check(User::SetWindowRegion(window, second, false) != 0, "SetWindowRgn without redraw");
    check(!User::GetUpdateRect(window, &update), "bRedraw FALSE did not invalidate");
    check(User::WindowRegion(window) == second, "new region replaced the old handle");
    check(Gdi::GetObjectType(region) == 0, "the replaced region was deleted");

    // Removing the region restores the full window.
    check(User::SetWindowRegion(window, 0, true) != 0, "SetWindowRgn(NULL) succeeds");
    check(User::WindowRegion(window) == 0, "region cleared");
    check(Gdi::GetObjectType(second) == 0, "the cleared region was deleted");
    check(User::GetWindowRegion(window, destination) == 0, "GetWindowRgn without a region");
    const auto again = User::GetDC(window);
    assert(again);
    assert(Gdi::FillRect(again, &whole, brush));
    assert(User::ReleaseDC(window, again));
    assert(User::Render());
    check(Pixel(Left + 1, Top + 1) == fill, "corner painted without a region");

    // The desktop answers GetWindowRgn from the screen region.
    check(User::GetWindowRegion(User::GetDesktopWindow(), destination) != 0,
          "desktop GetWindowRgn uses the screen region");

    // A window under a sibling with a window region sees through the parts
    // outside that region: the visible region subtracts the region's real
    // shape (USER over ), not its rectangle.
    {
        const int x = 450, y = 300;
        const auto under = User::CreateWindow("RegionWindow", "", 0x90000000u, x - 20, y - 20,
                                              Width + 40, Height + 40, 0, 0, 0);
        const auto over = User::CreateWindow("RegionWindow", "", 0x90000000u, x, y, Width, Height, 0, 0, 0);
        assert(under && over);
        User::CompleteCreation(under);
        User::CompleteCreation(over);
        const unsigned shape = EllipticRegion();
        assert(shape && User::SetWindowRegion(over, shape, true));
        const auto dc = User::GetDC(under);
        // The client of `under` starts 20 pixels before `over`: the corner of
        // `over` lies outside the ellipse, its centre inside.
        check(dc && Gdi::PtVisible(dc, 20 + 1, 20 + 1), "sibling region: corner outside the ellipse is visible");
        check(dc && !Gdi::PtVisible(dc, 20 + Width / 2, 20 + Height / 2), "sibling region: centre is covered");
        if (dc) User::ReleaseDC(under, dc);
        assert(User::DestroyWindow(over) && User::DestroyWindow(under));
    }
    // Teardown releases the adopted handle.
    const unsigned third = EllipticRegion();
    assert(third);
    assert(User::SetWindowRegion(window, third, true));
    assert(User::DestroyWindow(window));
    check(Gdi::GetObjectType(third) == 0, "DestroyWindow released the region");
    Gdi::DeleteObject(destination);
    Gdi::DeleteObject(brush);
    // USER keeps the cached screen region for the desktop's GetWindowRgn and
    // the up to three scratch regions of the RedrawWindow walk (
    // src/user_redraw_window.inc); Shutdown releases them.
    check(Gdi::LiveObjects() >= objects_before + 1 && Gdi::LiveObjects() <= objects_before + 4,
          "only the cached screen and redraw regions remain");
    const unsigned after_shutdown = (User::Shutdown(), Gdi::LiveObjects());
    check(after_shutdown < objects_before, "shutdown releases the screen, font and region objects");
    printf("native window region %s\n", failures ? "FAIL" : "PASS");
    return failures != 0;
}
