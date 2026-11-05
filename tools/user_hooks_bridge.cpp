// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_hooks.h"
using U = unsigned;
constexpr U Capacity = 64;
struct HookState
{
    Win98Hooks::Record records[Capacity + 1];
    U valid[Capacity + 1], heads[5][14], flags[5], current_hook[5], debug[5];
    U current, disabled;
};
using Service = U (*)(U, U, U, U, U, U);
struct HookEnvironment final : Win98Hooks::Environment
{
    HookState &s;
    Service service;
    HookEnvironment(HookState &state, Service callback) : s(state), service(callback) {}
    bool Valid(U h) const override { return h && h <= Capacity && s.valid[h]; }
    Win98Hooks::Record Read(U h) const override { return s.records[h]; }
    void Write(U h, const Win98Hooks::Record &r) override { s.records[h] = r; }
    U Head(int type, U task) const override { return s.heads[task][type + 1]; }
    void SetHead(int type, U task, U h) override { s.heads[task][type + 1] = h; }
    U CurrentTask() const override { return s.current; }
    U TaskFlags(U task) const override { return s.flags[task]; }
    U CurrentHook(U task) const override { return s.current_hook[task]; }
    void SetCurrentHook(U task, U h) override { s.current_hook[task] = h; }
    bool DebugPresent(U task) const override { return s.debug[task]; }
    U DebugHook(U h, int code, U wp, U lp) override { return service(1, h, 0, code, wp, lp); }
    U Callback(U h, U p, int code, U wp, U lp) override { return service(0, h, p, code, wp, lp); }
    void Dispose(U h) override
    {
        s.valid[h] = 0;
        service(2, h, 0, 0, 0, 0);
    }
    bool HooksDisabled() const override { return s.disabled; }
};
extern "C" U hooks_call(HookState *state, Service service, U operation, U h, U code, U wp, U lp)
{
    HookEnvironment e(*state, service);
    switch (operation)
    {
    case 0:
        return Win98Hooks::First(e, static_cast<int>(h));
    case 1:
        return Win98Hooks::Next(e, h);
    case 2:
        Win98Hooks::Link(e, h);
        return 0;
    case 3:
        return Win98Hooks::Remove(e, h);
    case 4:
        return Win98Hooks::Dispatch(e, h, static_cast<int>(code), wp, lp);
    case 5:
        return Win98Hooks::CallNext(e, h, static_cast<int>(code), wp, lp);
    case 6:
        return Win98Hooks::Filter(e, h, static_cast<int>(code));
    default:
        return 0;
    }
}
