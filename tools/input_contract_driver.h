// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../src/x86_app.h"
// Used by both the ASan host runner and the AArch64 boot self-test.
inline bool ContractClientToScreen(unsigned h, Gdi::POINT &point)
{
    // Native USER exposes ScreenToClient. Invert its client-origin translation.
    Gdi::POINT origin = {0, 0};
    if (!h || !User::ScreenToClient(h, &origin))
        return false;
    point.x -= origin.x;
    point.y -= origin.y;
    return true;
}

inline bool WindowContractPoints(Gdi::POINT &button, Gdi::POINT &background)
{
    const auto front = User::FindWindow("NEO98WindowContract", "Front window");
    button = {36, 12};
    background = {176, 53};
    return front && ContractClientToScreen(User::GetDlgItem(front, 201), button) &&
           ContractClientToScreen(front, background);
}

inline bool InputContractDriver(X86::Application &app)
{
    const auto h = User::FindWindow("NEO98InputContract", "Keyboard queue contract");
    Gdi::POINT point = {46, 23};
    if (!h || !app.WaitingForInput() || !ContractClientToScreen(h, point))
        return false;
    // Put nonmatching button transitions before the key. A filtered peek
    // must continue routing until it reaches the queued keyboard message.
    app.MouseButtons(point.x, point.y, 2);
    app.MouseButtons(point.x, point.y, 0);
    app.Key('A', 'A', true, false);
    app.KeyUp('A');
    app.Key(0x14, 0, false, false);
    app.Key(0x14, 0, false, false);
    app.KeyUp(0x14);
    app.Key(0x14, 0, false, false);
    app.KeyUp(0x14);
    app.KeyboardModifiers(0x22);
    app.KeyboardModifiers(0x20);
    app.KeyboardModifiers(0);
    User::PostMessage(h, 0x8001, 0, 0);
    return true;
}

inline bool MouseFocusContractDriver(X86::Application &app)
{
    if (!app.WaitingForInput())
        return false;
    const char *titles[] = {"Mouse focus contract 1", "Mouse focus contract 2", "Mouse focus contract 3"};
    for (unsigned phase = 0; phase < 3; ++phase)
    {
        const auto h = User::FindWindow("NEO98MouseFocusContract", titles[phase]);
        if (!h)
            continue;
        const auto target = phase == 2
                                ? User::FindWindow("NEO98MouseFocusContract", "Mouse activation target")
                                : User::GetDlgItem(h, 98);
        Gdi::POINT point = phase == 2 ? Gdi::POINT{26, 23} : Gdi::POINT{16, 13};
        if (!ContractClientToScreen(target, point))
            return false;
        User::SetWindowText(h, "Mouse focus input supplied");
        const int x = point.x, y = point.y;
        // Motion coalescing must leave room for all button/key transitions.
        for (unsigned n = 0; n < 400; ++n)
            app.MouseButtons(x + int(n & 1), y, 0);
        app.MouseButtons(x, y, 0);
        app.MouseButtons(x, y, 0);
        app.MouseButtons(x, y, 1);
        app.MouseButtons(x, y, 0);
        const auto key = phase == 2 ? 'S' : 'Q';
        app.Key(key, key, false, false);
        app.KeyUp(key);
        User::PostMessage(h, 0x8001, 0, 0);
        return true;
    }
    return false;
}
