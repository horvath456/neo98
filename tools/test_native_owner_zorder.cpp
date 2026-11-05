// SPDX-License-Identifier: GPL-2.0-or-later
// Native check for the implemented USER owned-window z-order pass: an owned
// popup created hidden and shown later must stay in front of its owner, also
// after the owner is raised, and must stay behind a WS_EX_TOPMOST window.
#include "gdi.h"
#include "user.h"
#include <cassert>
#include <cstdio>
static unsigned failures = 0;
static void check(bool okay, const char *name)
{
    printf("%s: %s\n", name, okay ? "PASS" : "FAIL");
    failures += !okay;
}
static unsigned Pixel(int x, int y)
{
    const unsigned char *p = User::Pixels() + (y * 800 + x) * 4;
    return unsigned(p[0]) | unsigned(p[1]) << 8 | unsigned(p[2]) << 16;
}
static void Fill(User::HWND window, int width, int height, unsigned colour)
{
    const auto dc = User::GetDC(window);
    assert(dc);
    Gdi::RECT area = {0, 0, width, height};
    const auto brush = Gdi::CreateSolidBrush(colour);
    assert(brush);
    Gdi::FillRect(dc, &area, brush);
    User::ReleaseDC(window, dc);
    Gdi::DeleteObject(brush);
}
static unsigned Order(User::HWND *out, unsigned capacity)
{
    unsigned count = 0;
    for (auto h = User::GetTopWindow(User::GetDesktopWindow()); h && count < capacity;
         h = User::GetWindow(h, 2))
        out[count++] = h;
    return count;
}
int main()
{
    assert(User::Initialize());
    assert(User::RegisterClass("ZOrderSheet", 1, 0, 0));
    // The property sheet the Date/Time applet shows: a top-level frame with a
    // child page, and the page holds the control that owns the drop-down.
    const auto sheet = User::CreateWindow("ZOrderSheet", "Sheet", 0x90000000u, 40, 40, 400, 300, 0, 0, 0);
    assert(sheet);
    User::CompleteCreation(sheet);
    Fill(sheet, 400, 300, Gdi::RGB(255, 0, 0));
    const auto page = User::CreateWindow("ZOrderSheet", "Page", 0x50000000u, 10, 10, 200, 100, sheet, 0, 0);
    assert(page);
    User::CompleteCreation(page);
    const auto control = User::CreateWindow("ZOrderSheet", "Combo", 0x50000000u, 10, 60, 75, 21, page, 0, 0);
    assert(control);
    User::CompleteCreation(control);
    User::Render();
    check(Pixel(200, 200) == 0xff0000, "owner painted");

    // The dropped list: a top-level WS_POPUP owned by the control, created
    // hidden, sized like the applet's open month list at 60,130..135,307.
    const auto list = User::CreateWindow("BUTTON", "List", 0x80000000u, 60, 130, 75, 177, control, 0, 0);
    assert(list);
    User::CompleteCreation(list);
    check(!User::Parent(list) && User::Owner(list) == control, "list is an owned popup");
    check(!User::IsVisible(list), "list starts hidden");

    // The sheet is activated after the control was created, which is what put
    // the still hidden list behind it before this package.
    check(User::OrderWindow(sheet, 0), "sheet raised");
    User::HWND order[8] = {};
    unsigned count = Order(order, 8);
    check(count == 2 && order[0] == list && order[1] == sheet, "owned popup stays in front");

    assert(User::ShowWindow(list, 5) == false);
    check(User::IsVisible(list), "list shown");
    User::Render();
    check(Pixel(90, 200) == 0xc0c0c0, "popup paints over its owner");
    check(Pixel(90, 290) == 0xc0c0c0, "popup paints outside its owner");
    check(Pixel(300, 200) == 0xff0000, "owner keeps the rest of its client");

    // Raising the owner again must carry the visible popup with it.
    check(User::OrderWindow(sheet, 0), "sheet raised again");
    count = Order(order, 8);
    check(count == 2 && order[0] == list && order[1] == sheet, "raise keeps the group order");
    User::Render();
    check(Pixel(90, 200) == 0xc0c0c0, "popup still painted after the raise");

    // A WS_EX_TOPMOST window stays above the owned group; the original keeps
    // the two bands apart and the owned pass works inside one band.
    const auto topmost = User::CreateWindow("ZOrderSheet", "Topmost", 0x90000000u, 60, 150, 200, 40, 0, 0, 0);
    assert(topmost);
    User::CompleteCreation(topmost);
    User::SetExtendedStyle(topmost, 8);
    Fill(topmost, 200, 40, Gdi::RGB(0, 0, 255));
    check(User::OrderWindow(sheet, 0), "sheet raised below the topmost band");
    count = Order(order, 8);
    check(count == 3 && order[0] == topmost && order[1] == list && order[2] == sheet,
          "topmost band above the owned group");
    User::Render();
    check(Pixel(90, 160) == 0x0000ff, "topmost window paints over the popup");

    // Hiding the owner's control subtree must not lose the popup's position.
    check(User::DestroyWindow(list), "list destroyed");
    count = Order(order, 8);
    check(count == 2 && order[0] == topmost && order[1] == sheet, "group collapses to the owner");
    printf("NATIVE OWNER ZORDER %s failures=%u\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
