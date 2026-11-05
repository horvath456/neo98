// SPDX-License-Identifier: GPL-2.0-or-later
// Original KERNEL32, VWIN32 and VMM thread-control algorithms.
#pragma once
namespace Win98ThreadControl
{
using U = unsigned;
inline bool Negative(U value) { return (value & 0x80000000u) != 0; }
inline bool SignedLess(U a, U b) { return (a ^ 0x80000000u) < (b ^ 0x80000000u); }
struct SuspendState { U count = 0, event = 0, flags = 0; };
struct PriorityState
{
    U base = 8, effective = 8, inherited = 0, boost = 0, decay = 0, adjustment = 0;
};
// Event-allocation failure leaves the internal suspend count incremented
// while the public KERNEL32 count remains unchanged.
template<class E> U SuspendVm(E &e, U thread, SuspendState &s)
{
    const U old = s.count;
    if (!SignedLess(old, 127)) return ~0u;
    ++s.count;
    if (SignedLess(0, old)) return old;
    const U flags = s.flags;
    s.flags &= ~8u;
    if (flags & 8) return old;
    const bool pending = (s.flags & 4) != 0;
    s.flags |= 4;
    if (pending) return old;
    const U event = e.ScheduleSuspend(thread);
    if (event) { s.event = event; return old; }
    s.flags &= ~4u;
    return ~0u;
}
// Set carry only when a pending event was cancelled.
template<class E> bool CancelSuspend(E &e, U thread, SuspendState &s)
{
    const bool pending = (s.flags & 4) != 0;
    s.flags &= ~4u;
    if (!pending) return false;
    const U event = s.event;
    s.event = 0;
    e.CancelSuspend(thread, event);
    return true;
}
// Resuming a zero internal suspend count does nothing.
template<class E> U ResumeVm(E &e, U thread, SuspendState &s)
{
    const U old = s.count;
    if (!old) return old;
    if (--s.count) return old;
    if (!CancelSuspend(e, thread, s))
    {
        s.flags |= 8;
        e.WakeSuspend(thread);
    }
    return old;
}
// WaitSuspend is a scheduler/semaphore provider and may yield. Check the wake
// bit after every resumption.
template<class E> void SuspendCallback(E &e, U thread, SuspendState &s)
{
    const bool pending = (s.flags & 4) != 0;
    s.flags &= ~4u;
    if (!pending || !s.count) return;
    for (;;)
    {
        if (!e.WaitSuspend(thread)) return; // Native process-abort boundary.
        const bool woke = (s.flags & 8) != 0;
        s.flags &= ~8u;
        if (woke) return;
    }
}
// KERNEL32..F73 /..FAC, the complete runtime-state guard.
template<class E> U ChangeRuntimeSuspend(E &e, U thread, bool suspend)
{
    U result = ~0u;
    if (!(e.ThreadFlags(thread) & 0x10000000u) && e.RuntimeThread(thread))
        result = suspend ? e.SuspendRuntime(thread) : e.ResumeRuntime(thread);
    if (result == ~0u) e.Error(259);
    return result;
}

template<class E> U Suspend(E &e, U handle)
{
    e.EnterThreads();
    const U thread = e.Lookup(handle, 0x40);
    U result = ~0u;
    if (thread && ((e.ThreadFlags(thread) & 0x40) || ChangeRuntimeSuspend(e, thread, true) != ~0u))
    {
        result = e.SuspendCount(thread);
        e.SetSuspendCount(thread, result + 1);
    }
    e.LeaveThreads();
    return result;
}
// A public resume at zero returns FFFFFFFF without setting error.
template<class E> U Resume(E &e, U handle)
{
    e.EnterThreads();
    const U thread = e.Lookup(handle, 0x40);
    U result = ~0u;
    if (thread && e.SuspendCount(thread) &&
        ((e.ThreadFlags(thread) & 0x40) || ChangeRuntimeSuspend(e, thread, false) != ~0u))
    {
        result = e.SuspendCount(thread);
        e.SetSuspendCount(thread, result - 1);
    }
    e.LeaveThreads();
    return result;
}
// Relative signed priority is clamped within the process class.
inline U BasePriority(U relative, U process_base)
{
    U result = relative + process_base;
    const U maximum = process_base == 24 ? 31 : 15, minimum = process_base == 24 ? 16 : 1;
    if (SignedLess(maximum, result)) result = maximum;
    if (SignedLess(result, minimum)) result = minimum;
    return result;
}
inline U SignMask(U value) { return Negative(value) ? ~0u : 0; }
inline U MinWrap(U a, U b) { return ((a-b) & SignMask(a-b)) + b; }
inline U MaxWrap(U a, U b) { return ((a-b) & ~SignMask(a-b)) + b; }
// Queue updates and priority-inheritance lists are native providers.
// Calculate and publish effective priority here.
template<class E> U RecomputePriority(E &e, U thread, PriorityState &s)
{
    U value = Negative(s.boost) ? MinWrap(s.boost, s.adjustment) : MaxWrap(s.boost, s.adjustment);
    value += s.base;
    if (Negative(value)) value = 0;
    value = MaxWrap(MinWrap(value, 31), s.inherited);
    if (value != s.effective)
    {
        s.effective = value;
        e.RequeuePriority(thread, value);
        e.PropagatePriority(thread, value);
        e.ReschedulePriority();
    }
    return value;
}
// Clear temporary priority boost and decay.
template<class E> U SetBasePriority(E &e, U thread, PriorityState &s, U value, bool deferred = false)
{
    if (value > 31) return value; // Original carry-set result; caller checks validity.
    s.base = value; s.boost = 0; s.decay = 0;
    if (deferred) e.DeferPriority(true);
    RecomputePriority(e, thread, s);
    if (deferred) e.DeferPriority(false);
    return 0xffffffe0u;
}
// Priority boosts expire through scheduler decay. Their maximum depends on
// the VM scheduler class.
template<class E> void BoostPriority(E &e, U thread, PriorityState &s, U value, U decay, U maximum)
{
    if (!Negative(value))
    {
        if (!SignedLess(s.boost, value)) return;
        value = MinWrap(value + s.base, maximum) - s.base;
        if (!value || Negative(value)) return;
        s.decay = MaxWrap(s.decay, decay);
    }
    else
    {
        if (!SignedLess(value, s.boost)) return;
        value = 0u - MinWrap(0u - value, s.base);
        s.decay = MinWrap(s.decay, decay);
    }
    s.boost = value;
    RecomputePriority(e, thread, s);
}
// Apply signed priority decay at a quantum boundary.
template<class E> void DecayPriority(E &e, U thread, PriorityState &s)
{
    if (!s.boost) return;
    s.boost -= Negative(s.boost) ? MaxWrap(s.decay, s.boost) : MinWrap(s.decay, s.boost);
    if (!s.boost) s.decay = 0;
    RecomputePriority(e, thread, s);
}
// A timeout scans ready threads, then the running thread. Clock, queue
// membership, and delivery are native providers. Defaults are a 16 ms
// quantum, 48 ms period, and 1 ms increment.
template<class E> U StarvationScan(E &e, U now, U period, U quantum, U increment)
{
    U threshold = period + (period >> 1), count = 0;
    for (U thread = e.FirstReady(); thread; thread = e.NextReady(thread))
    {
        ++count;
        auto &s = e.Priority(thread);
        if (now - e.LastRun(thread) < threshold || !s.effective) continue;
        const U maximum = e.Maximum(thread);
        const U sum = increment + s.effective;
        // Sign extension also changes EDX, the age threshold for later entries.
        threshold = SignMask(sum - maximum);
        const U bounded = ((sum - maximum) & threshold) + maximum;
        if (bounded <= s.effective) continue;
        const U boost = bounded - s.effective + s.boost;
        BoostPriority(e, thread, s, boost, boost, maximum);
    }
    return MaxWrap(quantum * count, 48);
}

template<class E> U GetPriority(E &e, U handle)
{
    e.EnterThreads();
    const U thread = e.Lookup(handle, 0x40);
    const U result = thread ? e.RelativePriority(thread) : 0x7fffffffu;
    e.LeaveThreads(); return result;
}

template<class E> U SetPriority(E &e, U handle, U value)
{
    e.EnterThreads();
    const U thread = e.Lookup(handle, 0x40);
    U result = 0;
    if (thread)
    {
        if ((SignedLess(value, U(-2)) || SignedLess(2, value)) && value != 15 && value != U(-15))
            e.Error(1800);
        else
        {
            e.SetRelativePriority(thread, value);
            if (e.RuntimeThread(thread)) e.PublishPriority(thread, BasePriority(value, e.ProcessBase(e.ThreadProcess(thread))));
            result = 1;
        }
    }
    e.LeaveThreads(); return result;
}

template<class E> U GetProcessPriority(E &e, U handle)
{
    e.EnterThreads();
    const U process = e.Lookup(handle, 0x20);
    U result = 0;
    if (process)
    {
        switch (e.ProcessBase(process)) { case 4:result=0x40;break;case 8:result=0x20;break;
            case 13:result=0x80;break;case 24:result=0x100;break; }
    }
    e.LeaveThreads(); return result;
}
// Allocation failure after changing the class does not restore it.
template<class E> U SetProcessPriority(E &e, U handle, U value)
{
    e.EnterThreads();
    const U process = e.Lookup(handle, 0x20);
    U result = 0, base = 0;
    if (process)
    {
        switch (value) { case 0x40:base=4;break;case 0x20:base=8;break;case 0x80:base=13;break;case 0x100:base=24;break; }
        if (!base) e.Error(1800);
        else
        {
            result = 1;
            if (base != e.ProcessBase(process))
            {
                e.SetProcessBase(process, base);
                U iterator = e.FirstThread(process);
                if (iterator)
                {
                    const U count_word = e.ProcessThreadCount(process) & 65535u;
                    const U signed_count = (count_word & 0x8000u) ? count_word | 0xffff0000u : count_word;
                    const U buffer = e.AllocatePriorityPairs(signed_count << 3);
                    if (!buffer) { e.Error(8); result = 0; }
                    else
                    {
                        U count = 0;
                        do
                        {
                            const U thread = e.IteratorThread(iterator);
                            if (e.RuntimeThread(thread)) e.PriorityPair(buffer, count++, thread,
                                BasePriority(e.RelativePriority(thread), base));
                            iterator = e.NextThread(process);
                        } while (iterator);
                        if (count) e.PublishPriorityPairs(buffer, count);
                        e.FreePriorityPairs(buffer);
                    }
                }
            }
        }
    }
    e.LeaveThreads(); return result;
}
// Caller has already validated the output DWORD.
template<class E> U ExitCode(E &e, U handle, U output)
{
    e.EnterGlobal();
    const U thread = e.Lookup(handle, 0x80000040u);
    if (thread) { e.WriteExitCode(output, e.ThreadExitCode(thread)); e.ReleaseThreadReference(thread); }
    e.LeaveGlobal(); return thread != 0;
}
// Original Win98 is a single-processor system.
template<class E> U Affinity(E &e, U handle, U mask)
{
    if (!e.Lookup(handle, 0x40)) { e.Error(6); return 0; }
    if (mask != 1) { e.Error(13); return 0; }
    return 1;
}
} // namespace Win98ThreadControl
