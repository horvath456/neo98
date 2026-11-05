// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_user_heap.h"
using U = unsigned;
using Service = U (*)(U, U, U, U, U);
struct UserBridge final : Win98UserSeeUserDo::Environment
{
    Service service;
    explicit UserBridge(Service s) : service(s) {}
    U LocalAllocate(U flags, U size) override { return service(0, flags, size, 0, 0); }
    U LocalRelease(U handle) override { return service(1, handle, 0, 0, 0); }
    U LocalCompact(U size) override { return service(2, size, 0, 0, 0); }
    U DataWord(U offset) override { return service(3, offset, 0, 0, 0); }
    U DataPair(U offset) override { return service(4, offset, 0, 0, 0); }
    U DataSelector() override { return service(5, 0, 0, 0, 0); }
    U FarCall(U slot, U value, U zeros) override { return service(6, slot, value, zeros, 0); }
    U MenuCall(U value, U word, U mask) override { return service(7, value, word, mask, 0); }
};
struct ShellBridge final : Win98ShellUserHeap::Environment
{
    Service service;
    explicit ShellBridge(Service s) : service(s) {}
    U SeeUserDo(U request, U first, U second, U third) override
    {
        UserBridge user(service);
        return Win98UserSeeUserDo::Call(user, request, first, second, third);
    }
};
extern "C" U user_see_user_do(Service service, U request, U first, U second, U third)
{
    UserBridge user(service);
    return Win98UserSeeUserDo::Call(user, request, first, second, third);
}
extern "C" U shell_user_heap_probe(Service service)
{
    ShellBridge shell(service);
    return Win98ShellUserHeap::Probe(shell);
}
