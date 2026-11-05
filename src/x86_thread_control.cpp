// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include <string.h>
namespace X86
{
namespace W = Win98ThreadControl;
struct Application::ThreadControlEnvironment
{
    Application &app;
    unsigned iterator = 0, deferred = 0, pair_count = 0;
    unsigned pairs[2 * Application::ThreadCapacity]{};
    unsigned ready[Application::ThreadCapacity]{}, ready_count = 0;
    // The primary thread of a child process, reached through the thread
    // handle of its PROCESS_INFORMATION (kernel handle kind 7). The
    // environment is then the child's, entered from the creator's turn; the
    // session runs one guest thread at a time, so the child's thread lock
    // needs no wait in the child's name.
    unsigned foreign_thread = 0;
    Application *caller = nullptr;
    explicit ThreadControlEnvironment(Application &a) : app(a) {}
    Thread &Data(unsigned thread) { return app.threads[thread - 1]; }
    void EnterThreads() { if (!foreign_thread) app.KernelThreadLock(true, false); }
    void LeaveThreads() { if (!foreign_thread) app.KernelThreadLock(false, false); }
    void EnterGlobal() { app.KernelThreadLock(true, true); }
    void LeaveGlobal() { app.KernelThreadLock(false, true); }
    void Error(unsigned code) { (caller ? caller : &app)->last_error = code; }
    unsigned Lookup(unsigned handle, unsigned type)
    {
        if (foreign_thread) return foreign_thread;
        if ((type & 0x40) && handle == 0xfffffffeu) return app.current_thread + 1;
        if ((type & 0x20) && handle == 0x7fffffffu) return 17;
        if (type & 0x40)
        {
            const auto *h = app.FindKernelHandle(handle);
            if (h && h->kind == 2) return h->object + 1;
        }
        Error(6); return 0;
    }
    unsigned ThreadFlags(unsigned thread) { return Data(thread).done ? 0x10000000u : 0; }
    unsigned RuntimeThread(unsigned thread) { return Data(thread).done ? 0 : thread; }
    unsigned SuspendCount(unsigned thread) { return Data(thread).suspended; }
    void SetSuspendCount(unsigned thread, unsigned value) { Data(thread).suspended = value; }
    unsigned SuspendRuntime(unsigned thread) { return W::SuspendVm(*this, thread, Data(thread).control_suspend); }
    unsigned ResumeRuntime(unsigned thread) { return W::ResumeVm(*this, thread, Data(thread).control_suspend); }
    unsigned ScheduleSuspend(unsigned thread)
    {
        // One pending event fits in the existing thread record. No heap/handle
        // allocation is needed; the VWIN32 pending bit prevents duplicate events.
        auto &t = Data(thread);
        if (t.control_event) { app.Fail("Duplicate thread suspension event"); return 0; }
        t.control_event = app.next_control_event++;
        if (!t.control_event) t.control_event = app.next_control_event++;
        return t.control_event;
    }
    void CancelSuspend(unsigned thread, unsigned event)
    {
        auto &t = Data(thread);
        if (t.control_event && t.control_event != event) app.Fail("Mismatched thread suspension event");
        t.control_event = 0;
    }
    void WakeSuspend(unsigned) {} // Runnable observes the original published counters.
    bool WaitSuspend(unsigned thread)
    {
        if (thread != app.current_thread + 1) { app.Fail("Suspension callback entered in wrong thread"); return false; }
        auto &t = Data(thread);
        t.control_event = 0;
        t.block = 6;
        app.Suspend(Running);
        return app.state != Failed && app.state != Exited;
    }
    unsigned RelativePriority(unsigned thread) { return Data(thread).relative_priority; }
    void SetRelativePriority(unsigned thread, unsigned value) { Data(thread).relative_priority = value; }
    unsigned ThreadProcess(unsigned) { return 17; }
    unsigned ProcessBase(unsigned) { return app.process_priority_base; }
    void SetProcessBase(unsigned, unsigned value) { app.process_priority_base = value; }
    void RequeuePriority(unsigned, unsigned) {} // Pump selects the greatest effective priority directly.
    void PropagatePriority(unsigned, unsigned) {} // No native priority-inheritance links exist yet.
    void ReschedulePriority() {} // The public boundary preempts after releasing the kernel lock.
    void DeferPriority(bool enter) { if (enter) ++deferred; else --deferred; }
    void PublishPriority(unsigned thread, unsigned value)
    {
        W::SetBasePriority(*this, thread, Data(thread).control_priority, value);
    }
    unsigned FirstThread(unsigned) { iterator = 0; return NextThread(17); }
    unsigned NextThread(unsigned)
    {
        while (iterator < Application::ThreadCapacity)
        {
            const unsigned i = iterator++;
            if (app.threads[i].fiber) return i + 1;
        }
        return 0;
    }
    unsigned IteratorThread(unsigned iterator_token) { return iterator_token; }
    unsigned ProcessThreadCount(unsigned)
    {
        unsigned count = 0; for (const auto &t : app.threads) if (t.fiber) ++count; return count;
    }
    unsigned AllocatePriorityPairs(unsigned bytes)
    {
        // The native process has at most sixteen thread records. Fixed local
        // storage replaces the original temporary heap allocation, including its lifetime.
        pair_count = 0; return bytes <= sizeof pairs ? 1 : 0;
    }
    void PriorityPair(unsigned, unsigned index, unsigned thread, unsigned value)
    {
        if (index >= Application::ThreadCapacity) { app.Fail("Thread priority-pair storage overflow"); return; }
        pairs[index * 2] = thread; pairs[index * 2 + 1] = value; ++pair_count;
    }
    void PublishPriorityPairs(unsigned, unsigned count)
    {
        if (count != pair_count) { app.Fail("Invalid thread priority-pair count"); return; }
        for (unsigned i = 0; i < count; ++i)
            W::SetBasePriority(*this, pairs[i * 2], Data(pairs[i * 2]).control_priority,
                               pairs[i * 2 + 1], i + 1 < count);
    }
    void FreePriorityPairs(unsigned) { pair_count = 0; }
    unsigned FirstReady() { return ready_count ? ready[0] : 0; }
    unsigned NextReady(unsigned thread)
    {
        for (unsigned i = 0; i + 1 < ready_count; ++i)
            if (ready[i] == thread) return ready[i + 1];
        return 0;
    }
    W::PriorityState &Priority(unsigned thread) { return Data(thread).control_priority; }
    unsigned LastRun(unsigned thread) { return Data(thread).control_last_run; }
    // All native Win32 processes live in the normal system VM. A realtime
    // process base does not turn that VM into VMM's separate realtime VM.
    unsigned Maximum(unsigned) { return 15; }
    unsigned ThreadExitCode(unsigned thread) { return Data(thread).done ? Data(thread).result : 259; }
    void WriteExitCode(unsigned output, unsigned value) { app.Write32(output, value); }
    void ReleaseThreadReference(unsigned) {} // Cooperative lookup has no escaping borrowed pointer.
};
void Application::InitializeThreadControl(unsigned index)
{
    auto &t = threads[index];
    t.relative_priority = 0;
    t.control_suspend = {};
    // CreateThread initializes the public count to zero or one. A suspended
    // start has no active callback, so it needs an actual pending event. Merely
    // copying count=1 would make ResumeVm publish an unconsumed wake bit.
    if (t.suspended)
    {
        ThreadControlEnvironment e(*this);
        W::SuspendVm(e, index + 1, t.control_suspend);
    }
    t.control_priority = {};
    t.control_priority.base = t.control_priority.effective = W::BasePriority(0, process_priority_base);
    t.control_last_run = unsigned(Now() / 1000);
    t.control_quantum_us = 0;
}
void Application::ScanThreadPriorities()
{
    const auto now = Now();
    if (!control_scan_due) control_scan_due = now + control_scan_period * 1000ull;
    if (now < control_scan_due) return;
    ThreadControlEnvironment e(*this);
    // The native ready queue retains Pump's round-robin order. Like VMM's
    // timeout, visit the previous running thread after the other ready threads.
    for (unsigned n = 0; n < ThreadCapacity; ++n)
    {
        const unsigned i = (scheduler_cursor + n) % ThreadCapacity;
        if (i != current_thread && Runnable(i)) e.ready[e.ready_count++] = i + 1;
    }
    if (Runnable(current_thread)) e.ready[e.ready_count++] = current_thread + 1;
    if (e.ready_count)
        control_scan_period = W::StarvationScan(e, unsigned(now / 1000), control_scan_period, 16, 1);
    control_scan_due = now + control_scan_period * 1000ull;
}
void Application::AccountThreadQuantum(unsigned index, unsigned long long began)
{
    const auto now = Now();
    auto &t = threads[index];
    t.control_last_run = unsigned(now / 1000);
    const auto elapsed = now - began + t.control_quantum_us;
    if (elapsed >= 16000)
    {
        ThreadControlEnvironment e(*this);
        W::DecayPriority(e, index + 1, t.control_priority);
        t.control_quantum_us = 0;
    }
    else t.control_quantum_us = unsigned(elapsed);
}
void Application::PreemptThreadControl()
{
    if (state == Failed || state == Exited) return;
    for (unsigned i = 0; i < ThreadCapacity; ++i)
        if (i != current_thread && Runnable(i) &&
            threads[i].control_priority.effective > threads[current_thread].control_priority.effective)
        {
            threads[current_thread].block = 6;
            Suspend(Running);
            return;
        }
}
unsigned Application::ThreadControlApi(ApiName name, const unsigned *a)
{
    ThreadControlEnvironment e(*this);
    unsigned result;
    // SuspendThread and ResumeThread on the thread handle CreateProcess gave
    // back: the handle names the child's primary thread. Windows 98 accepts
    // any thread object in the session. CREATE_SUSPENDED
    // children wait in that thread until the creator resumes it; MSI starts
    // the EXE of a custom action this way.
    if (name.id == API_ID("SuspendThread") || name.id == API_ID("ResumeThread"))
        if (const auto *h = FindKernelHandle(a[0]); h && h->kind == 7)
        {
            bool done = false;
            Application *child = ChildApplication(h->object, done);
            const bool resume = name.id == API_ID("ResumeThread");
            // An ended thread has the count zero (Resume answers FFFFFFFF
            // without an error) and no runtime state (Suspend: 259).
            if (!child || done) { if (!resume) last_error = 259; return ~0u; }
            ThreadControlEnvironment ce(*child);
            ce.foreign_thread = 1;
            ce.caller = this;
            result = resume ? W::Resume(ce, a[0]) : W::Suspend(ce, a[0]);
            return result;
        }
    switch (name.id)
    {
    API_CASE("SuspendThread")
    {
        result = W::Suspend(e, a[0]);
        auto &t = threads[current_thread];
        if (result != ~0u && t.suspended)
            W::SuspendCallback(e, current_thread + 1, t.control_suspend);
        return result;
    }
    API_CASE("ResumeThread")
    {
        result = W::Resume(e, a[0]);
        if (result != ~0u) PreemptThreadControl();
        return result;
    }
    API_CASE("GetThreadPriority") return W::GetPriority(e, a[0]);
    API_CASE("SetThreadPriority")
    {
        result = W::SetPriority(e, a[0], a[1]);
        if (result) PreemptThreadControl();
        return result;
    }
    API_CASE("GetPriorityClass") return W::GetProcessPriority(e, a[0]);
    API_CASE("SetPriorityClass")
    {
        result = W::SetProcessPriority(e, a[0], a[1]);
        if (result) PreemptThreadControl();
        return result;
    }
    API_CASE("SetThreadAffinityMask") return W::Affinity(e, a[0], a[1]);
    API_CASE("GetExitCodeThread")
    {
        if (!a[1] || !virtual_memory.Access(a[1], 4, true)) { last_error = 87; return 0; }
        return W::ExitCode(e, a[0], a[1]);
    }
    default:
        break;
    }
    Fail("Unrouted thread control API"); return 0;
}
// A thread out of a saved state that stood in a call of the system
// (Application::Run, src/x86_state_machine.cpp). Nearly every call a thread
// may be saved in only waits and is made again from its gate. SuspendThread
// is not one of those: the thread has suspended itself already and waits to
// be resumed, and made again it would suspend itself twice. Here is the
// rest of that call: the wait of W::SuspendCallback, whose pending bit the
// call took before it waited, and the return. A running thread has the
// count zero, so the count before, which the call answers, is zero.
bool Application::StateResume()
{
    auto &t = threads[current_thread];
    const auto &item = imports[(pending - GateBase) / 16];
    // A call with frames of the host that a state carries as records (a
    // modal dialog): the gate is served, and in the place of the call its
    // frames are entered again (Application::ResumeHostCall).
    if (t.host_resume)
        return Service();
    if (strcmp(item.name, "SuspendThread") || Win16Process())
        return Service();
    ThreadControlEnvironment e(*this);
    while (t.control_suspend.count)
    {
        if (!e.WaitSuspend(current_thread + 1)) return false;
        const bool woke = (t.control_suspend.flags & 8) != 0;
        t.control_suspend.flags &= ~8u;
        if (woke) break;
    }
    const unsigned sp = Register(Reg::Esp);
    Register(Reg::Eax, 0);
    Register(Reg::Eip, Read32(sp));
    Register(Reg::Esp, sp + 8);
    pending = 0;
    waiting = false;
    return state != Failed && state != Exited;
}
} // namespace X86
