// SPDX-License-Identifier: GPL-2.0-or-later
// The WINMM timer family: timeBeginPeriod, timeEndPeriod, timeGetDevCaps,
// timeSetEvent and timeKillEvent.
// In Windows 98 these five entries are ring-3 argument checks in front of
// DeviceIoControl on the VTDAPI handle that WINMM keeps open. The
// control codes are 1 for the capabilities, 2 for BeginPeriod, 3 for
// EndPeriod, 5 for the millisecond clock, 6 for SetEvent and 7 for KillEvent.
// The scheduling itself is the timer driver in ring 0; the part that is
// readable in ring 3, and the part a program sees, is all here:
// timeGetDevCaps rejects a size below eight with 11 and a buffer
// IsBadWritePtr answers for with 129, then copies exactly
// two dwords out of the twelve the driver returns.
// timeBeginPeriod and timeEndPeriod hand the period
// through unchecked and return the driver's dword.
// timeSetEvent rejects every flag outside 0F1h with a zero
// identifier. Without a callback type it builds a 22h byte
// LocalAlloc record with the magic 80808080h at +16h, the
// word delay at +0, the word resolution at +2, the flags at
// +0Ch, dwUser at +1Ah and lpTimeProc at +1Eh, and the
// record address is the identifier. One timer thread per
// process runs the callbacks. WINMM keeps a list keyed by
// process id, creates the thread with priority
// 15 and waits for it to publish its thread id. That thread
// sleeps alertably and the driver queues the trampoline
// to it, which calls lpTimeProc(id, 0, dwUser, 0, 0)
// and frees a one shot record before the call. With a
// callback type the record stays on the caller's stack, the
// driver signals the handle itself and the identifier is the
// driver's small number, whose high word is zero.
// timeKillEvent splits on the high word of the identifier: zero
// is a driver number, anything else is a record, probed with
// IsBadWritePtr for 22h bytes and checked for the magic. The
// ring-3 answer for a record it does not recognise is 11. On
// success it clears the magic and frees the record.
//
#include "sound.h"
#include "x86_app.h"
#include "win98_system_clock.h"
#include <string.h>
namespace X86
{
namespace
{
// The entry address of the per-process timer thread. Application::Run sends a
// thread that starts here into RunMultimediaTimerThread instead of the guest.
constexpr unsigned TimerThreadEntry = 0x71000f20;
// VTDAPI is not in the imported tree; it lives inside VMM32.VXD. These are the
// bounds the timer driver accepts.
constexpr unsigned PeriodMinimum = 1, PeriodMaximum = 1000;
constexpr unsigned RecordBytes = 0x22, RecordMagic = 0x80808080;
// One armed multimedia timer. `record` is the guest block a callback timer is
// named by; an event timer has no block and is named by `number`.
struct MultimediaTimer
{
    Application *process = nullptr;
    unsigned record = 0, number = 0, procedure = 0, user = 0, flags = 0;
    unsigned delay = 0, resolution = 0;
    unsigned long long due = 0;
};
MultimediaTimer armed[64];
unsigned next_number = 0;
// The per-process timer thread.
struct TimerProcess
{
    Application *process = nullptr;
    unsigned thread = 0;
};
TimerProcess processes[16];
TimerProcess *ProcessOf(Application *app, bool create)
{
    TimerProcess *empty = nullptr;
    for (auto &entry : processes)
    {
        if (entry.process == app) return &entry;
        if (!entry.process && !empty) empty = &entry;
    }
    if (!create || !empty) return nullptr;
    *empty = TimerProcess{};
    empty->process = app;
    return empty;
}
} // namespace
// The one multimedia thread a process gets.  creates it at the first
// timer; this port also creates it at the first waveOutOpen, because Windows
// delivers the wave callbacks on a thread of WINMM too and this port has only
// this one. EnsureMultimediaThread is what both entries call.
void Application::EnsureMultimediaThread()
{
    auto *owner = ProcessOf(this, true);
    if (!owner || owner->thread) return;
    const unsigned creation[] = {0, 0, TimerThreadEntry, 0, 0, 0};
    owner->thread = CreateThread(creation);
    // WINMM gives its timer thread THREAD_PRIORITY_TIME_CRITICAL.
    // The scheduler of this port picks strictly by priority, so at normal
    // priority the thread never ran beside a program that raised its own
    // and never waits: Space Cadet sets THREAD_PRIORITY_HIGHEST and polls
    // PeekMessage. No wave block then got its WHDR_DONE, WaveMix never
    // refilled its three blocks and every sound stopped after a second.
    if (owner->thread)
    {
        const unsigned priority[] = {owner->thread, 15};
        Api("SetThreadPriority", priority);
    }
}
// The timer thread routine and callback trampoline. The
// original sleeps alertably and the driver queues one call per expiry. This
// port owns the clock, so the wait is the next due time of this process, and
// the same thread also signals the event timers the driver would signal
// itself; nothing else in the port runs while a guest thread is between two
// instructions.
// The same thread carries the sound. Without a host audio device the mixer has
// no clock of its own, so this is where it gets one, and the WOM_DONE of every
// finished wave block is delivered from here, which is the thread Windows
// delivers it on as well.
void Application::RunMultimediaTimerThread()
{
    while (state != Failed && state != Exited && !threads[current_thread].done)
    {
        Sound::Shared().Advance(Now());
        PumpWaveCallbacks();
        PumpMidiCallbacks();
        PumpDMusic16();
        PumpMciDevices();
        unsigned long long next = 0;
        MultimediaTimer *ready = nullptr;
        const auto now = Now();
        for (auto &timer : armed)
        {
            if (timer.process != this) continue;
            if (timer.due <= now) { ready = &timer; break; }
            if (!next || timer.due < next) next = timer.due;
        }
        if (ready)
        {
            const unsigned identifier = ready->record, procedure = ready->procedure;
            const unsigned user = ready->user, type = ready->flags & 0xf0;
            if (ready->flags & 1) ready->due = now + (unsigned long long)ready->delay * 1000;
            else if (identifier)
            {
                // a one shot loses its magic and its block before
                // the callback runs, so the callback may arm the next timer.
                Write32(identifier + 0x16, 0);
                Free(identifier);
                *ready = MultimediaTimer{};
            }
            else *ready = MultimediaTimer{};
            if (!type)
            {
                const unsigned arguments[] = {identifier, 0, user, 0, 0};
                Call(procedure, arguments, 5);
            }
            else if (type == 0x10) ThreadApi("SetEvent", &procedure);
            else KernelAsyncApi("PulseEvent", &procedure);
            continue;
        }
        auto &thread = threads[current_thread];
        thread.block = 3;
        // Without an armed timer the thread still wakes regularly, so a timer
        // armed by another thread starts on time. An open wave device shortens
        // the wait, because a block that has played waits for its WOM_DONE.
        const unsigned long long idle = WaveOutActive() || WaveInActive() || MidiActive() || MciActive() || DMusic16Active() ? 5000 : 20000;
        thread.deadline = next && next < now + idle ? next : now + idle;
        Suspend(Idle);
    }
}
// A process drops its timers and its thread record. A timeBeginPeriod it did
// not end stays in force, as in VTD.
void Application::FreeMultimediaTimers()
{
    for (auto &timer : armed)
        if (timer.process == this) timer = MultimediaTimer{};
    if (auto *owner = ProcessOf(this, false)) *owner = TimerProcess{};
}
unsigned Application::MultimediaTimerApi(ApiName name, const unsigned *a)
{
#define API(n) case API_ID(#n):
    switch (name.id)
    {
    // The size test comes first, then the write probe, and only two
    // of the driver's three dwords reach the caller.
    API(timeGetDevCaps)
    {
        if (a[1] < 8) return 11;                                          // MMSYSERR_INVALPARAM
        if (!a[0] || !virtual_memory.Access(a[0], a[1], true)) return 129; // TIMERR_STRUCT
        Write32(a[0], PeriodMinimum);
        Write32(a[0] + 4, PeriodMaximum);
        return 0;
    }
    // timeBeginPeriod and timeEndPeriod maintain system-wide period counts.
    // Values below one fail; periods of 54 ms or more succeed without a
    // request. Counter underflow or overflow fails. Process exit does not
    // release period requests.
    API(timeBeginPeriod) API(timeEndPeriod)
    {
        if (a[0] < PeriodMinimum) return 97;
        if (a[0] >= Win98Clock::SystemClock::Periods) return 0;
        return ChangeMinimumPeriod(a[0], API_IS(name, "timeBeginPeriod")) ? 0 : 97;
    }
    // Flags outside 0F1h, a failed thread and a failed record all
    // answer with the identifier zero; the original sets no last error here.
    API(timeSetEvent)
    {
        if (a[4] & ~0xf1u) return 0;
        const unsigned type = a[4] & 0xf0;
        // Only the two documented callback types have a meaning in ring 3.
        if (type && type != 0x10 && type != 0x20) return 0;
        if (!a[2]) return 0;
        //  and keep the delay and the resolution as words.
        const unsigned delay = a[0] & 0xffff, resolution = a[1] & 0xffff;
        MultimediaTimer *slot = nullptr;
        for (auto &timer : armed)
            if (!timer.process) { slot = &timer; break; }
        if (!slot) return 0;
        auto *owner = ProcessOf(this, true);
        if (!owner) return 0;
        EnsureMultimediaThread();
        if (!owner->thread) return 0;
        unsigned record = 0;
        if (!type)
        {
            record = Allocate(RecordBytes);
            if (!record) return 0;
            unsigned char block[RecordBytes] = {};
            const unsigned stored = a[4] | 0x10;
            block[0] = (unsigned char)delay;
            block[1] = (unsigned char)(delay >> 8);
            block[2] = (unsigned char)resolution;
            block[3] = (unsigned char)(resolution >> 8);
            // +4 is the driver trampoline and +8 its argument in the original.
            // This port calls the procedure itself, so the pair stays zero.
            block[0x0c] = (unsigned char)stored;
            block[0x0d] = (unsigned char)(stored >> 8);
            memcpy(block + 0x16, "\x80\x80\x80\x80", 4);
            memcpy(block + 0x1a, &a[3], 4);
            memcpy(block + 0x1e, &a[2], 4);
            if (!Store(record, block, sizeof block)) { Free(record); return 0; }
        }
        *slot = MultimediaTimer{};
        slot->process = this;
        slot->record = record;
        slot->number = record ? 0 : (next_number = next_number % 0xffff + 1);
        slot->procedure = a[2];
        slot->user = a[3];
        slot->flags = a[4];
        slot->delay = delay ? delay : 1;
        slot->resolution = resolution;
        slot->due = Now() + (unsigned long long)slot->delay * 1000;
        return record ? record : slot->number;
    }
    // The high word of the identifier picks the path.
    API(timeKillEvent)
    {
        if (!a[0]) return 11;
        if (a[0] >> 16)
        {
            if (!virtual_memory.Access(a[0], RecordBytes, true) ||
                Read32(a[0] + 0x16) != RecordMagic) return 11;
        }
        for (auto &timer : armed)
        {
            if (timer.process != this) continue;
            if (a[0] >> 16 ? timer.record != a[0] : timer.number != a[0]) continue;
            if (timer.record)
            {
                Write32(timer.record + 0x16, 0);
                Free(timer.record);
            }
            timer = MultimediaTimer{};
            return 0;
        }
        return 11;
    }
    default:
        break;
    }
    Fail(name);
    return 0;
#undef API
}
} // namespace X86
