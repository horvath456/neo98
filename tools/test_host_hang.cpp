// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_hang.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main()
{
    using namespace HostHang;
    auto at = [](unsigned ms) { return Clock::time_point{} + std::chrono::milliseconds(ms); };
    auto since = at(0), call = at(10), input = at(0);
    bool was_waiting = false;
    auto observe = [&](unsigned ms, bool runnable) {
        const bool pending_input = input > call;
        Observe(since, was_waiting, at(ms), call, input, pending_input, pending_input || runnable);
        return (pending_input || runnable) && at(ms) - since >= std::chrono::seconds(3);
    };

    assert(!observe(20, true));
    // Work becomes idle without another counted API call; repeated timer
    // wake-ups after a long idle period must not flash the hang indicator.
    for (unsigned wake : {10000u, 20000u, 30000u})
    {
        assert(!observe(wake - 1, false));
        assert(!observe(wake, true));
        assert(!observe(wake + 100, true));
    }
    // A real, continuously runnable loop still reaches the original threshold.
    assert(!observe(32999, true));
    assert(observe(33000, true));
    call = at(33001);
    assert(!observe(33001, true));
    assert(!observe(34000, false));
    // An unanswered input is timed from delivery, even if no thread runs.
    input = at(40000);
    assert(!observe(40100, false));
    assert(!observe(42999, false));
    assert(observe(43000, false));
    call = at(43001);
    assert(!observe(43001, false));
    assert(!observe(90000, false));
    assert(!observe(90001, true));
    puts("HOST HANG PASS: idle wake-ups, continuous stall, pending input, recovery");
}
