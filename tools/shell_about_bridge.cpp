// SPDX-License-Identifier: GPL-2.0-or-later
// Bridge for tools/test_shell_about_original.py: drives the implemented
// SHELL.DLL resource query behind flat thunk index 7 and the USER routine it
// calls, so the Python oracle can compare them against the original 16-bit code
// under Unicorn.
#include "../src/win98_shell_about.h"
using U = unsigned;
// (operation, a, b) -> result. 0 asks for the GDI per cent, 1 for the USER per
// cent, 2 for GetFreeSpace. 3 and 4 record a word and a dword write.
using Service = U (*)(U, U, U);

namespace
{
struct Bridge final : Win98ShellAbout::Environment
{
    Service service;
    explicit Bridge(Service s) : service(s) {}
    U GdiFreePercent() override { return service(0, 0, 0); }
    U UserFreePercent() override { return service(1, 0, 0); }
    U FreeSpace() override { return service(2, 0, 0); }
    void WriteWord(U at, U value) override { service(3, at, value); }
    void WriteDword(U at, U value) override { service(4, at, value); }
};
} // namespace

extern "C" U shell_about_percent(U used, U total)
{
    return Win98ShellAbout::Percent(used, total);
}
extern "C" U shell_about_free_system_resources(U flag, U gdi, U user)
{
    return Win98ShellAbout::FreeSystemResources(flag, gdi, user);
}
extern "C" void shell_about_resources(Service service, U resources, U space)
{
    Bridge bridge(service);
    Win98ShellAbout::Resources(bridge, resources, space);
}
