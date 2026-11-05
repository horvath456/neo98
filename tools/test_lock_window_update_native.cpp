// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/gdi.h"
#include "../src/user.h"
#include "../src/gdi_truetype.h"
#include <cstdlib>
#include <cassert>
#include <cstdio>

using U = unsigned;

// This bitmap-only provider test must never enter the TrueType driver.
namespace GdiTrueType { bool AbcWidths(const Font &, unsigned, unsigned, int *) { std::abort(); } }

static int clip(U dc, Gdi::RECT &rect)
{
    rect = {};
    return int(Gdi::GetClipBox(dc, &rect));
}

int main()
{
    assert(User::Initialize());
    assert(User::RegisterClass("LockWindowUpdateNative", 1, 0, 0));
    const U target = User::CreateWindow("LockWindowUpdateNative", "target",
                                        0x90000000u, 20, 20, 140, 90, 0, 0, 0);
    const U child = User::CreateWindow("LockWindowUpdateNative", "child",
                                       0x50000000u, 8, 8, 60, 40, target, 0, 0);
    const U other = User::CreateWindow("LockWindowUpdateNative", "other",
                                       0x90000000u, 240, 20, 140, 90, 0, 0, 0);
    assert(target && child && other);
    User::CompleteCreation(target); User::CompleteCreation(child);
    User::CompleteCreation(other);
    assert(User::Render());

    Gdi::RECT rect;
    U dc = User::GetDC(target);
    assert(dc && clip(dc, rect) == 2);
    assert(User::ReleaseDC(target, dc));

    assert(!User::LockWindowUpdate(0));
    assert(User::LockWindowUpdate(target));
    assert(!User::LockWindowUpdate(target));
    assert(!User::LockWindowUpdate(other));

    dc = User::GetDC(target);
    assert(dc && clip(dc, rect) == 1);
    assert(User::ReleaseDC(target, dc));
    dc = User::GetDC(child);
    assert(dc && clip(dc, rect) == 1);
    assert(User::ReleaseDC(child, dc));

    dc = User::GetDC(other);
    assert(dc && clip(dc, rect) == 2);
    assert(User::ReleaseDC(other, dc));
    dc = User::GetDC(0);
    assert(dc && !Gdi::PtVisible(dc, 30, 30) && Gdi::PtVisible(dc, 230, 30));
    assert(User::ReleaseDC(0, dc));

    User::InvalidateRect(target, nullptr, true);
    User::Paint paint{};
    dc = User::BeginPaint(target, paint);
    assert(dc && clip(dc, rect) == 1);
    // The erase of DefWindowProc: nothing reaches the screen, but GDI keeps
    // the bounds for USER, and the unlock makes them invalid again.
    {
        const U brush = Gdi::CreateSolidBrush(0xffffff);
        const Gdi::RECT all{0, 0, 140, 90};
        assert(Gdi::FillRect(dc, &all, brush));
        Gdi::DeleteObject(brush);
    }
    assert(User::EndPaint(target, paint));
    assert(!User::GetUpdateRect(target, &rect));
    assert(User::Render());
    assert(User::Dirty() == false);

    assert(User::AttachProcess(77, nullptr));
    User::BindTask(77, 770);
    assert(!User::LockWindowUpdate(0));
    User::BindTask(0, 0);
    assert(User::LockWindowUpdate(0));
    assert(User::Dirty());
    assert(User::GetUpdateRect(target, &rect));
    assert(User::Render());

    // USER: the unlock invalidates what the DCs of the
    // locked windows tried to draw (GDI's bounds for USER, GetBoundsRect
    // 8001h ), not their whole visible area.
    {
        const U brush = Gdi::CreateSolidBrush(0x0000ff);
        assert(User::LockWindowUpdate(target));
        dc = User::GetDC(child);
        const Gdi::RECT spot{5, 6, 25, 16};
        assert(dc && Gdi::FillRect(dc, &spot, brush));
        assert(User::ReleaseDC(child, dc));
        // A DC that draws nothing adds nothing.
        dc = User::GetDC(target);
        assert(dc && User::ReleaseDC(target, dc));
        assert(User::ValidateRect(target, nullptr) && User::ValidateRect(child, nullptr) &&
               User::ValidateRect(other, nullptr));
        assert(User::LockWindowUpdate(0));
        Gdi::RECT drawn{};
        assert(User::GetUpdateRect(child, &drawn));
        if (drawn.left != 5 || drawn.top != 6 || drawn.right != 25 || drawn.bottom != 16)
            std::printf("child update %d,%d,%d,%d\n", drawn.left, drawn.top, drawn.right, drawn.bottom);
        assert(drawn.left == 5 && drawn.top == 6 && drawn.right == 25 && drawn.bottom == 16);
        // The child sits at 8,8 in the target's client: the target owes the
        // same pixels (RDW_ALLCHILDREN from the desktop, 85h).
        assert(User::GetUpdateRect(target, &drawn));
        assert(drawn.right - drawn.left == 20 && drawn.bottom - drawn.top == 10);
        assert(!User::GetUpdateRect(other, &drawn));
        assert(User::Render());
        Gdi::DeleteObject(brush);
    }

    assert(User::LockWindowUpdate(target));
    assert(User::DestroyWindow(target));
    assert(!User::LockWindowUpdate(0));
    assert(User::DestroyWindow(other));
    User::DetachProcess(77);
    User::Shutdown();
    Gdi::ShutdownBitmaps();
    std::puts("LOCK-WINDOW-UPDATE PASS: ownership, subtree clips, unrelated drawing, redraw, and teardown");
}
