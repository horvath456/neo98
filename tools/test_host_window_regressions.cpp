// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_seamless.h"
#include <cassert>
#include <cstdio>

static unsigned pixel(int x, int y)
{
    return reinterpret_cast<const unsigned *>(User::ScreenBits() + y * User::ScreenStride())[x] & 0xffffffu;
}

static void fill(unsigned window, unsigned brush)
{
    const auto dc = User::GetDC(window);
    assert(dc);
    Gdi::RECT client{};
    User::GetClientRect(window, &client);
    Gdi::FillRect(dc, &client, brush);
    User::DrawingChanged(dc);
    assert(User::ReleaseDC(window, dc));
}

int main()
{
    assert(User::Initialize());
    const auto red = Gdi::CreateSolidBrush(0x0000ff), blue = Gdi::CreateSolidBrush(0xff0000);
    assert(User::RegisterClass("Game", 0, 0, red));
    assert(User::RegisterClass("Shell_TrayWnd", 0, 0, blue));
    const auto game = User::CreateWindow("Game", "game", 0x90000000u, 100, 450, 300, 130, 0, 0, 0);
    const auto tray = User::CreateWindow("Shell_TrayWnd", "tray", 0x90000000u, 0, 570, 800, 30, 0, 0, 0);
    assert(game && tray);
    User::Render();
    fill(tray, blue);
    fill(game, red);
    assert(pixel(150, 575) == 0x0000ff); // visible taskbar clips game drawing
    User::SetHostHiddenWindow(tray);
    User::Render();
    fill(game, red);
    assert(User::IsVisible(tray)); // Explorer's logical state is preserved
    assert(pixel(150, 575) == 0xff0000); // omitted taskbar no longer cuts the game
    Gdi::RECT rects[8]{};
    assert(User::WindowVisibleRects(tray, rects, 8) == 0);

    // A Mac window keeps its dimensions when the guest window leaves the
    // screen. Only the on-screen portion of its image is opaque.
    assert(User::MoveWindow(game, -20, 520, 300, 130));
    User::Render();
    fill(game, red);
    HostSeamless::Mask mask;
    HostSeamless::View view;
    view.grace = 0;
    HostSeamless::Collect(mask, view);
    std::vector<HostSeamless::Item> items;
    HostSeamless::Plan(mask, items);
    bool found = false;
    for (const auto &item : items) if (item.id == game)
    {
        found = true;
        assert(item.frame.left == -20 && item.frame.bottom == 650);
        std::vector<unsigned> picture;
        HostSeamless::ComposeItem(item, User::Pixels(), 800, 600, picture);
        assert(picture.size() == 300 * 130);
        assert(picture[0] == 0 && picture[129 * 300 + 100] == 0);
    }
    assert(found);

    User::SetHostHiddenWindow(0);
    assert(User::MoveWindow(game, 100, 450, 300, 130));
    User::Render();
    fill(tray, blue);
    // Keep a game DC open across repeated down/up moves, as render loops do.
    const auto dc = User::GetDC(game);
    assert(dc);
    for (int i = 0; i < 4; ++i)
    {
        assert(User::MoveWindow(game, 100, 520, 300, 130));
        User::Render();
        Gdi::RECT client{0, 0, 300, 130};
        Gdi::FillRect(dc, &client, red);
        User::DrawingChanged(dc);
        assert(pixel(150, 575) == 0x0000ff);
        assert(User::MoveWindow(game, 100, 300, 300, 130));
        User::Render();
        Gdi::FillRect(dc, &client, red);
        User::DrawingChanged(dc);
        assert(pixel(150, 575) == 0x0000ff);
    }
    assert(User::ReleaseDC(game, dc));
    User::Shutdown();
    Gdi::DeleteObject(red);
    Gdi::DeleteObject(blue);
    puts("HOST WINDOW REGRESSIONS PASS");
}
