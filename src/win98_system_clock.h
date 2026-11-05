// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 GetTickCount reads a millisecond counter advanced by timer
// interrupts. timeGetTime and short waits refresh it before reading or
// scheduling. The default interrupt period is 16 ms; timeBeginPeriod may
// request a shorter period, and timeEndPeriod releases that request. A period
// change takes effect at the next timer reload. Neo98 uses host microseconds
// for the counter and PIT ticks for interrupt phase.
#pragma once

namespace Win98Clock
{
struct SystemClock
{
    static constexpr unsigned long long PitHz = 1193180, TicksPerMs = 0x4a9, FullCount = 0x10000;
    static constexpr unsigned Periods = 0x36;   // the longest period that has a counter
    static constexpr unsigned MinTimeSlice = 16; // Default [386Enh] time slice
    static constexpr unsigned TimeoutUpdateLimit = 0x32; // Short waits refresh time

    unsigned char requests[Periods + 1] = {}; // VTD 100228 - 1 + period
    unsigned long long count = FullCount;     // programmed PIT count (VTD +30h)
    unsigned long long next_tick = 0;         // PIT tick of the next interrupt
    unsigned long long updated = 0;           // host time of the last update, microseconds
    bool started = false;

    static unsigned long long Ticks(unsigned long long us)
    {
        return us / 1000000 * PitHz + us % 1000000 * PitHz / 1000000;
    }
    static unsigned long long Microseconds(unsigned long long ticks)
    {
        return ticks / PitHz * 1000000 + ticks % PitHz * 1000000 / PitHz;
    }
    // Choose the smallest requested period, or 36h when none is requested.
    unsigned MinPeriod() const
    {
        for (unsigned period = 1; period <= Periods; ++period)
            if (requests[period]) return period;
        return Periods;
    }
    // Program the next timer reload.
    unsigned long long ProgrammedCount() const
    {
        const unsigned period = MinPeriod();
        return period == Periods ? FullCount : period * TicksPerMs;
    }
    // Boot: VMM asks for its time slice before any program runs.
    void Start(unsigned long long now)
    {
        if (started) return;
        started = true;
        ++requests[MinTimeSlice];
        count = ProgrammedCount();
        next_tick = Ticks(now) + count;
        updated = now;
    }
    // The timer interrupts that have come since the last look. Each one
    // updates the clock to its own moment; only the last one matters.
    void Interrupts(unsigned long long now)
    {
        Start(now);
        const auto ticks = Ticks(now);
        if (ticks < next_tick) return;
        const auto last = next_tick + (ticks - next_tick) / count * count;
        next_tick = last + count;
        const auto at = Microseconds(last);
        if (at > updated) updated = at;
    }
    // VTD_Update_System_Clock from Get_System_Time or a short timeout.
    void Update(unsigned long long now)
    {
        Interrupts(now);
        if (now > updated) updated = now;
    }
    // The VMM cell: whole milliseconds up to the last update.
    unsigned Cell() const { return unsigned(updated / 1000); }
    // GetTickCount: the cell as the interrupts have left it.
    unsigned TickCount(unsigned long long now)
    {
        Interrupts(now);
        return Cell();
    }
    // Get_System_Time: timeGetTime.
    unsigned SystemTime(unsigned long long now)
    {
        Update(now);
        return Cell();
    }
    // Refresh the clock before scheduling a timeout of 1–49 ms.
    void ArmTimeout(unsigned milliseconds, unsigned long long now)
    {
        if ((int)milliseconds > 0 && milliseconds < TimeoutUpdateLimit) Update(now);
    }
    // Reject periods of zero or above 36h and counters that would wrap. The
    // current interrupt retains its previous count.
    bool Begin(unsigned period, unsigned long long now) { return Change(period, 1, now); }
    bool End(unsigned period, unsigned long long now) { return Change(period, -1, now); }
    bool Change(unsigned period, int delta, unsigned long long now)
    {
        if (!period || period > Periods) return false;
        const unsigned char value = (unsigned char)(requests[period] + delta);
        if (value == (delta > 0 ? 0 : 0xff)) return false;
        Interrupts(now);
        requests[period] = value;
        count = ProgrammedCount();
        return true;
    }
};
} // namespace Win98Clock
