// SPDX-License-Identifier: GPL-2.0-or-later
// Bridge for tools/test_shell_pif.py: drives the implemented SHELL.DLL
// PIF-property forwarders through one service callback so the Python oracle can
// compare results, shared state and call traces against the original 16-bit
// code under Unicorn.
#include "../src/win98_shell_pif.h"
using U = unsigned;
// (operation, a, b) -> result. Operations match test_shell_pif.py.
using Service = U (*)(U, U, U);

namespace
{
struct Bridge final : Win98ShellPif::Environment
{
    Service service;
    explicit Bridge(Service s) : service(s) {}
    U LoadModule(const char *) override { return service(0, 0, 0); }
    U Procedure(U instance, U ordinal) override { return service(1, instance, ordinal); }
    void FreeModule(U instance) override { service(2, instance, 0); }
    U CallProcedure(U procedure, U bytes) override { return service(3, procedure, bytes); }
    U StaleResult() override { return service(4, 0, 0); }
};
Win98ShellPif::State &Shared()
{
    static Win98ShellPif::State state;
    return state;
}
} // namespace

// The six shared words are set and read directly, the same way the oracle sets
// and reads them in SHELL's own data segment, so both sides start a comparison
// from the identical state.
extern "C" void shell_pif_reset(const U *in)
{
    auto &s = Shared();
    s.instance = in[0];
    s.handles = in[1];
    s.open = in[2];
    s.get = in[3];
    s.set = in[4];
    s.close = in[5];
}
extern "C" void shell_pif_state(U *out)
{
    const auto &s = Shared();
    out[0] = s.instance;
    out[1] = s.handles;
    out[2] = s.open;
    out[3] = s.get;
    out[4] = s.set;
    out[5] = s.close;
}
extern "C" U shell_pif_load(Service service)
{
    Bridge bridge(service);
    return Win98ShellPif::Load(Shared(), bridge);
}
extern "C" void shell_pif_unload(Service service)
{
    Bridge bridge(service);
    Win98ShellPif::Unload(Shared(), bridge);
}
extern "C" U shell_pif_open(Service service)
{
    Bridge bridge(service);
    return Win98ShellPif::Open(Shared(), bridge);
}
extern "C" U shell_pif_get(Service service)
{
    Bridge bridge(service);
    return Win98ShellPif::Get(Shared(), bridge);
}
extern "C" U shell_pif_set(Service service)
{
    Bridge bridge(service);
    return Win98ShellPif::Set(Shared(), bridge);
}
extern "C" U shell_pif_close(Service service, U handle)
{
    Bridge bridge(service);
    return Win98ShellPif::Close(Shared(), bridge, handle);
}
