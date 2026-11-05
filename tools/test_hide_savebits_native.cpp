// SPDX-License-Identifier: GPL-2.0-or-later
// Read the framebuffer and update debt without a DC/Render/GetUpdateRect
// call: each of those would settle delayed geometry and hide this bug.
#include "../src/user.cpp"
#include "../src/gdi_truetype.h"
#include <cstdio>
#include <cstdlib>

namespace GdiTrueType {
bool AbcWidths(const Font &, unsigned, unsigned, int *)
{
    std::fputs("Unexpected TrueType query in raster SaveBits fixture\n", stderr);
    std::abort();
}
}

static unsigned checks, failures;
static void require(bool okay, const char *name)
{
    if (okay) return;
    std::fprintf(stderr, "Fixture setup failed: %s\n", name);
    std::abort();
}
static void check(bool okay, const char *name)
{
    ++checks;
    if (!okay) ++failures;
    std::printf("%s %s\n", okay ? "PASS" : "FAIL", name);
}

static unsigned pixel(int x, int y)
{
    const auto *p = User::ScreenBits() + y * User::ScreenStride() + 4 * x;
    return p[0] | (unsigned(p[1]) << 8) | (unsigned(p[2]) << 16);
}

static void paint(unsigned h, unsigned color)
{
    User::Paint p{};
    const auto dc = User::BeginPaint(h, p);
    const auto brush = Gdi::CreateSolidBrush(color);
    Gdi::RECT rect{};
    User::GetClientRect(h, &rect);
    require(dc && Gdi::FillRect(dc, &rect, brush), "paint");
    User::EndPaint(h, p);
    Gdi::DeleteObject(brush);
}

struct Scene
{
    unsigned board, first, reused;
    Scene()
    {
        require(User::Initialize(), "USER initialization");
        require(User::SetScreenMode(800, 600, 32), "32-bit screen");
        unsigned plain[12] = {48, 0, 1, 0, 0, 1};
        unsigned saved[12] = {48, 0x800, 1, 0, 0, 1};
        require(User::RegisterNativeClass(plain, "SaveBoard"), "board class");
        require(User::RegisterNativeClass(saved, "SavePopup"), "saved class");
        board = User::CreateWindow("SaveBoard", "board", 0x90000000u, 0, 0, 600, 400, 0, 0, 0);
        first = User::CreateWindow("SavePopup", "first", 0x80000000u, 80, 80, 240, 140, board, 0, 0);
        reused = User::CreateWindow("SavePopup", "reused", 0x80000000u, 80, 80, 240, 140, board, 0, 0);
        require(board && first && reused, "windows");
        User::CompleteCreation(board); User::CompleteCreation(first); User::CompleteCreation(reused);
        User::Render();
        paint(board, 0x0000ff);
        User::ShowWindow(first, 8);
        paint(first, 0x888888);
        check(pixel(90, 100) == 0x888888, "popup covers the red board");
    }
    ~Scene() { User::Shutdown(); Gdi::ShutdownBitmaps(); }
};

int main()
{
    {
        Scene s;
        User::ShowWindow(s.first, 0);
        check(pixel(90, 100) == 0xff0000, "hide restores before returning");
        check(!User::Find(s.board)->update.count, "valid restore owes no board paint");
        User::ShowWindow(s.reused, 8);
        paint(s.reused, 0x555555);
        User::ShowWindow(s.reused, 0);
        check(pixel(90, 100) == 0xff0000, "existing popup saves the restored background");
    }
    {
        Scene s;
        User::DestroyWindow(s.first);
        check(pixel(90, 100) == 0xff0000, "destroy restores before returning");
        User::ShowWindow(s.reused, 8);
        paint(s.reused, 0x555555);
        User::DestroyWindow(s.reused);
        check(pixel(90, 100) == 0xff0000, "show after destroy keeps the original background");
    }
    {
        Scene s;
        User::MoveWindow(s.first, 140, 80, 240, 140);
        check(pixel(90, 100) == 0xff0000, "move restores the exposed strip synchronously");
        check(pixel(160, 100) == 0x888888 && pixel(350, 100) == 0x888888,
              "overlapping move preserves source pixels at the destination");
        check(!User::Find(s.board)->update.count, "moved valid SPB preserves direct board drawing");
    }
    {
        Scene s;
        User::MoveWindow(s.first, 80, 80, 120, 140);
        check(pixel(250, 100) == 0xff0000, "shrink restores the exposed strip synchronously");
        check(pixel(90, 100) == 0x888888, "shrink leaves the retained popup pixels intact");
    }
    {
        Scene s;
        const Gdi::RECT changed{85, 90, 105, 110};
        User::InvalidateRect(s.board, &changed, true);
        User::ValidateRect(s.board, nullptr);
        User::ShowWindow(s.first, 0);
        check(User::Find(s.board)->update.count != 0, "invalid SPB exposes paint debt before returning");
        check(pixel(90, 100) == 0x888888 && pixel(150, 100) == 0xff0000,
              "partial SPB restores only its valid pixels");
    }
    {
        Scene s;
        User::MoveWindow(s.reused, 340, 80, 240, 140);
        User::ShowWindow(s.reused, 8);
        paint(s.reused, 0x0000aa);
        User::MoveWindow(s.first, 340, 80, 240, 140, true);
        User::MoveWindow(s.reused, 80, 80, 240, 140, true);
        User::FinishWindowPositions();
        check(pixel(90, 100) == 0xaa0000 && pixel(350, 100) == 0x888888,
              "deferred swaps copy both sources before restoring SPBs");
    }
    {
        Scene s;
        User::MoveWindow(s.reused, 340, 80, 240, 140);
        User::ShowWindow(s.reused, 8);
        paint(s.reused, 0x0000aa);
        const auto other = User::CreateWindow("SaveBoard", "other", 0x90000000u,
                                               610, 80, 100, 100, 0, 0, 0);
        require(other != 0, "third window");
        User::CompleteCreation(other);
        paint(other, 0x00ff00);
        User::MoveWindow(s.first, 340, 80, 240, 140, true);
        User::ShowWindow(other, 0, true);
        User::MoveWindow(s.reused, 80, 80, 240, 140, true);
        User::FinishWindowPositions();
        check(pixel(90, 100) == 0xaa0000 && pixel(350, 100) == 0x888888,
              "a hide within a deferred swap preserves both copy sources");
    }
    {
        Scene s;
        User::ShowWindow(s.reused, 8);
        paint(s.reused, 0x555555);
        User::ShowWindow(s.first, 0);
        check(pixel(90, 100) == 0x555555, "hiding a lower SPB does not overwrite the upper popup");
        User::ShowWindow(s.reused, 0);
        check(User::Find(s.board)->update.count != 0, "upper SPB cannot restore the hidden lower popup");
    }
    {
        Scene s;
        User::ShowWindow(s.first, 0);
        const auto child = User::CreateWindow("SaveBoard", "scroll child", 0x50000000u,
                                               20, 260, 100, 40, s.board, 0, 0);
        require(child != 0, "scroll child");
        User::CompleteCreation(child);
        paint(child, 0xffff00);
        User::ValidateRect(s.board, nullptr);
        User::ScrollWindowArea(s.board, 20, 0, nullptr, nullptr, nullptr, true, false, 0, true);
        check(pixel(45, 270) == 0x00ffff && pixel(135, 270) == 0x00ffff,
              "ScrollWindow moves child pixels exactly once");
    }
    std::printf("HIDE SAVEBITS: %u/%u checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
