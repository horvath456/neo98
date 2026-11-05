// SPDX-License-Identifier: GPL-2.0-or-later
// exposure reaches parents, but child-only changes stay in their subtree.
#include "../src/user.cpp"
#include "../src/gdi_truetype.h"
#include <cstdio>
#include <cstdlib>

namespace GdiTrueType {
bool AbcWidths(const Font &, unsigned, unsigned, int *) { std::abort(); }
}

static unsigned checks, failures;
static void check(bool okay, const char *name)
{
    ++checks;
    failures += !okay;
    std::printf("%s %s\n", okay ? "PASS" : "FAIL", name);
}
static void require(bool okay) { if (!okay) std::abort(); }
static bool debt(unsigned h, int x, int y)
{
    const auto *w = User::Find(h);
    Gdi::RECT area{};
    require(User::ClientScreenRect(h, &area));
    for (unsigned i = 0; i < w->update.count; ++i) {
        const auto &r = w->update.rectangles[i];
        if (x >= area.left + r.left && x < area.left + r.right &&
            y >= area.top + r.top && y < area.top + r.bottom) return true;
    }
    return false;
}
static void paint(unsigned h, unsigned color, bool silent = false)
{
    User::Paint p{};
    const auto dc = User::BeginPaint(h, p);
    require(dc != 0);
    if (!silent) {
        Gdi::RECT r{};
        User::GetClientRect(h, &r);
        const auto brush = Gdi::CreateSolidBrush(color);
        require(Gdi::FillRect(dc, &r, brush));
        Gdi::DeleteObject(brush);
    }
    User::EndPaint(h, p);
}
static unsigned pixel(int x, int y)
{
    const auto *p = User::ScreenBits() + y * User::ScreenStride() + x * 4;
    return p[0] | (unsigned(p[1]) << 8) | (unsigned(p[2]) << 16);
}
struct Scene
{
    unsigned root, parent, child;
    Scene(bool clip = false)
    {
        require(User::Initialize());
        require(User::SetScreenMode(800, 600, 32));
        unsigned plain[12] = {48, 0, 1, 0, 0, 1};
        unsigned saved[12] = {48, 0x800, 1, 0, 0, 1};
        require(User::RegisterNativeClass(plain, "Exposure"));
        require(User::RegisterNativeClass(saved, "ExposureSaved"));
        root = window(0, 50, 50, 260, 220);
        parent = window(root, 20, 20, 180, 150, clip ? 0x02000000u : 0);
        child = window(parent, 20, 20, 100, 50);
        User::Render();
        paint(root, 0xff);
        paint(parent, 0xff, !clip);
        paint(child, 0xff, !clip);
        clear();
    }
    unsigned window(unsigned owner, int x, int y, int w, int h,
                    unsigned extra = 0, bool saved = false)
    {
        const auto result = User::CreateWindow(saved ? "ExposureSaved" : "Exposure", "",
            (owner ? 0x54000000u : 0x90000000u) | extra, x, y, w, h, owner, 0, 0);
        require(result != 0);
        User::CompleteCreation(result);
        return result;
    }
    void clear()
    {
        User::ValidateRect(root, nullptr);
        User::ValidateRect(parent, nullptr);
        User::ValidateRect(child, nullptr);
    }
    unsigned cover(bool internal = false, bool saved = false)
    {
        const auto h = window(internal ? parent : 0, internal ? 10 : 80,
                              internal ? 10 : 80, 130, 80, 0, saved);
        User::OrderWindow(h, 0);
        User::FinishWindowPositions();
        paint(h, 0xff0000);
        clear();
        require(pixel(110, 110) == 0x0000ff);
        return h;
    }
    ~Scene() { User::Shutdown(); Gdi::ShutdownBitmaps(); }
};

int main()
{
    for (unsigned action = 0; action < 5; ++action) {
        Scene s;
        const auto cover = s.cover();
        std::printf("external cover action=%u\n", action);
        if (action == 0) User::DestroyWindow(cover);
        if (action == 1) User::ShowWindow(cover, 0);
        if (action == 2) User::MoveWindow(cover, 140, 80, 130, 80);
        if (action == 3) User::MoveWindow(cover, 80, 80, 20, 80);
        if (action == 4) { User::OrderWindow(cover, 1); User::FinishWindowPositions(); }
        check(debt(s.root, 110, 110), "external exposure invalidates the root under its descendants");
        check(debt(s.parent, 110, 110), "external exposure invalidates the immediate parent");
        check(debt(s.child, 110, 110), "external exposure invalidates the silent child");
        check(!debt(s.root, 55, 55), "unaffected root pixels remain valid");
        if (action == 2)
            check(!debt(s.root, 170, 110) && !debt(s.child, 170, 110), "remaining cover is excluded");
        paint(s.root, 0xff);
        paint(s.parent, 0, true);
        paint(s.child, 0, true);
        check(pixel(110, 110) == 0xff0000, "parent paint restores red through both silent descendants");
    }
    {
        Scene s;
        User::ShowWindow(s.child, 0);
        check(debt(s.parent, 110, 110), "hiding a child invalidates its immediate parent");
        check(!debt(s.root, 110, 110), "hiding a child does not invalidate its grandparent");
    }
    {
        Scene s;
        const auto cover = s.cover(true);
        User::DestroyWindow(cover);
        check(debt(s.parent, 110, 110) && debt(s.child, 110, 110), "sibling cover exposes its parent and sibling");
        check(!debt(s.root, 110, 110), "sibling exposure stays below the cover's parent");
    }
    {
        Scene s(true);
        const auto cover = s.cover();
        User::DestroyWindow(cover);
        check(debt(s.child, 110, 110), "WS_CLIPCHILDREN still exposes the child");
        check(!debt(s.parent, 110, 110) && !debt(s.root, 110, 110), "WS_CLIPCHILDREN stops ancestor painting under the child");
    }
    {
        Scene s;
        const auto cover = s.cover(false, true);
        User::DestroyWindow(cover);
        check(!debt(s.root, 110, 110) && !debt(s.parent, 110, 110) && !debt(s.child, 110, 110),
              "valid SaveBits restoration leaves the whole subtree valid");
        check(pixel(110, 110) == 0xff0000, "valid SaveBits restores the silent child's red background");
    }
    {
        // Explorer resizes WorkerA after its rebar's toolbars have
        // painted synchronously. The rebar clips its children; WorkerA does
        // not. Geometry invalidation must pass that intermediate boundary.
        Scene s(true);
        paint(s.child, 0xff0000);
        s.clear();
        User::MoveWindow(s.root, 50, 50, 240, 220);
        check(debt(s.child, 110, 110), "resize invalidates descendants below WS_CLIPCHILDREN");
        paint(s.root, 0xff);
        paint(s.parent, 0xff);
        paint(s.child, 0xff0000);
        check(pixel(110, 110) == 0x0000ff, "grandchild repaints after resized ancestor erases it");
        s.clear();
        User::InvalidateRect(s.root, nullptr, true);
        check(!debt(s.child, 110, 110), "ordinary InvalidateRect still stops at WS_CLIPCHILDREN");
    }
    std::printf("EXPOSED CHILD: %u/%u checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
