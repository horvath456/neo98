// SPDX-License-Identifier: GPL-2.0-or-later
// NEO98's cooperative Windows thread/event runtime. New implementation.
#include "win98_kernel.h"
#include "win98_local_handles.h"
#include "x86_app.h"
#include "win16_machine.h"
#include "win98_system_clock.h"
#include "host_microseconds.h"
#include "sound.h"
#include "arm64_machine.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <new>
#if defined(__APPLE__) || defined(__linux__)
#include <time.h>
#else
extern "C" unsigned long long neo98_clock_us();
#endif
namespace X86
{
Fiber *Application::NewThreadFiber()
{
    void *storage = malloc(sizeof(Fiber));
    return storage ? new (storage) Fiber : nullptr;
}
void Application::FreeThreadFiber(Fiber *fiber)
{
    if (!fiber) return;
    fiber->~Fiber();
    free(fiber);
}
// The host's clock, which runs through a pause of the machine. The guest's
// wall clock (src/x86_time.cpp) reads it; everything else reads Now.
unsigned long long Application::HostNow()
{
#if defined(__APPLE__)
    return HostClock::Microseconds();
#elif defined(__linux__)
    timespec time = {};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (unsigned long long)time.tv_sec * 1000000 + time.tv_nsec / 1000;
#else
    return neo98_clock_us();
#endif
}
// The pause of the whole machine. pause_began is the host
// time at which the machine stopped, zero while it runs; pause_total is the
// sum of the pauses that are over. Both are read on every Now.
namespace
{
unsigned long long pause_began = 0, pause_total = 0;
}
// The clock of the machine in microseconds: the host's clock less every
// pause. It stands while the machine is paused. GetTickCount, timeGetTime,
// QueryPerformanceCounter, the timers, the timeouts of the waits, the
// vertical blank and the clock of the mixer without a device all read it.
unsigned long long Application::Now()
{
    // A host thread beside the main loop may read the clock as well.
    const unsigned long long began = __atomic_load_n(&pause_began, __ATOMIC_RELAXED);
    return (began ? began : HostNow()) - __atomic_load_n(&pause_total, __ATOMIC_RELAXED);
}
void Application::SetNow(unsigned long long machine_microseconds)
{
    // Now is the moment the pause began less the sum of the pauses; the
    // sum may wrap, the difference does not.
    const unsigned long long now = HostNow();
    __atomic_store_n(&pause_began, now ? now : 1, __ATOMIC_RELAXED);
    __atomic_store_n(&pause_total, (now ? now : 1) - machine_microseconds, __ATOMIC_RELAXED);
}
bool Application::MachinePaused() { return __atomic_load_n(&pause_began, __ATOMIC_RELAXED) != 0; }
unsigned long long Application::PausedMicroseconds()
{
    const unsigned long long began = __atomic_load_n(&pause_began, __ATOMIC_RELAXED);
    return __atomic_load_n(&pause_total, __ATOMIC_RELAXED) + (began ? HostNow() - began : 0);
}
// The one switch. The clock stops at this moment and goes on from it, the
// mixer plays silence and keeps its place, and the keyboard and the mouse
// reach no program (RawKey, MouseButtons, MouseWheel). The caller stops
// running the guest: the host leaves Pump alone and parks the threads of its
// own that run guest code (tools/neo98_host.cpp, machine_pause).
void Application::PauseMachine(bool pause)
{
    if (pause == MachinePaused()) return;
    const unsigned long long now = HostNow();
    if (pause) __atomic_store_n(&pause_began, now ? now : 1, __ATOMIC_RELAXED);
    else
    {
        // The sum first: a reader between the two stores sees the clock a
        // pause behind for a moment, never ahead.
        __atomic_store_n(&pause_total, pause_total + (now - pause_began), __ATOMIC_RELAXED);
        __atomic_store_n(&pause_began, 0ull, __ATOMIC_RELAXED);
        Arm64::SetCounterPause(pause_total);
        X86::SetCounterPause(pause_total);
        // The mouse of DirectInput reports movements: where the pointer went
        // meanwhile is none.
        DinputForget();
    }
    Sound::Shared().Hold(pause);
}
namespace
{
Win98Clock::SystemClock system_clock;
bool TraceClock()
{
    static const bool on = getenv("NEO98_TRACE_CLOCK") != nullptr;
    return on;
}
} // namespace
unsigned Application::TickCount() { return system_clock.TickCount(Now()); }
unsigned Application::SystemTime() { return system_clock.SystemTime(Now()); }
void Application::ArmTimeout(unsigned milliseconds) { system_clock.ArmTimeout(milliseconds, Now()); }
bool Application::ChangeMinimumPeriod(unsigned period, bool begin)
{
    const bool okay = begin ? system_clock.Begin(period, Now()) : system_clock.End(period, Now());
    // NEO98_TRACE_CLOCK=1 names every request and the interrupt period after it.
    if (TraceClock())
        fprintf(stderr, "NEO98 CLOCK %s period=%u ok=%d interrupt-count=%llu\n", begin ? "begin" : "end", period,
                okay ? 1 : 0, system_clock.count);
    return okay;
}
unsigned Application::ThreadId() const { return threads[current_thread].id; }
unsigned Application::LiveThreads() const
{
    unsigned n = 0;
    for (const auto &t : threads)
        if (t.fiber && !t.done)
            ++n;
    return n;
}
unsigned Application::Teb() const { return TebOf(current_thread); }
Application::KernelHandle *Application::FindKernelHandle(unsigned h)
{
    // KERNEL32's handle resolver knows the pseudo-handles before it
    // ever looks at the table:  is the running process (
    // the value GetCurrentProcess returns ), FFFFFFF6h..FFFFFFF4h
    // are the three standard handles, and FFFFFFFEh is the running thread
    // (the value GetCurrentThread returns ). The thread
    // branch reads the live thread record and never tests the access mask, so
    // the pseudo-handle always carries full access. FFFFFFFFh is no
    // pseudo-handle at all: Windows 98 keeps it as a table index, the bounds
    // test rejects it and the caller gets error 6.
    // Resolve the current process to the same identity-backed object as a
    // foreign process. Waits and DuplicateHandle use this resolver too;
    // rejecting the pseudo-handle breaks RPCRT4's local client waits.
    if (h == 0x7fffffffu)
    {
        current_process_handle = KernelHandle{};
        current_process_handle.kind = 9;
        current_process_handle.object = ProcessId();
        return &current_process_handle;
    }
    if (h == 0xfffffffeu)
    {
        current_thread_handle = KernelHandle{};
        current_thread_handle.kind = 2;
        current_thread_handle.object = current_thread;
        return &current_thread_handle;
    }
    // A handle ConvertToGlobalHandle published carries the original's mixing
    // constant and is therefore no longer a value of this table's own space.
    // It is looked up first, because it names the same object in every
    // process of the session.
    if (h)
        for (auto &global : global_kernel_handles)
            if (global.handle == h)
                return &global.slot;
    if ((h & 0xff000000) != 0x60000000)
        return nullptr;
    unsigned i = KernelHandleNumber(h);
    if (!i || i > KernelHandleCapacity)
        return nullptr;
    auto &slot = kernel_handles[i - 1];
    if (slot.kind && slot.generation == KernelHandleGeneration(h))
        return &slot;
    return nullptr;
}
// ConvertToGlobalHandle. The original duplicates the object into the shared
// process of KERNEL32, closes the source and hands the duplicate back mixed
// with 453A4D3Ch (src/win98_local_handles.h). The shared process
// is the table global_kernel_handles here: its entries hold a reference of
// their own, so the object outlives the process that made it, and every
// process resolves them first (FindKernelHandle).
// The answer is the handle in that table, numbered by its slot above the 128
// entries a process numbers in its own table. Before, the published value was
// the caller's own handle value, and two processes that converted handles
// with the same number got one entry: the second caller's value named the
// first caller's object.
// Only a record that means the same thing in every process travels: an event,
// a mutex, a semaphore, a file mapping and a process. A thread handle names a
// slot of the process that owns the thread and stays where it is. A process
// travels as its identity (kind 9), which every process resolves alike; the
// pseudo-handle of GetCurrentProcess is what OpenProcess answers for the
// caller's own identity here, and DuplicateHandle in the original resolves
// it to the process object as well. SHELL32 needs exactly that :
// every process that runs its notification thread puts
// ConvertToGlobalHandle(OpenProcess(SYNCHRONIZE, FALSE, own id)) and the
// thread into a list in shared memory, and whoever sends a notification
// drops each entry whose handle does not answer WAIT_TIMEOUT.
// With zero for the handle the first notification emptied the list and freed
// it, and the next process to leave read the freed list's null.
unsigned Application::PublishGlobalHandle(unsigned handle)
{
    KernelHandle record;
    if (handle == 0x7fffffffu)
    {
        record.kind = 9;
        record.object = ProcessId();
    }
    else
    {
        const auto *source = FindKernelHandle(handle);
        if (!source) return 0;
        record = *source;
        record.owner = nullptr;
        // A child's handle names a slot of this process; its identity names
        // the same process everywhere.
        if (record.kind == 6)
        {
            record.kind = 9;
            record.object = ChildProcessIdentity(source->object);
            if (!record.object) return 0;
        }
    }
    const unsigned kind = record.kind;
    if (kind != 1 && kind != 3 && kind != 4 && kind != 5 && kind != 9) return 0;
    for (unsigned i = 0; i < sizeof global_kernel_handles / sizeof *global_kernel_handles; ++i)
    {
        auto &global = global_kernel_handles[i];
        if (global.handle) continue;
        // CloseHandle advances the generation of a slot it empties, so a
        // stale value never names the next object in the same slot.
        record.generation = global.slot.generation;
        const unsigned shared = KernelHandleValue(KernelHandleCapacity + 1 + i, record.generation);
        global.handle = shared ^ Win98LocalHandles::GlobalMix;
        global.slot = record;
        if (kind == 1) ++events[record.object].references;
        else if (kind == 3) ++kernel_mutexes[record.object].references;
        else if (kind == 4) ++semaphores[record.object].references;
        else if (kind == 5) ReferenceMapping(record.object);
        // the source is closed. The pseudo-handle owns nothing.
        if (handle != 0x7fffffffu)
        {
            const unsigned close[1] = {handle};
            ThreadApi("CloseHandle", close);
        }
        return shared;
    }
    return 0;
}
// The process whose thread table a thread handle names. A handle that came
// from DuplicateHandle across processes carries its owner; everything else
// belongs to the process that holds it. A process that has already left the
// session is gone, and its threads with it, so the handle falls back to this
// process and its wait answers at once.
Application *Application::ThreadHandleOwner(const KernelHandle *handle)
{
    if (!handle || !handle->owner || handle->owner == this) return this;
    for (auto *process : session_processes)
        if (process == handle->owner) return process;
    return this;
}
unsigned Application::NewKernelHandle(unsigned kind, unsigned object)
{
    for (unsigned i = 0; i < KernelHandleCapacity; ++i)
    {
        auto &h = kernel_handles[i];
        if (!h.kind)
        {
            h.kind = kind;
            h.object = object;
            h.access = 0x1fffff;
            h.inherit = false;
            if (kind == 1)
                ++events[object].references;
            else if (kind == 3)
                ++kernel_mutexes[object].references;
            else if (kind == 4)
                ++semaphores[object].references;
            else if (kind == 5)
                ReferenceMapping(object);
            else if (kind == 6 || kind == 7)
                ReferenceChild(object);
            else if (kind == 8)
                ++change_notifications[object].references;
            else if (kind == 10) ReferenceSnapshot(object);
            else if (kind == 2)
                ++threads[object].references;
            return KernelHandleValue(i + 1, h.generation);
        }
    }
    if (log)
    {
        // Which kinds fill the table, for a program that runs out.
        unsigned count[16] = {};
        for (const auto &h : kernel_handles)
            ++count[h.kind < 16 ? h.kind : 15];
        char text[256];
        int at = snprintf(text, sizeof text, "kernel handle table full, kinds:");
        for (unsigned k = 1; k < 16 && at > 0 && at < (int)sizeof text; ++k)
            if (count[k])
                at += snprintf(text + at, sizeof text - at, " %u=%u", k, count[k]);
        log(text);
    }
    last_error = 8;
    return 0;
}
bool Application::Signaled(unsigned handle, unsigned thread)
{
    TickWaitableTimers();
    auto *h = FindKernelHandle(handle);
    if (!h)
        return true; // API reports invalid handle after resuming.
    if (h->kind == 6 || h->kind == 7) return ChildSignaled(h->object, h->kind == 7);
    // A foreign process handle carries the process identity, not a slot.
    if (h->kind == 9) return ForeignProcessSignaled(h->object);
    if (h->kind == 5)
        return true; // Mapping objects cannot be waited on; the API rejects them.
    if (h->kind == 3)
    {
        const auto &m = kernel_mutexes[h->object];
        return !m.owner || m.owner == thread;
    }
    if (h->kind == 4)
        return Win98Semaphore::CanAcquire(semaphores[h->object].data);
    // The change-notification event is manual-reset: only the original
    // FindNextChangeNotification re-arm clears it, never a completed wait.
    if (h->kind == 8)
        return change_notifications[h->object].signaled;
    if (h->kind == 1) return events[h->object].signaled;
    return ThreadHandleOwner(h)->threads[h->object].done;
}
unsigned Application::ThreadEip(unsigned i)
{
    unsigned eip = 0;
    if (i >= ThreadCapacity) return 0;
    if (i == current_thread && cpu) { cpu->Get(Reg::Eip, eip); return eip; }
    if (threads[i].context) cpu->ContextGet(threads[i].context, Reg::Eip, eip);
    return eip;
}
// Does the running thread own a critical section of the guest? The central
// composition borrows whatever thread suspends and then runs window procedures
// that can send messages to other threads. Win98 never paints on a thread that
// did not ask USER for it, and a thread in the middle of a locked stretch does
// not ask, so the compositor keeps its hands off it.
bool Application::HoldsCriticalSection()
{
    const unsigned thread = ThreadId();
    if (!thread) return false;
    const Critical *named[] = {&critical_system, &critical_threads, &critical_tls, &critical_crt_exit,
                               &critical_crt_locale, &critical_crt_multibyte, &critical_path,
                               &critical_path_task};
    for (const auto *p : named)
        if (p->data.owner == thread) return true;
    for (auto *head : critical_buckets)
        for (const auto *p = head; p; p = p->next)
            if (p->data.owner == thread) return true;
    for (auto *head : global_critical_buckets)
        for (const auto *p = head; p; p = p->next)
            if (p->data.owner == thread) return true;
    return false;
}
bool Application::AnyRunnable()
{
    for (unsigned i = 0; i < ThreadCapacity; ++i)
        if (Runnable(i)) return true;
    return false;
}
bool Application::AnyRunnableExcept(const Application *app, unsigned thread)
{
    for (unsigned i = 0; i < ThreadCapacity; ++i)
        if ((this != app || i != thread) && Runnable(i)) return true;
    return false;
}
bool Application::Runnable(unsigned i)
{
    if(debug_stopped)return false;
    extern bool CloseProgramThreadRunnable(unsigned);
    if (!CloseProgramThreadRunnable(threads[i].id)) return false;
    TickWaitableTimers();
    TickNetwork();
    struct Binding{unsigned p=User::CurrentProcess(),t=User::CurrentThread();~Binding(){User::BindTask(p,t);}} binding;User::BindTask(process_id,threads[i].id);
    auto &t = threads[i];
    if (!t.fiber || t.done || t.suspended)
        return false;
    if (t.alertable && t.apc_count) return true;
    if (t.pulse_handle) return true;
    if (t.block == 10 || t.block == 11) return PipeReady(t.wait_handle,t.block==11);
    if (t.block == 9) return ConsoleReady();
    if (t.block == 12 && KernelPipeReady(t.wait_handle)) return true; // mailslot read
    if ((t.block == 14 || t.block == 15) && CommReady(i)) return true; // serial read / WaitCommEvent
    if (t.block == 8) return SentMessageReady(i);
    if (t.block == 1 && (HasSentMessages(i) || HasWinEvents(i) || User::HasQueueActivation(t.id))) return true;
    if (t.block == 16 && HasSentMessages(i)) return true; // WaitForInputIdle is a USER wait
    // A message wait also ends for a native control that owes a paint, as
    // the GetMessage wait below does: the wait serves it where it stands
    // and goes on waiting (Suspend). Explorer's browser thread waits in
    // MsgWaitForMultipleObjects; the list of its address bar kept waiting
    // for its frame there.
    if (t.block == 7) return HasSentMessages(i) || MultiwaitReady(i) || User::HasQueueActivation(t.id) ||
                             (t.multiwait.message && User::PendingControlPaint());
    if (t.block == 6) return true; // Thread-control safe point after a suspend/preemption.
    if (t.block == 4)
        return !loader_owner || loader_owner == t.id;
    if (t.block == 5)
        return CriticalReady(t.wait_handle, t.id);
    // The lock of the screen (src/x86_screen_lock.cpp): a call that draws on
    // the screen runs once the holder unlocked or handed it over; a new Lock
    // runs once those calls have drawn.
    if (t.block == 17)
        return User::ScreenMayDraw(process_id, t.id);
    if (t.block == 18)
        return !User::ScreenGrantsPending(t.id);
    if (t.block == 0)
        return true;
    if (t.block == 1)
    {
        User::Message message = {};
        return (i == InputThread() && MatchingInput(t.message_window, t.message_minimum, t.message_maximum) >= 0) ||
               User::PeekMessage(&message, t.message_window, t.message_minimum, t.message_maximum, false) ||
               // Pending native-control paints make the owning thread
               // runnable, as QS_PAINT ends a Windows 98 message wait.
               User::PendingControlPaint() ||
               TimerReady(i) || User::FrameTrackDue() ||
               (i == 0 && User::Dirty() && !User::Rendering() && ComposeDue());
    }
    if (t.block == 2 && Signaled(t.wait_handle, t.id))
        return true;
    return t.deadline && Now() >= t.deadline;
}
void Application::ReleaseThread(unsigned i)
{
    auto &t = threads[i];
    if (!t.fiber || !t.fiber->Finished() || t.references || !i)
        return;
    ReleaseCrtThreadData(i);
    EndThreadStaticTls(i);
    FreeThreadFiber(t.fiber);
    if (t.context)
        cpu->FreeContext(t.context);
    if (t.stack_size)
        Unmap(t.stack_base, t.stack_size);
    Unmap(TebOf(i), 4096);
    t = Thread{};
}
unsigned Application::CreateThread(const unsigned *a, unsigned crt_start_data)
{
    // A 16-bit module hosted in this 32-bit process (a driver, MSVIDEO) can
    // reach CreateThread from one of its gates: MMSYSTEM's mmThreadCreate, or
    // waveOutOpen starting the multimedia thread. The new thread takes its
    // segment registers from the running context, so it is made from the flat
    // context the hosted call came from (src/x86_win16_hosted.cpp) and the
    // gate's own context comes back afterwards.
    if (win16.state && win16.state->hosted && (Register(Reg::Cs) & 4))
    {
        CpuContext *gate = nullptr;
        if (!Check(cpu->NewContext(gate)) || !Check(cpu->Save(gate)))
        {
            if (gate) cpu->FreeContext(gate);
            last_error = 8;
            return 0;
        }
        unsigned low = 0;
        const bool switched = Win16HostedFlat(low);
        const unsigned handle = switched ? CreateThread(a, crt_start_data) : 0;
        if (switched)
            Win16HostedResume(low);
        Check(cpu->Restore(gate));
        cpu->FreeContext(gate);
        if (!switched)
        {
            Fail("CreateThread from a hosted 16-bit gate without a flat context to start from");
            return 0;
        }
        return handle;
    }
    if (a[0] || (a[4] & ~4u) ||
        (a[5] && (a[5] > ~0u - 3 || !virtual_memory.Access(a[5], 4, true))))
    {
        last_error = 87;
        return 0;
    }
    if (a[1] > 0x200000)
    {
        last_error = 8;
        return 0;
    }
    // Win98 reads dwStackSize as the size to commit, not as the size of the
    // stack. Windows 98 passes it with the SizeOfStackReserve of the program
    // (a zero takes its SizeOfStackCommit), and reserves the
    // program's size unless the commit is larger; then the larger of the
    // commit and 1 MB, in 64 KB steps. A thread of msiexec started with
    // 0A000h runs a custom action that probes 160 KB of stack. The
    // port maps the whole reserve; a thread slot holds 2 MB.
    const unsigned commit = a[1] ? a[1] : image.StackCommit();
    unsigned reserve = image.StackReserve() ? image.StackReserve() : 0x100000;
    if (commit > reserve)
        reserve = commit > 0x100000 ? commit : 0x100000;
    unsigned stack_size = reserve > 0x200000 ? 0x200000 : (reserve + 0xffff) & ~0xffffu;
    if (stack_size < 65536) stack_size = 65536;
    unsigned i = 1;
    for (; i < ThreadCapacity; ++i)
    {
        ReleaseThread(i);
        if (!threads[i].fiber) break;
    }
    if (i == ThreadCapacity)
    {
        // Diagnostics for a failing installer.
        if (log)
        {
            char text[1024];
            int at = snprintf(text, sizeof text, "CreateThread failed: all thread slots of the process are taken:");
            for (unsigned k = 1; k < ThreadCapacity && at > 0 && at < (int)sizeof text; ++k)
                at += snprintf(text + at, sizeof text - at, " %u:%s/r%u/b%u", k,
                               threads[k].fiber && threads[k].fiber->Finished() ? "done" : "live",
                               threads[k].references, threads[k].block);
            log(text);
        }
        last_error = 8;
        return 0;
    }
    // Slots past the reservation at 20000000h take their stack from the
    // private arena, as every Win98 thread does (VirtualAlloc ).
    const unsigned base = i < FixedStackSlots ? 0x20000000 + i * 0x210000 + 4096 : virtual_memory.FindPrivate(stack_size);
    if (!base)
    {
        if (log) log("CreateThread failed: no free private address range for the stack");
        last_error = 8;
        return 0;
    }
    auto &t = threads[i];
    const unsigned handle = NewKernelHandle(2, i);
    if (!handle) return 0;
    // A handle failure must never leave a runnable fiber behind. The handle is
    // reserved first, and every subsequent failure releases all acquired state.
    const unsigned teb = TebOf(i), top = base + stack_size;
    CpuContext *creator = nullptr;
    Fiber *fiber = nullptr;
    bool creator_saved = false, stack_mapped = false, teb_mapped = false;
    auto rollback = [&]() -> unsigned
    {
        if (creator_saved) Check(cpu->Restore(creator));
        if (creator) cpu->FreeContext(creator);
        FreeThreadFiber(fiber);
        if (t.context) cpu->FreeContext(t.context);
        EndThreadStaticTls(i);
        if (teb_mapped) Unmap(teb, 4096);
        if (stack_mapped) Unmap(base, stack_size);
        auto *slot = FindKernelHandle(handle);
        slot->kind = 0;
        slot->generation = (slot->generation % KernelGenerationMask) + 1;
        t = Thread{};
        last_error = 8;
        return 0;
    };
    if (!Check(cpu->NewContext(creator))) return rollback();
    if (!Check(cpu->Save(creator))) return rollback();
    creator_saved = true;
    t.app = this;
    t.id = AllocateThreadIdentity();if(!t.id){last_error=8;return rollback();}
    t.keyboard_layout = threads[current_thread].keyboard_layout;
    t.stack_size = stack_size;
    t.stack_base = base;
    t.suspended = (a[4] & 4) ? 1 : 0;
    InitializeThreadControl(i);
    t.crt_start_data = crt_start_data;
    activation_tasks[i] = {};
    stack_mapped = Map(base, stack_size, 3);
    if (!stack_mapped) return rollback();
    teb_mapped = Map(teb, 4096, 3);
    if (!teb_mapped) return rollback();
    // A Win16 gate runs at CPL 3. Building a helper thread while it is active
    // must therefore load an RPL 3 FS selector backed by a DPL 3 data
    // descriptor. Loading the ordinary ring-zero selector there raises #GP;
    // Unicorn reports that pending exception when the creator context is
    // restored, before the Win16 gate can return.
    const unsigned privilege = Win16Process() ? 3u : 0u;
    const unsigned long long descriptor = 0xfffull | ((unsigned long long)(teb & 0xffffff) << 16) |
                                          ((0x93ull | (unsigned long long)(privilege << 5)) << 40) |
                                          (0x40ull << 48) |
                                          ((unsigned long long)(teb >> 24) << 56);
    Store(SystemPageBase + (3 + i) * 8, &descriptor, 8);
    Register(Reg::Fs, (3 + i) * 8 | privilege);
    Write32(teb, 0xffffffff);
    Write32(teb + 4, top);
    Write32(teb + 8, base);
    Write32(teb + 0x18, teb);
    Write32(teb + 0x1c, 0xffff0001); // Win32 TIB flags and released thunk count
    Write32(teb + 0x2c, teb + 0x88); // TDB constructor : the TLS slots
    Write32(teb + 0x20,process_id);Write32(teb + 0x24, t.id);
    Write32(teb + 0x30, process_database);
    // Every thread owns its own static TLS blocks. They exist before the first
    // DLL_THREAD_ATTACH notification and before the thread routine starts.
    if (!StartThreadStaticTls(i)) return rollback();
    const int zero[] = {Reg::Eax, Reg::Ebx, Reg::Ecx, Reg::Edx,
                       Reg::Esi, Reg::Edi, Reg::Ebp};
    for (int r : zero) Register(r, 0);
    // The i386 CRT reads the scope and try level beyond the SEH chain pair.
    // Leave room for the complete thread-start registration and its scope.
    const unsigned entry_stack = top - ((!Win16Process() && !Arm64Process()) ? 40 : 16);
    Register(Reg::Esp, entry_stack);
    Write32(entry_stack, 0x7000ffe0);
    Write32(entry_stack + 4, a[3]);
    if (!Win16Process() && !Arm64Process()) InstallThreadStartHandler(teb, top - 32);
    Register(Reg::Eip, a[2]);
    Register(Reg::Eflags, 2);
    if (!InitializeFpu(Win32FpuControl)) return rollback();
    if (state == Failed || !Check(cpu->NewContext(t.context)) ||
        !Check(cpu->Save(t.context))) return rollback();
    if (!Check(cpu->Restore(creator))) return rollback();
    cpu->FreeContext(creator);
    creator = nullptr;
    creator_saved = false;
    fiber = NewThreadFiber();
    if (!fiber || !fiber->Start(
                      [](void *p)
                      {
                          auto *thread = static_cast<Thread *>(p);
                          thread->app->Run();
                      }, &t)) return rollback();
    if (a[5]) Write32(a[5], t.id);
    if (state == Failed) return rollback();
    t.fiber = fiber; // Publication is the final operation; no provider yields here.
    ++threads_created;
    return handle;
}
unsigned Application::ThreadApi(ApiName name, const unsigned *a)
{
#define API(n) case API_ID(#n):
    switch (name.id)
    {
    API(CreateThread) return CreateThread(a);
    API(GetCurrentThreadId) return ThreadId();
    API(GetCurrentThread) return 0xfffffffe;
    API(GetCurrentProcessId) return process_id;
    API(GetProcessVersion)
    {
        struct Environment
        {
            Application &app;
            void Lock() { app.CrtLock(true); }
            void Unlock() { app.CrtLock(false); }
            unsigned CurrentProcess() { return app.process_id; }
            unsigned FindProcess(unsigned pid) { return pid==app.process_id?pid:0; }
            unsigned MainModule(unsigned) { return 1; } // Private module token, not a public HMODULE.
            unsigned ModuleVersion(unsigned)
            {
                const auto version = app.image.SubsystemVersion();
                // The Win98 module record holds one byte each for major and minor.
                return ((version >> 8) & 0xff00) | (version & 255);
            }
            void Error(unsigned value) { app.last_error = value; }
        } e{*this};
        return Win98::ProcessVersion(e, a[0]);
    }
    API(GetVersionExA)
    {
        struct Environment
        {
            Application &app;
            unsigned Read(unsigned p) { return app.Read32(p); }
            void Write(unsigned p, unsigned value) { app.Write32(p, value); }
            void Word(unsigned p, unsigned value)
            {
                const unsigned short v = value;
                app.Store(p, &v, 2);
            }
            void Byte(unsigned p, unsigned value)
            {
                const unsigned char v = value;
                app.Store(p, &v, 1);
            }
            void Zero(unsigned p, unsigned size)
            {
                unsigned char data[128] = {};
                app.Store(p, data, size);
            }
            unsigned Open()
            {
                unsigned handle = 0;
                bool created = false;
                app.registry.Open(0x80000002, "Software\\Microsoft\\Windows\\CurrentVersion", false, handle,
                                  created);
                return handle;
            }
            const Registry::Value *Value(unsigned handle, const char *name)
            {
                unsigned node;
                const Registry::Value *value = nullptr;
                if (app.registry.Key(handle, node) || app.registry.store.Query(node, name, value))
                    return nullptr;
                return value;
            }
            bool ServicePack(unsigned handle, unsigned &value)
            {
                const auto *data = Value(handle, "ServicePackNumber");
                if (!data || data->size > 4)
                    return false;
                if (data->size)
                    memcpy(&value, data->data, data->size);
                return true;
            }
            bool Suffix(unsigned handle, unsigned p, unsigned size)
            {
                const auto *data = Value(handle, "SubVersionNumber");
                return data && data->size <= size && (!data->size || app.Store(p, data->data, data->size));
            }
            void Close(unsigned handle) { app.registry.Close(handle); }
        } e{*this};
        return Win98::VersionEx(e, a[0]);
    }
    API(SetLastError)
    {
        last_error = a[0];
        return 0;
    }
    API(GetTickCount) return TickCount();
    API_CASE("CreateEventA") API_CASE("OpenEventA") API_CASE("CreateWaitableTimerA") API_CASE("OpenWaitableTimerA")
    {
        const bool timer=strstr(name,"WaitableTimer")!=nullptr;
        const bool open = !strncmp(name,"Open",4);
        unsigned name_address = a[(open||timer) ? 2 : 3];
        bool inherit = open && a[1];
        // `nLength` has to be twelve and only `bInheritHandle` is
        // read. Win98 carries no security on a kernel object, so a supplied
        // descriptor is ignored, not refused.
        if (!open && a[0])
        {
            unsigned attributes[3];
            if (!Read(a[0], attributes, sizeof attributes))
                return 0;
            if (attributes[0] != 12)
            {
                last_error = 87;
                return 0;
            }
            inherit = attributes[2] != 0;
        }
        if (open && !name_address)
        {
            last_error = 87;
            return 0;
        }
        const char *object_name = ReadString(name_address);
        if (state == Failed)
            return 0;
        // treats an empty name like no name at all.
        if (name_address && !*object_name)
            name_address = 0;
        if (strlen(object_name) > KernelObjectNameLimit)
        {
            last_error = 123;
            return 0;
        }
        // The name lives in the session-wide namespace, not in this process.
        unsigned found = KernelObjectSlots;
        if (name_address)
        {
            const unsigned kind = NamedObjectKind(object_name);
            if (kind && kind != 1)
            { last_error = 6; return 0; }
            if (kind == 1)
            {
                found = FindNamedObject(1, object_name) - 1;
                if(events[found].timer!=timer){last_error=6;return 0;}
            }
        }
        const bool existing = found != KernelObjectSlots;
        if (!existing)
        {
            if (open)
            {
                // the name is not in the shared table, so
                // OpenEventA/OpenWaitableTimerA answer ERROR_INVALID_NAME.
                last_error = 123;
                return 0;
            }
            for (unsigned i = 0; i < KernelObjectSlots; ++i)
                if (!events[i].references)
                {
                    found = i;
                    break;
                }
            if (found == KernelObjectSlots)
            {
                if (log) log("CreateEvent failed: the event table of the session is full");
                last_error = 8;
                return 0;
            }
            auto &e = events[found];
            e = {};
            e.named = name_address != 0;
            e.manual = a[1] != 0;
            e.timer=timer;
            e.signaled = !timer && a[2] != 0;
            strcpy(e.name, object_name);
        }
        const unsigned result = NewKernelHandle(1, found);
        if (!result)
        {
            if (!existing)
                events[found] = {};
            return 0;
        }
        auto *h = FindKernelHandle(result);
        h->inherit = inherit;
        h->access = open ? a[0] : 0x1f0003;
        if (h->access & 0x10000000)
            h->access = (h->access & ~0x10000000u) | 0x1f0003;
        if (h->access & 0x80000000)
            h->access = (h->access & ~0x80000000u) | 0x20001;
        if (h->access & 0x40000000)
            h->access = (h->access & ~0x40000000u) | 0x20002;
        if (h->access & 0x20000000)
            h->access = (h->access & ~0x20000000u) | 0x120000;
        if (!open)
            last_error = existing ? 183 : 0;
        return result;
    }
    API_CASE("SetEvent") API_CASE("ResetEvent")
    {
        auto *h = FindKernelHandle(a[0]);
        if (!h || h->kind != 1 || events[h->object].timer)
        {
            last_error = 6;
            return 0;
        }
        if (!(h->access & 2))
        {
            last_error = 5;
            return 0;
        }
        events[h->object].signaled = API_IS(name,"SetEvent");
        return 1;
    }
    // DuplicateHandle (KERNEL32 ordinal 222) first touches the caller's
    // LPHANDLE under an exception handler. It then resolves both process handles with PROCESS_DUP_HANDLE
    // (20h) and the source object with 1FFFFFh; a failure of any of the three
    // leaves with FALSE. Object type 15h always fails with error 0Ch, and
    // types 0Ah and 0Ch fail when the two processes belong to different
    // address contexts. Without DUPLICATE_SAME_ACCESS, Windows 98 builds the new
    // access from the high byte of the source access, 20h for GENERIC_READ,
    // 110h for GENERIC_WRITE and the low 1FFFFFh of the requested access.
    // Windows 98 writes the new table index shifted left by two, and
    // DUPLICATE_CLOSE_SOURCE closes the source afterwards.
    // Files and pipes share their backing state across supported process
    // handles. Console duplication stays within one process; mappings use
    // their existing cross-process provider below.
    API(DuplicateHandle)
    {
        const unsigned options = a[6];
        Application *source_process = ProcessFromHandle(a[0]), *target_process = ProcessFromHandle(a[2]);
        if (!source_process || !target_process)
        {
            last_error = 6;
            return 0;
        }
        if((a[1]&0xff000000)==0x40000000){
            if(!virtual_memory.Access(a[3],4,true)||options&~3u){last_error=87;return 0;}
            unsigned result=0;
            const unsigned access=((a[4]&0x80000000)?1:0)|((a[4]&0x40000000)?2:0);
            unsigned error=(!(options&2)&&(a[4]&0x40000100u))?source_process->files.ValidateTimeHandle(a[1],true):0;
            if(!error)error=source_process->files.Duplicate(a[1],target_process->files,access,options&2,a[5]!=0,result);
            if(!error&&!(options&2))target_process->files.SetWriteAttributes(result,(a[4]&0x40000100u)!=0);
            if(options&1)source_process->files.Close(a[1]);
            if(error){last_error=error;return 0;}Write32(a[3],result);return 1;
        }
        if((a[1]&0xff000000)==0x62000000){
            if(source_process!=this||target_process!=this){last_error=50;return 0;}
            return ConsoleApi("DuplicateHandle",a);
        }
        if(KernelSocketHandle(a[1])){
            if(source_process!=this||target_process!=this){last_error=50;return 0;}
            return KernelPipesApi(name,a);
        }
        if (source_process != this || target_process != this)
        {
            // Windows 98 reads the type byte of the source object and refuses
            // exactly three cases: type 15h always, with error 0Ch, and types
            // 0Ah and 0Ch when the two processes have different memory
            // contexts. Everything else duplicates, a thread included, and
            // DirectSound needs that: it asks DDHELP.EXE for its mixer thread
            // and then waits in the client process on the thread handle until
            // that thread has closed the wave device.
            auto *entry = source_process->FindKernelHandle(a[1]);
            if (!entry) { last_error = 6; return 0; }
            // A file mapping travels as its own mapping of the same bytes
            // (SHLWAPI's SHAllocShared/SHLockShared hand a block over).
            if (entry->kind == 5)
            {
                const unsigned mapping = target_process->DuplicateMappingFrom(*source_process, entry->object);
                if (!mapping) { last_error = 8; return 0; }
                if (a[3] && !Store(a[3], &mapping, 4)) { last_error = 87; return 0; }
                if (options & 1) { const unsigned close[1] = {a[1]}; source_process->ThreadApi("CloseHandle", close); }
                return 1;
            }
            // An event, a mutex and a semaphore already live in one
            // session-wide table, so the target only needs a handle of its
            // own. A thread lives in the table of its process, so the handle
            // carries that process with it.
            if (entry->kind != 1 && entry->kind != 2 && entry->kind != 3 && entry->kind != 4)
            {
                last_error = 0xc;                                     // ERROR_INVALID_ACCESS
                return 0;
            }
            auto *owner = entry->kind == 2 ? source_process->ThreadHandleOwner(entry) : nullptr;
            const unsigned object = entry->object;
            unsigned access = entry->access;
            if (!(options & 2))
            {
                access &= 0xff000000;
                if (a[4] & 0x80000000) access |= 0x20;
                if (a[4] & 0x40000000) access |= 0x110;
                access |= a[4] & 0x1fffff;
            }
            const unsigned result = target_process->NewKernelHandle(entry->kind, object);
            if (!result) { last_error = 8; return 0; }
            auto *target = target_process->FindKernelHandle(result);
            target->access = access;
            target->inherit = a[5] != 0;
            if (entry->kind == 2)
            {
                // NewKernelHandle counted the reference in the target's own
                // table; the thread belongs to `owner`, so move it there.
                --target_process->threads[object].references;
                ++owner->threads[object].references;
                target->owner = owner;
            }
            if (a[3] && !Store(a[3], &result, 4)) { last_error = 87; return 0; }
            if (options & 1) { const unsigned close[1] = {a[1]}; source_process->ThreadApi("CloseHandle", close); }
            return 1;
        }
        auto *source = FindKernelHandle(a[1]);
        if (!source)
        {
            last_error = 6;
            return 0;
        }
        unsigned access = source->access;
        if (!(options & 2))
        {
            access &= 0xff000000;
            if (a[4] & 0x80000000)
                access |= 0x20;
            if (a[4] & 0x40000000)
                access |= 0x110;
            access |= a[4] & 0x1fffff;
        }
        const unsigned kind = source->kind, object = source->object;
        Application *const owner = kind == 2 ? ThreadHandleOwner(source) : nullptr;
        const unsigned result = NewKernelHandle(kind, object);
        if (!result)
            return 0;
        auto *target = FindKernelHandle(result);
        if (kind == 2 && owner != this)
        {
            --threads[object].references;
            ++owner->threads[object].references;
            target->owner = owner;
        }
        target->access = access;
        target->inherit = a[5] != 0;
        if (a[3] && !Store(a[3], &result, 4))
        {
            last_error = 87;
            return 0;
        }
        if (options & 1)
        {
            const unsigned close[1] = {a[1]};
            ThreadApi("CloseHandle", close);
        }
        return 1;
    }
    API(CloseHandle)
    {
        // The process and thread pseudo-handles answer
        // TRUE and change nothing. They own no table entry and no reference.
        if (a[0] == 0x7fffffffu || a[0] == 0xfffffffeu) return 1;
        auto *h = FindKernelHandle(a[0]);
        if (!h)
        {
            last_error = 6;
            return 0;
        }
        if (h->kind == 1){
            if(!--events[h->object].references && events[h->object].timer)CancelTimerApcs(h->object);
        }
        else if (h->kind == 3)
        {
            if (!--kernel_mutexes[h->object].references)
                kernel_mutexes[h->object] = {};
        }
        else if (h->kind == 4)
        {
            if (!--semaphores[h->object].references)
                semaphores[h->object] = {};
        }
        else if (h->kind == 5)
            ReleaseMappingReference(h->object);
        else if (h->kind == 6 || h->kind == 7)
            ReleaseChild(h->object);
        else if (h->kind == 8)
        {
            // Ordinal 281 shares CloseHandle's body; the type-9 destructor
            // closes the IFS search when the last reference disappears.
            if (!--change_notifications[h->object].references)
                CloseChangeNotification(h->object + 1);
        }
        else if (h->kind == 10) ReleaseSnapshot(h->object);
        else if (h->kind == 9)
            ; // a foreign process handle holds no native reference
        else
            --ThreadHandleOwner(h)->threads[h->object].references;
        h->kind = 0;
        h->owner = nullptr;
        h->generation = (h->generation % KernelGenerationMask) + 1;
        // A published handle that is closed loses its publication with the
        // entry; the record survives as long as another process still holds a
        // handle of its own.
        for (auto &global : global_kernel_handles)
            if (&global.slot == h) global.handle = 0;
        return 1;
    }
    API(WaitForSingleObject)
    {
        auto &t = threads[current_thread];
        const auto deadline = a[1] == 0xffffffff ? 0 : Now() + (unsigned long long)a[1] * 1000;
        for (;;)
        {
            auto *h = FindKernelHandle(a[0]);
            // A mapping object and a Toolhelp snapshot are not waitable; a
            // process handle is, whether the process is a child of this one
            // or a foreign one (kind 9).
            if (!h || h->kind == 5 || h->kind >= 10)
            {
                last_error = 6;
                return 0xffffffff;
            }
            // No access check: WaitForSingleObjectEx looks the handle up with
            // required access 0 (the check only runs
            // for a non-zero mask), so a process opened with
            // PROCESS_QUERY_INFORMATION alone is waitable. RPCRT4 waits on
            // exactly such a handle for its LRPC client.
            if (Signaled(a[0], t.id) || t.pulse_handle == a[0])
            {
                t.pulse_handle = 0;
                if (h->kind == 1 && !events[h->object].manual)
                    events[h->object].signaled = false;
                if (h->kind == 4)
                    Win98Semaphore::Acquire(semaphores[h->object].data);
                if (h->kind == 3)
                {
                    auto &m = kernel_mutexes[h->object];
                    if (m.recursion == ~0u)
                    {
                        last_error = 8;
                        return ~0u;
                    }
                    m.owner = t.id;
                    ++m.recursion;
                    const bool abandoned = m.abandoned;
                    m.abandoned = false;
                    return abandoned ? 0x80 : 0;
                }
                return 0;
            }
            if (!a[1] || (deadline && Now() >= deadline))
                return 258;
            t.block = 2;
            t.wait_handle = a[0];
            ArmTimeout(a[1]);
            t.deadline = deadline;
            Suspend(Idle);
            if (state == Failed || state == Exited)
                return 0xffffffff;
        }
    }
    API(Sleep)
    {
        auto &t = threads[current_thread];
        t.block = a[0] ? 3 : 0;
        ArmTimeout(a[0]);
        t.deadline = a[0] == 0xffffffff ? 0 : Now() + (unsigned long long)a[0] * 1000;
        Suspend(a[0] ? Idle : Running);
        return 0;
    }
    API(ResumeThread) return ThreadControlApi(name, a);
    API(GetExitCodeThread)
    {
        auto *h=FindKernelHandle(a[0]);
        if(h && h->kind==7) return ProcessApi(name,a);
        return ThreadControlApi(name, a);
    }
    API(ExitThread)
    {
        auto &t = threads[current_thread];
        if (user_initialized) DestroyThreadWindows(t.id);
        t.done = true;
        t.result = a[0];
        // At queue shutdown, answer incoming sends and remove this thread's
        // outgoing records from foreign lists.
        CancelSendsOfThread(current_thread);
        // with the queue goes its activation state, and the
        // foreground when it was this queue.
        if (user_initialized) User::ForgetThreadQueue(t.id);
        return 0;
    }
    default:
        break;
    }
    Fail(name);
    return 0;
#undef API
}
#include "x86_kernel_async.inc"
#include "x86_waitable_timer.inc"
} // namespace X86
