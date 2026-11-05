// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/user.h"
#include <cassert>
#include <cstdio>
static void Pixel(Gdi::HDC dc, int x, int y, unsigned color)
{
    const auto brush = Gdi::CreateSolidBrush(color);
    Gdi::RECT r{x, y, x + 1, y + 1};
    assert(Gdi::FillRect(dc, &r, brush));
    Gdi::DeleteObject(brush);
}
int main()
{
    assert(User::Initialize());
    User::RegisterClass("HostSurface", 0x71001000, 0, 0);
    const auto a = User::CreateWindow("HostSurface", "A", 0x10cf0000, 40, 40, 300, 240, 0, 0, 0);
    const auto b = User::CreateWindow("HostSurface", "B", 0x10cf0000, 40, 40, 300, 240, 0, 0, 0);
    assert(a && b);
    User::SetHostWindowSurfaces(true);
    auto da = User::GetDC(a), db = User::GetDC(b);
    assert(da && db);
    assert(User::WindowFromDC(da) == a && User::WindowFromDC(db) == b);
    Pixel(da, 20, 20, 0x112233);
    Pixel(db, 20, 20, 0x778899);
    assert(Gdi::GetPixel(da, 20, 20) == 0x112233);
    assert(Gdi::GetPixel(db, 20, 20) == 0x778899);
    int aw = 0, ah = 0, as = 0, bw = 0, bh = 0, bs = 0;
    const auto *ap = User::HostWindowPixels(a, aw, ah, as);
    const auto *bp = User::HostWindowPixels(b, bw, bh, bs);
    assert(ap && bp && ap != bp && aw == 300 && ah == 240 && bw == aw && bh == ah);
    assert(User::WindowVisibleRects(a, nullptr, 0) > 0);
    auto frame = User::GetWindowDC(a);
    assert(frame && User::WindowFromDC(frame) == a);
    Pixel(frame, 4, 4, 0xaabbcc);
    assert(Gdi::GetPixel(frame, 4, 4) == 0xaabbcc);
    assert(User::ReleaseDC(a, frame));
    // A held DC and its pixels survive a logical window move.
    assert(User::MoveWindow(a, 90, 70, 300, 240));
    User::HostWindowPixels(a, aw, ah, as);
    assert(Gdi::GetPixel(da, 20, 20) == 0x112233);
    User::SetHostPointerWindow(a);
    assert(User::CaptureMouse(120, 120, 1, 1).host_window == a);
    assert(User::ReleaseDC(a, da));
    assert(User::ReleaseDC(b, db));
    User::SetHostWindowSurfaces(false);
    assert(!User::HostWindowPixels(a, aw, ah, as));
    User::Shutdown();
    puts("HOST WINDOW SURFACES PASS: overlap, leases, frame DC, move, input "
         "target, teardown");
}
