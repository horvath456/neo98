// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include "../src/user.cpp"

namespace {
unsigned victim, replacement, shell_callbacks;

void Replace(unsigned hwnd)
{
    assert(hwnd == victim);
    assert(User::DestroyWindow(hwnd));
    assert(!User::IsWindow(hwnd));
    replacement = User::CreateWindow("STATIC", "replacement", 0x80000000,
                                     1, 2, 30, 20, 0, 0, 0, 1);
    assert(replacement && replacement != victim && User::IsWindow(replacement));
}

void Shell(void *, unsigned code, unsigned hwnd, unsigned)
{
    if (code == 2) { ++shell_callbacks; Replace(hwnd); }
}

unsigned Make()
{
    const unsigned hwnd = User::CreateWindow("STATIC", "victim",
        0x90000000, 4, 5, 60, 25, 0, 0, 0, 1);
    assert(hwnd);
    User::CompleteCreation(hwnd);
    return hwnd;
}
}

int main()
{
    assert(User::Initialize());

    User::SetShellEvents(nullptr, Shell);
    victim = Make();
    assert(User::DestroyWindow(victim));
    assert(shell_callbacks == 1 && User::IsWindow(replacement));
    assert(User::LiveWindows() == 1);
    assert(User::DestroyWindow(replacement));
    User::SetShellEvents(nullptr, nullptr);

    User::Shutdown();
    assert(!User::LiveWindows() && !Gdi::LiveObjects());
    std::puts("WINDOW DESTROY REENTRANCY PASS: shell callback preserves replacement slot");
}
