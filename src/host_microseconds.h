// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#if defined(__APPLE__)
#include <mach/mach_time.h>
#include <time.h>
namespace HostClock
{
// Reduce the conversion to one division when a microsecond is a whole
// number of ticks. Other timebases keep the system's conversion routine.
constexpr unsigned long long TickDivisor(unsigned numerator, unsigned denominator)
{
    const auto scaled = (unsigned long long)denominator * 1000;
    return numerator && scaled % numerator == 0 ? scaled / numerator : 0;
}
inline unsigned long long Microseconds()
{
    static const unsigned long long divisor = [] {
        mach_timebase_info_data_t timebase{};
        if (mach_timebase_info(&timebase) != KERN_SUCCESS) return 0ull;
        return TickDivisor(timebase.numer, timebase.denom);
    }();
    // This is the same clock as CLOCK_MONOTONIC_RAW, including host sleep.
    // Converting straight to microseconds avoids the nanosecond intermediate.
    return divisor ? mach_continuous_time() / divisor : clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) / 1000;
}
}
#endif
