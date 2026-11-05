// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell.h"
using U = unsigned;
using Service = U (*)(U, U, U, U, U);
struct Bridge final : Win98Shell::Environment
{
    Service service;
    explicit Bridge(Service s) : service(s) {}
    U RegisterMessage(U kind) override { return service(0, kind, 0, 0, 0); }
    U Install() override { return service(1, 0, 0, 0, 0); }
    void Unhook(U h) override { service(2, h, 0, 0, 0); }
    bool IsWindow(U h) override { return service(3, h, 0, 0, 0); }
    bool Post(U h, U m, U w, U l) override { return service(4, h, m, w, l); }
    U Send(U h, U m, U w, U l) override { return service(5, h, m, w, l); }
    U Next(U h, U c, U w, U l) override { return service(6, h, c, w, l); }
    U AllocateInfo() override { return service(7, 0, 0, 0, 0); }
    U PrepareInfo(U a, U h) override { return service(8, a, h, 0, 0); }
    void CopyInfo(U d, U a) override { service(9, d, a, 0, 0); }
    void FreeInfo(U a) override { service(10, a, 0, 0, 0); }
};
static_assert(sizeof(Win98Shell::State) == 80, "oracle state layout");
extern "C" U shell_call(Win98Shell::State *s, Service service, U operation, U a, U b, U c)
{
    Bridge e(service);
    if (operation == 0)
        return Win98Shell::Register(*s, e, a, b);
    if (operation == 1)
        return Win98Shell::Hook(*s, e, a, b, c);
    return Win98Shell::Send(*s, e, a, b, c);
}
