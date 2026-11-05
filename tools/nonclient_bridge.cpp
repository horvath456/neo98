// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_nonclient.h"
using U = unsigned;
struct State
{
    unsigned short words[0x900];
    Win98Nonclient::WindowState windows[9];
    Win98Nonclient::Rect rectangle;
    U version, menus[9];
};
using Service = U (*)(U, U, U, U, U, U);
struct NativeEnvironment final : Win98Nonclient::Environment
{
    State &s;
    Service service;
    NativeEnvironment(State &state, Service callback) : s(state), service(callback) {}
    unsigned short Word(U offset) const override { return s.words[offset / 2]; }
    unsigned short ExpectedVersion() const override { return service(4, 0, 0, 0, 0, 0); }
    Win98Nonclient::WindowState ReadWindow(U h) const override { return s.windows[h]; }
    void WriteWindow(U h, const Win98Nonclient::WindowState &w) override { s.windows[h] = w; }
    bool MenuValid(U menu) override { return service(1, menu, 0, 0, 0, 0); }
    int MenuHeight(U menu, U h, int width, int border, int top) override
    {
        return service(2, menu, h, width, border, top);
    }
    void NotifyReorder(U h) override { service(3, h, 0, 0, 0, 0); }
};
extern "C" U nonclient_call(State *state, Service service, U operation, U a, U b, U c, U d)
{
    NativeEnvironment e(*state, service);
    switch (operation)
    {
    case 0:
        return Win98Nonclient::BorderUnits(e, a, b, c, d);
    case 1:
        return Win98Nonclient::HasWindowEdge(a, b, c);
    case 2:
        return Win98Nonclient::Adjust(e, state->rectangle, a, b, c);
    case 3:
        Win98Nonclient::Calculate(e, a, state->rectangle, b);
        return 0;
    default:
        return 0;
    }
}
