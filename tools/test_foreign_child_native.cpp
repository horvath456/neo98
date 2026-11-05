// SPDX-License-Identifier: GPL-2.0-or-later
#include "user.h"
#include "gdi.h"
#include <cassert>
#include <cstdio>

static unsigned cleaned[3];
static void Cleanup(void *context, User::HWND window)
{
    const auto process = User::WindowProcess(window);
    assert(process == 1 || process == 2);
    assert(context == User::ProcessContextOf(process));
    ++cleaned[process];
}
static void Fill(User::HWND window, unsigned color)
{
    auto dc = User::GetDC(window);
    assert(dc);
    Gdi::RECT rect{0, 0, 100, 100};
    auto brush = Gdi::CreateSolidBrush(color);
    assert(brush && Gdi::FillRect(dc, &rect, brush));
    assert(User::ReleaseDC(window, dc));
    assert(Gdi::DeleteObject(brush));
}
static unsigned Pixel(int x, int y)
{
    const auto *p = User::Pixels() + (y * 800 + x) * 4;
    return p[0] | unsigned(p[1]) << 8 | unsigned(p[2]) << 16;
}
int main()
{
    assert(User::Initialize());
    int parent_context, child_context;
    assert(User::AttachProcess(1, &parent_context));
    assert(User::AttachProcess(2, &child_context));
    User::SetPropertyCleanup(nullptr, Cleanup);
    User::BindTask(1, 11);
    assert(User::RegisterClass("PreviewParent", 1, 0, 0));
    auto parent = User::CreateWindow("PreviewParent", "preview", 0x90000000u,
                                   40, 40, 100, 100, 0, 0, 0, 11);
    assert(parent);
    User::CompleteCreation(parent);
    Fill(parent, Gdi::RGB(0, 0, 255));
    User::BindTask(2, 22);
    assert(User::RegisterClass("PreviewChild", 1, 0, 0));
    assert(!User::CreateWindow("PreviewChild", "invalid", 0x50000000u,
                               0, 0, 100, 100, 0xdead, 0, 0, 22));
    assert(User::CreateWindowError() == 1400);
    auto create = [&] {
        auto child = User::CreateWindow("PreviewChild", "saver", 0x50000000u,
                                       10, 10, 100, 100, parent, 0, 0, 22);
        assert(child);
        User::CompleteCreation(child);
        assert(User::Parent(child) == parent && !User::Owner(child));
        assert(User::WindowProcess(child) == 2 && User::WindowProcess(parent) == 1);
        assert(User::CallbackContext(nullptr, child) == &child_context);
        assert(User::CallbackContext(nullptr, parent) == &parent_context);
        Gdi::RECT rect;
        assert(User::GetWindowRect(child, &rect));
        assert(rect.left == 50 && rect.top == 50 && rect.right == 150 && rect.bottom == 150);
        Fill(child, Gdi::RGB(255, 0, 0));
        User::Render();
        assert(Pixel(55, 55) == 0xff0000);
        assert(Pixel(45, 45) == 0xff);
        assert(Pixel(145, 145) != 0xff0000); // The parent clips its child.
        return child;
    };
    auto child = create();
    User::ShowWindow(parent, 0);
    assert(!User::IsVisible(child));
    User::ShowWindow(parent, 5);
    assert(User::IsVisible(child));
    User::ReleaseProcessWindows(2);
    assert(!User::IsWindow(child) && User::IsWindow(parent));
    assert(cleaned[1] == 0 && cleaned[2] == 1);
    // Releasing a process also removes its classes.
    assert(User::RegisterClass("PreviewChild", 1, 0, 0));
    child = create();
    User::BindTask(1, 11);
    User::ReleaseProcessWindows(1);
    assert(!User::IsWindow(parent) && !User::IsWindow(child));
    assert(cleaned[1] == 1 && cleaned[2] == 2);
    User::DetachProcess(2);
    User::DetachProcess(1);
    puts("FOREIGN CHILD PASS: ownership, contexts, geometry, clipping, visibility, process cleanup, invalid parent");
}
