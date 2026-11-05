// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>

namespace HostHang
{
using Clock = std::chrono::steady_clock;

// A hang needs uninterrupted pending work without progress. Time spent idle
// must not count when a timer wakes a guest thread for the next host turn.
inline void Observe(Clock::time_point &waiting_since, bool &was_waiting, Clock::time_point now,
                    Clock::time_point last_call, Clock::time_point last_input,
                    bool input_waiting, bool waiting)
{
    if (!waiting)
        waiting_since = now;
    else if (!was_waiting || waiting_since <= last_call)
        waiting_since = input_waiting && last_input < now ? last_input : now;
    was_waiting = waiting;
}
}
