// SPDX-License-Identifier: GPL-2.0-or-later
// Native allocation and wait services for the implemented Win98 mutex routines.
#include <strings.h>
#include "win98_tls.h"
#include "x86_app.h"
#include "linux_shared_memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned CrtTimeMutex = 0x63fffff8, CrtEnvironmentMutex = 0x63fffff4;
constexpr unsigned SystemMutex = 0x64000000, CrtExitMutex = 0x64000008, TlsMutex = 0x64000010,
                   ThreadMutex = 0x64000014, CrtLocaleMutex = 0x64000018, CrtMultibyteMutex = 0x6400001c, PathMutex = 0x64000004, PathTaskMutex = 0x6400000c;
bool PrivateMutex(unsigned id)
{
    return id == CrtTimeMutex || id == CrtEnvironmentMutex || id == SystemMutex || id == TlsMutex || id == CrtExitMutex || id == CrtLocaleMutex || id == CrtMultibyteMutex ||
           id == PathMutex || id == PathTaskMutex || id == ThreadMutex;
}
} // namespace
Application::Critical *Application::global_critical_buckets[64] = {};
unsigned Application::next_critical_identity = 1;
// The record arena of the session. Win98 keeps the record of a critical
// section in the shared kernel heap, so every process sees it at one address
// and the ring-3 code of KERNEL32 changes it without a
// transition. Here a record is 32 bytes of host memory in chunks of 64 KiB;
// every process maps a chunk at the same guest address, and that address is
// the identity the section's client record carries at +4. Nothing is ever
// moved or handed out twice, so a pointer stays valid for the session.
namespace
{
unsigned char *critical_chunks[Application::CriticalRecordBytes / Application::CriticalChunkBytes];
unsigned char *CriticalChunk(unsigned index, bool create)
{
    if (index >= sizeof critical_chunks / sizeof *critical_chunks) return nullptr;
    if (!critical_chunks[index] && create)
    {
        // The engine wants host memory aligned like the guest chunk: page
        // aligned for Unicorn, host-page aligned for FEXCore, whose host pages
        // are 16 KiB. A chunk is 64 KiB aligned in the
        // guest, so is its host copy.
#if defined(__APPLE__) || defined(__linux__)
        // A block of its own alignment: a saved state follows a pointer to
        // the start of a block and not one into its middle
        // (src/x86_state_fields.h).
        void *block = nullptr;
#if defined(__linux__) && defined(__x86_64__)
        if (ProcessCpuBackend() == CpuBackend::Native)
        {
            block = LinuxSharedMemory::Allocate(Application::CriticalChunkBytes);
            if (!block) return nullptr;
        }
        else
#endif
        if (posix_memalign(&block, Application::CriticalChunkBytes, Application::CriticalChunkBytes)) return nullptr;
        memset(block, 0, Application::CriticalChunkBytes);
        critical_chunks[index] = static_cast<unsigned char *>(block);
#else
        auto *storage = new unsigned char[2 * Application::CriticalChunkBytes]();
        if (!storage) return nullptr;
        const auto aligned = ((unsigned long long)(size_t)storage + Application::CriticalChunkBytes - 1) &
                             ~(unsigned long long)(Application::CriticalChunkBytes - 1);
        critical_chunks[index] = storage + (aligned - (unsigned long long)(size_t)storage);
#endif
    }
    return critical_chunks[index];
}
} // namespace
Application::Critical::~Critical()
{
    // A freed record keeps its bytes for the session, but no longer looks like
    // a mutex: the guest code sees type 0 and hands the call to the host,
    // which then reports the dead section exactly as before.
    if (&data != &own) data = Win98Critical::Mutex{0, 0, 0, 0, 0, 0, 0, 0};
}
// Maps the chunk of the record arena that holds `address` into this process.
// A process maps a chunk when it creates a record in it and when the guest code
// first touches it; the second case is a section another process created,
// reached through shared memory. Called from AllocateMutex and from the memory
// fault hook, where Unicorn retries the access after a true answer.
bool Application::MapCriticalChunk(unsigned address)
{
    if (address < CriticalRecordBase || address - CriticalRecordBase >= CriticalRecordBytes) return false;
    const unsigned index = (address - CriticalRecordBase) / CriticalChunkBytes;
    if (critical_chunks_mapped[index / 32] & (1u << (index % 32))) return false;
    auto *host = CriticalChunk(index, false);
    if (!host || !cpu) return false;
    if (cpu->MapHost(CriticalRecordBase + index * CriticalChunkBytes, CriticalChunkBytes,
                       Prot::Read | Prot::Write, host) != CpuError::Ok)
        return false;
    critical_chunks_mapped[index / 32] |= 1u << (index % 32);
    return true;
}
void Application::PromoteCritical(unsigned id)
{
    auto **link = &critical_buckets[(id >> 5) & 63];
    while (*link && (*link)->id != id) link = &(*link)->next;
    if (!*link) return; // Already global, or the original guarded access failed.
    auto *record = *link;
    *link = record->next;
    auto &global = global_critical_buckets[(id >> 5) & 63];
    record->next = global;
    global = record;
}
Application::Critical *Application::FindCritical(unsigned id)
{
    if (id == SystemMutex)
        return &critical_system;
    if (id == TlsMutex)
        return &critical_tls;
    if (id == CrtExitMutex)
        return &critical_crt_exit;
    if (id == CrtLocaleMutex)
        return &critical_crt_locale;
    if (id == CrtMultibyteMutex) return &critical_crt_multibyte;
    if (id == ThreadMutex) return &critical_threads;
    if (id == PathMutex) return &critical_path;
    if (id == PathTaskMutex) return &critical_path_task;
    for (auto *p = critical_buckets[(id >> 5) & 63]; p; p = p->next)
        if (p->id == id)
            return p;
    for (auto *p = global_critical_buckets[(id >> 5) & 63]; p; p = p->next)
        if (p->id == id) return p;
    return nullptr;
}
bool Application::CriticalReady(unsigned id, unsigned thread)
{
    const auto *p = FindCritical(id);
    if (!p || p->data.owner == thread) return true;
    // Allow a waiter to run when the owner has vanished so
    // SweepCriticalWaiters in WaitCritical can transfer ownership.
    return CriticalThreadGone(p->data.owner, CriticalSession(p));
}
// A session-wide critical section may contain threads from other processes
// that this process table cannot resolve.
bool Application::CriticalSession(const Critical *p) const
{
    if (!p) return false;
    for (const auto *q = global_critical_buckets[(p->id >> 5) & 63]; q; q = q->next)
        if (q == p) return true;
    return false;
}
// Terminated threads never return from Suspend to remove themselves from
// waiters. Release their critical sections during cleanup. For session
// sections, an identity absent from this process may still belong to another
// process.
bool Application::CriticalThreadGone(unsigned id, bool session) const
{
    if (!id) return false;
    for (const auto &t : threads)
        if (t.id == id) return t.done || !t.fiber;
    if (!session) return true;
    // Session critical sections can belong to foreign threads. Search all
    // process thread tables using session-wide identities. If none contains
    // the owner, it has been removed and the section can be released.
    for (const auto *process : session_processes)
    {
        if (!process || process == this) continue;
        if (process->state == Exited || process->state == Failed) continue;
        for (const auto &t : process->threads)
            if (t.id == id) return t.done || !t.fiber;
    }
    return true;
}
void Application::SweepCriticalWaiters(Critical *p)
{
    if (!p) return;
    const bool session = CriticalSession(p);
    unsigned kept = 0;
    for (unsigned i = 0; i < p->waiting; ++i)
    {
        // The waiter already decremented the counter in EnterMutex.
        if (CriticalThreadGone(p->waiters[i], session)) ++p->data.available;
        else p->waiters[kept++] = p->waiters[i];
    }
    for (unsigned i = kept; i < 16; ++i) p->waiters[i] = 0;
    p->waiting = kept;
    if (!CriticalThreadGone(p->data.owner, session)) return;
    // The owner exited. Release each nesting level, as LeaveMutex would, then
    // wake the next waiter.
    const unsigned depth = p->data.recursion ? p->data.recursion : 1;
    p->data.owner = 0;
    p->data.recursion = 0;
    p->data.available += depth;
    if (p->waiting && (!p->data.available || p->data.available > 0x80000000u))
    {
        p->data.owner = p->waiters[0];
        for (unsigned i = 1; i < p->waiting; ++i) p->waiters[i - 1] = p->waiters[i];
        p->waiters[--p->waiting] = 0;
        p->data.recursion = 1;
    }
}
void Application::WaitCritical(unsigned id)
{
    auto *p = FindCritical(id);
    const unsigned thread = ThreadId();
    if (p) SweepCriticalWaiters(p);
    if (!p || p->waiting == 16)
    {
        Fail("Invalid critical-section wait");
        return;
    }
    p->waiters[p->waiting++] = thread;
    // Every unsuccessful exit removes its waiter entry so WakeCritical cannot
    // grant ownership to a thread that is no longer waiting.
    struct Leave
    {
        Application &app; unsigned id, thread; bool owned = false;
        ~Leave()
        {
            if (owned) return;
            auto *p = app.FindCritical(id);
            if (!p) return;
            for (unsigned i = 0; i < p->waiting; ++i)
                if (p->waiters[i] == thread)
                {
                    for (unsigned j = i + 1; j < p->waiting; ++j) p->waiters[j - 1] = p->waiters[j];
                    p->waiters[--p->waiting] = 0;
                    ++p->data.available;
                    break;
                }
        }
    } leave{*this, id, thread};
    while (state != Failed && state != Exited)
    {
        p = FindCritical(id);
        if (!p)
        {
            Fail("Critical section deleted while a thread was waiting");
            return;
        }
        SweepCriticalWaiters(p);
        if (p->data.owner == thread)
        {
            leave.owned = true;
            return;
        }
        if (!p->data.owner && p->waiting && p->waiters[0] == thread)
        {
            // SweepCriticalWaiters removed the departed owner before this
            // thread joined the list. The first waiter claims the
            // already-adjusted section.
            for (unsigned i = 1; i < p->waiting; ++i) p->waiters[i - 1] = p->waiters[i];
            p->waiters[--p->waiting] = 0;
            p->data.owner = thread;
            p->data.recursion = 1;
            leave.owned = true;
            return;
        }
        auto &t = threads[current_thread];


        t.block = 5;
        t.wait_handle = id;
        // EnterCriticalSection does not enter an owner-draw/message loop while blocked.
        Suspend(Idle);
    }
}
void Application::WakeCritical(unsigned id)
{
    auto *p = FindCritical(id);
    if (!p)
    {
        Fail("Invalid critical-section wake");
        return;
    }
    if (state == Failed || state == Exited)
        return;
    SweepCriticalWaiters(p);
    if (!p->waiting)
    {
        // All waiters exited. Keep the section free; SweepCriticalWaiters
        // already updated the counter.
        p->data.owner = 0;
        p->data.recursion = 0;
        return;
    }
    const unsigned next = p->waiters[0];
    for (unsigned i = 1; i < p->waiting; ++i)
        p->waiters[i - 1] = p->waiters[i];
    --p->waiting;
    // Counter changes already happened in the original fast paths. Transfer ownership once.
    p->data.owner = next;
    p->data.recursion = 1;
}

void Application::FreeCritical()
{
    for (auto &head : critical_buckets)
        while (head)
        {
            auto *next = head->next;
            delete head;
            head = next;
        }
}
struct Application::CriticalEnvironment final : Win98Critical::Environment
{
    Application &app;
    Win98Critical::Mutex invalid;
    explicit CriticalEnvironment(Application &a) : app(a) {}
    Win98Critical::Client ReadClient(unsigned h) override
    {
        Win98Critical::Client c{};
        app.Read(h, &c, sizeof c);
        return c;
    }
    bool ProbeClient(unsigned h) override
    {
        Win98Critical::Client c{};
        return app.cpu->Read(h, &c, sizeof c) == CpuError::Ok;
    }
    void TouchClient(unsigned h) override
    {
        const unsigned value = app.Read32(h);
        app.Write32(h, value);
    }
    void WriteTag(unsigned h, unsigned char value) override { app.Store(h, &value, 1); }
    void WriteObject(unsigned h, unsigned value) override
    {
        app.Write32(h + 4, value);
        // A critical section whose storage lies in a session block is one
        // object for the whole session, because those pages are the same pages
        // in every process. COMCTL32 and SHELL32 keep them in shared data; a second process only ever
        // enters them.
        if (value && app.state != Failed && app.state != Exited && Application::FindSessionBlock(h))
            app.PromoteCritical(value);
    }
    Win98Critical::Mutex *FindMutex(unsigned id) override
    {
        auto *p = app.FindCritical(id);
        return p ? &p->data : nullptr;
    }
    Win98Critical::Mutex &Data(unsigned id) override
    {
        if (auto *p = FindMutex(id))
            return *p;
        app.Fail("Invalid private critical-section object");
        return invalid;
    }
    bool HeapContains(unsigned id) override { return !PrivateMutex(id) && app.FindCritical(id); }
    unsigned CurrentThread() override { return app.ThreadId(); }
    void GlobalLock() override { Win98Critical::EnterMutex(*this, SystemMutex); }
    void GlobalUnlock() override { Win98Critical::LeaveMutex(*this, SystemMutex); }
    unsigned AllocateMutex() override
    {
        if (app.state == Failed || app.state == Exited || next_critical_identity == CriticalRecordBytes / 32)
            return 0;
        const unsigned id = CriticalRecordBase + (next_critical_identity << 5);
        const unsigned offset = id - CriticalRecordBase;
        auto *chunk = CriticalChunk(offset / CriticalChunkBytes, true);
        if (!chunk)
            return 0;
        auto *p = new Critical(reinterpret_cast<Win98Critical::Mutex *>(chunk + offset % CriticalChunkBytes));
        if (!p)
            return 0;
        ++next_critical_identity;
        p->id = id;
        app.MapCriticalChunk(id);
        ++app.critical_serial;
        auto &head = app.critical_buckets[(p->id >> 5) & 63];
        p->next = head;
        head = p;
        return p->id;
    }
    void InitializeMutex(unsigned id) override { Data(id) = Win98Critical::Mutex{}; }
    bool ProcessPresent() override { return true; }
    unsigned CreateProcessList() override { return app.critical_list++; }
    bool BindProcess(unsigned) override { return true; } // Private bucket membership is the process reference.
    void DestroyProcessList(unsigned) override {} // List tokens have no separate allocation; bucket removal releases ownership.
    void DestroyMutex(unsigned id) override
    {
        if (PrivateMutex(id))
            app.Fail("Cannot destroy the private kernel lock");
        // Removing the object below makes blocked fibers runnable so they can report deletion.
    }
    void FreeMutex(unsigned id) override
    {
        if (PrivateMutex(id))
            return;
        auto **link = &app.critical_buckets[(id >> 5) & 63];
        while (*link && (*link)->id != id)
            link = &(*link)->next;
        if (!*link)
        {
            link = &global_critical_buckets[(id >> 5) & 63];
            while (*link && (*link)->id != id) link = &(*link)->next;
        }
        if (*link)
        {
            auto *p = *link;
            *link = p->next;
            delete p;
        }
        else
            app.Fail("Invalid private critical-section free");
    }
    void Wait(unsigned id) override { app.WaitCritical(id); }
    void Wake(unsigned id) override { app.WakeCritical(id); }
    bool Fault(unsigned code) override
    {
        char text[96];
        snprintf(text, sizeof text, "Unhandled critical-section exception %08x", code);
        app.Fail(text);
        return false;
    }
};
unsigned Application::CriticalApi(ApiName name, const unsigned *a)
{
    CriticalEnvironment e(*this);
    if (API_IS(name,"InitializeCriticalSection") || API_IS(name,"InitializeCriticalSectionAndSpinCount"))
        Win98Critical::Initialize(e, a[0]);
    else if (API_IS(name,"MakeCriticalSectionGlobal"))
    {
        // The original detaches process lifetime; the ARM provider moves that
        // same object (including blocked waiters) into the session registry.
        const unsigned id = Read32(a[0] + 4);
        Win98Critical::MakeGlobal(e, a[0]);
        if (state != Failed && state != Exited) PromoteCritical(id);
    }
    else if (API_IS(name,"ReinitializeCriticalSection"))
    {
        // KERNEL32 : when the section is already registered the
        // original only resets its waiting event and returns; otherwise it
        // calls InitializeCriticalSection. NEO98 keeps no
        // separate event object per section, so the registered case is a
        // no-op. Blocked waiters and spin state are not reset.
        if (!FindCritical(Read32(a[0] + 4)))
            Win98Critical::Initialize(e, a[0]);
    }
    else if (API_IS(name,"DeleteCriticalSection"))
        Win98Critical::Delete(e, a[0]);
    else if (API_IS(name,"EnterCriticalSection"))
        Win98Critical::Enter(e, a[0]);
    else if (API_IS(name,"LeaveCriticalSection"))
        Win98Critical::Leave(e, a[0]);
    return 0;
}
void Application::ReleaseHeapMutex(unsigned object)
{
    CriticalEnvironment e(*this);
    Win98Critical::Free(e, object);
}
void Application::KernelPathLock(bool acquire, unsigned id)
{
    if (id > 1) { Fail("Unknown private kernel path lock"); return; }
    const unsigned mutex = id ? PathTaskMutex : PathMutex;
    CriticalEnvironment e(*this);
    if (acquire) Win98Critical::EnterMutex(e, mutex);
    else Win98Critical::LeaveMutex(e, mutex);
}
void Application::KernelThreadLock(bool acquire, bool global)
{
    CriticalEnvironment e(*this);
    const unsigned mutex = global ? SystemMutex : ThreadMutex;
    if (acquire) Win98Critical::EnterMutex(e, mutex);
    else Win98Critical::LeaveMutex(e, mutex);
}
// GetpWin16Lock and _ConfirmWin16Lock (ordinals 93 and 96) still answer,
// because the original exports them. PLAN.md step 8 took the global Win16
// mutex out, so the record is an ordinary per-process system level that only
// a guest that calls EnterSysLevel on it (DDRAW, around its operations) ever
// owns. What the Win16Mutex protected of the screen is the lock of the
// screen's pixels now (src/x86_screen_lock.cpp).
unsigned Application::Win16LockRecord()
{
    if (!win16_lock)
    {
        win16_lock = Allocate(sizeof(Win98Critical::Mutex));
        // An unowned system level, the way KERNEL32 hands it out: type 4 and
        // available. Allocate only zeroes the bytes, and a guest that calls
        // EnterSysLevel on the record reads them.
        const Win98Critical::Mutex unowned;
        if (win16_lock)
            Store(win16_lock, &unowned, sizeof unowned);
    }
    return win16_lock;
}
// The word at fs:[0x1e] stays a per-thread counter of open thunk
// transitions. Ordinals 48, 49, 104 and K32Thk1632Prolog/Epilog keep it; no
// lock hangs on it any more.
unsigned Application::ThunkLockCount()
{
    unsigned short count = 0; Read(Teb() + 0x1e, &count, 2); return count;
}
void Application::ThunkLockCount(unsigned value)
{
    unsigned short count = value; Store(Teb() + 0x1e, &count, 2);
}
void Application::CrtLock(bool acquire, unsigned id)
{
    // Original MSVCRT time/environment, exit, locale and multibyte state
    // use distinct recursive locks 11, 12, 13, 19 and 25.
    const unsigned mutex = id == 0 ? SystemMutex : id == 11 ? CrtTimeMutex : id == 12 ? CrtEnvironmentMutex : id == 13 ? CrtExitMutex : id == 19 ? CrtLocaleMutex : id == 25 ? CrtMultibyteMutex : 0;
    if (!mutex)
    {
        Fail("Unknown private CRT lock");
        return;
    }
    if ((id == 11 || id == 12) && !FindCritical(mutex))
    {
        auto *record = new Critical;
        if (!record) { Fail("CRT private lock allocation failed"); return; }
        record->id = mutex;
        auto &head = critical_buckets[(mutex >> 5) & 63];
        record->next = head;
        head = record;
    }
    CriticalEnvironment e(*this);
    if (acquire)
        Win98Critical::EnterMutex(e, mutex);
    else
        Win98Critical::LeaveMutex(e, mutex);
}
struct Application::TlsEnvironment final : Win98Tls::Environment
{
    Application &app;
    CriticalEnvironment mutex;
    explicit TlsEnvironment(Application &a) : app(a), mutex(a) {}
    void Lock(bool global) override { Win98Critical::EnterMutex(mutex, global ? SystemMutex : TlsMutex); }
    void Unlock(bool global) override { Win98Critical::LeaveMutex(mutex, global ? SystemMutex : TlsMutex); }
    unsigned &Bitmap(unsigned word) override { return app.tls_bitmap[word]; }
    unsigned Read(unsigned index) override { return app.Read32(app.Teb() + 0x88 + index * 4); }
    void Write(unsigned index, unsigned value) override { app.Write32(app.Teb() + 0x88 + index * 4, value); }
    void ClearThreads(unsigned index) override
    {
        for (unsigned i = 0; i < Application::ThreadCapacity; ++i)
            if (app.threads[i].fiber)
            {
                const auto teb = TebOf(i);
                app.Write32(teb + 0x88 + index * 4, 0);
            }
    }
    void Error(unsigned value) override
    {
        app.last_error = value;
        app.Write32(app.Teb() + 0x60, value);
    }
};
// The error word of the running thread lives at TEB+60h, where the guest
// bodies of GetLastError and SetLastError read and write it; the dispatcher
// takes it from there when a gate starts and puts it back when the gate ends.
void Application::LoadError() { last_error = Read32(Teb() + 0x60); }
void Application::StoreError() { Write32(Teb() + 0x60, last_error); }
// the static TLS loader takes its index from the ordinary TlsAlloc,
// so both kinds of thread storage share the 80 slots of the process.
unsigned Application::AllocateTlsSlot()
{
    TlsEnvironment e(*this);
    return Win98Tls::Alloc(e);
}
unsigned Application::TlsApi(ApiName name, const unsigned *a)
{
    TlsEnvironment e(*this);
    if (API_IS(name,"TlsAlloc"))
        return Win98Tls::Alloc(e);
    if (API_IS(name,"TlsFree"))
        return Win98Tls::Free(e, a[0]);
    if (API_IS(name,"TlsSetValue"))
        return Win98Tls::Set(e, a[0], a[1]);
    return Win98Tls::Get(e, a[0]);
}
// The guest code of EnterCriticalSection and LeaveCriticalSection, one copy in
// a page of every process that calls them. It is the uniprocessor path of
// KERNEL32 (EnterMutex, LeaveMutex ) with every case the host
// must see turned into a jump to the ordinary entry of the import: a client
// without tag 4, an object outside the record arena or without the mutex type,
// a section another thread holds, and a leave that has to wake a waiter. Those
// cases take exactly the host path they took before, which also still decides
// every fault. Only eax, ecx, edx and the flags change, as in the original.
// The code is correct only because it runs as one piece. A thread of this port
// changes only where uc_emu_start returns: at a gate, at a fault, or at the
// start of a block after Application::Slice asked for the stop. Slice never
// asks inside this page, nothing here calls a gate, and the only accesses that
// can fault are the two reads of the client record before anything changes. So
// no other guest thread and no host code runs between the test of a record and
// its change.
namespace
{
static_assert(Application::CriticalRecordBase == 0xc8010000u && Application::CriticalRecordBytes == 0x4000000u,
              "the range test below is encoded for these values");
constexpr unsigned char CriticalEnterCode[] = {
    0x8b, 0x44, 0x24, 0x04,                   // 00 mov eax,[esp+4] the client
    0x80, 0x38, 0x04, 0x75, 0x45,             // 04 cmp byte [eax],4; jne 4E
    0x8b, 0x40, 0x04,                         // 09 mov eax,[eax+4] the record
    0x8d, 0x90, 0x00, 0x00, 0xff, 0x37,       // 0C lea edx,[eax-C8010000h]
    0x81, 0xfa, 0x00, 0x00, 0x00, 0x04,       // 12 cmp edx,4000000h
    0x73, 0x34,                               // 18 jae 4E
    0xa8, 0x1f, 0x75, 0x30,                   // 1A test al,1Fh; jnz 4E
    0x80, 0x38, 0x04, 0x75, 0x2b,             // 1E cmp byte [eax],4; jne 4E
    0x64, 0x8b, 0x15, 0x24, 0x00, 0x00, 0x00, // 23 mov edx,fs:[24h] this thread
    0x83, 0x78, 0x10, 0x01, 0x75, 0x10,       // 2A cmp dword [eax+10h],1; jne 40
    0xc7, 0x40, 0x10, 0x00, 0x00, 0x00, 0x00, // 30 mov dword [eax+10h],0 free: take it
    0x89, 0x50, 0x08,                         // 37 mov [eax+8],edx
    0xff, 0x40, 0x04,                         // 3A inc dword [eax+4]
    0xc2, 0x04, 0x00,                         // 3D ret 4
    0x39, 0x50, 0x08, 0x75, 0x09,             // 40 cmp [eax+8],edx; jne 4E
    0xff, 0x48, 0x10,                         // 45 dec dword [eax+10h] held by this thread
    0xff, 0x40, 0x04,                         // 48 inc dword [eax+4]
    0xc2, 0x04, 0x00,                         // 4B ret 4
};                                            // 4E jmp <gate+8>
constexpr unsigned char CriticalLeaveCode[] = {
    0x8b, 0x44, 0x24, 0x04,                   // 00 mov eax,[esp+4]
    0x80, 0x38, 0x04, 0x75, 0x48,             // 04 cmp byte [eax],4; jne 51
    0x8b, 0x40, 0x04,                         // 09 mov eax,[eax+4]
    0x8d, 0x90, 0x00, 0x00, 0xff, 0x37,       // 0C lea edx,[eax-C8010000h]
    0x81, 0xfa, 0x00, 0x00, 0x00, 0x04,       // 12 cmp edx,4000000h
    0x73, 0x37,                               // 18 jae 51
    0xa8, 0x1f, 0x75, 0x33,                   // 1A test al,1Fh; jnz 51
    0x80, 0x38, 0x04, 0x75, 0x2e,             // 1E cmp byte [eax],4; jne 51
    0x8b, 0x48, 0x04,                         // 23 mov ecx,[eax+4] recursion
    0x83, 0xf9, 0x01, 0x77, 0x1c, 0x75, 0x24, // 26 cmp ecx,1; ja 47; jne 51
    0x8b, 0x50, 0x10, 0x42,                   // 2D mov edx,[eax+10h]; inc edx
    0x7e, 0x1e,                               // 31 jle 51 a waiter needs waking
    0xc7, 0x40, 0x04, 0x00, 0x00, 0x00, 0x00, // 33 mov dword [eax+4],0
    0xc7, 0x40, 0x08, 0x00, 0x00, 0x00, 0x00, // 3A mov dword [eax+8],0
    0x89, 0x50, 0x10,                         // 41 mov [eax+10h],edx
    0xc2, 0x04, 0x00,                         // 44 ret 4
    0x49, 0x89, 0x48, 0x04,                   // 47 dec ecx; mov [eax+4],ecx
    0xff, 0x40, 0x10,                         // 4B inc dword [eax+10h]
    0xc2, 0x04, 0x00,                         // 4E ret 4
};                                            // 51 jmp <gate+8>
// GetLastError, SetLastError and TlsGetValue, as KERNEL32 of Win98 has them:
// a few instructions on the thread's own block, no ring change. The error word
// is TEB+60h, the slots start at TEB+88h (TlsEnvironment below), and the
// dispatcher keeps its copy of the word in step with TEB+60h at every gate
// (Application::LoadError, StoreError). TlsGetValue of an index past the 80
// slots takes the gate, which answers ERROR_INVALID_PARAMETER.
constexpr unsigned char GetLastErrorCode[] = {
    0x64, 0xa1, 0x60, 0x00, 0x00, 0x00,       // 00 mov eax,fs:[60h]
    0xc3,                                     // 06 ret
};
constexpr unsigned char SetLastErrorCode[] = {
    0x8b, 0x44, 0x24, 0x04,                   // 00 mov eax,[esp+4]
    0x64, 0xa3, 0x60, 0x00, 0x00, 0x00,       // 04 mov fs:[60h],eax
    0xc2, 0x04, 0x00,                         // 0A ret 4
};
constexpr unsigned char TlsGetValueCode[] = {
    0x8b, 0x44, 0x24, 0x04,                   // 00 mov eax,[esp+4] the index
    0x83, 0xf8, 0x50,                         // 04 cmp eax,50h
    0x73, 0x16,                               // 07 jae 1F
    0x64, 0x8b, 0x04, 0x85, 0x88, 0x00, 0x00, 0x00,               // 09 mov eax,fs:[eax*4+88h]
    0x64, 0xc7, 0x05, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 11 mov dword fs:[60h],0
    0xc2, 0x04, 0x00,                         // 1C ret 4
};                                            // 1F jmp <gate+8>
constexpr unsigned CriticalEnterAt = 0, CriticalLeaveAt = 0x80;
constexpr unsigned GetLastErrorAt = 0x100, SetLastErrorAt = 0x120, TlsGetValueAt = 0x140;
void Relative(unsigned char *at, unsigned from, unsigned to)
{
    const unsigned value = to - (from + 5);
    at[0] = 0xe9;
    for (unsigned i = 0; i < 4; ++i) at[1 + i] = (unsigned char)(value >> (8 * i));
}
} // namespace
// Resolve trivial exports directly to code outside the hooked gate range.
// Every engine can execute these bytes. The ordinary HLT gate remains the
// fallback for invalid TLS indexes and the entry used by a traced run.
// TlsSetValue keeps the host's existing failure path for a protected TEB.
unsigned Application::TrivialProcedure(Import &item, unsigned gate)
{
    if (!cpu || Arm64Process() || !fast_gates || stub_mode || trace_calls || export_census ||
        strcasecmp(item.dll, "KERNEL32.DLL")) return gate;
    const unsigned char *body = nullptr;
    unsigned length = 0, offset = 0, arguments = 0;
    if (!strcmp(item.name, "GetLastError")) {
        body = GetLastErrorCode; length = sizeof GetLastErrorCode; offset = 0x200;
    } else if (!strcmp(item.name, "SetLastError")) {
        body = SetLastErrorCode; length = sizeof SetLastErrorCode; offset = 0x240; arguments = 1;
    } else if (!strcmp(item.name, "TlsGetValue")) {
        body = TlsGetValueCode; length = sizeof TlsGetValueCode; offset = 0x280; arguments = 1;
    }
    if (!body || item.argc != arguments) return gate;
    const unsigned address = TrivialCodeBase + offset;
    // This marker describes the import, not an instruction in its gate.
    if (item.fast_return == TrivialBodyGate && trivial_code_mapped) return address;
    // Keep the first call in the crash history and first-use log, just as
    // the old guest bodies did. Later calls need no hook or host entry.
    constexpr unsigned state_base = TrivialCodeBase + 4096;
    static_assert(TrivialCodeBase + 8192 <= ThreadTebBase, "API pages fit the service reservation");
    if (!trivial_code_mapped) {
        // Public function pointers must pass the guest's pointer probes and
        // VirtualQuery. Internal critical-section pages are not tracked there.
        if (!virtual_memory.Track(TrivialCodeBase, 8192, 4)) return gate;
        if (cpu->Map(TrivialCodeBase, 8192, Prot::Read | Prot::Write) != CpuError::Ok) {
            virtual_memory.Forget(TrivialCodeBase, 8192);
            return gate;
        }
        if (cpu->Protect(TrivialCodeBase, 4096, Prot::Read | Prot::Exec) != CpuError::Ok) {
            cpu->Unmap(TrivialCodeBase, 8192);
            virtual_memory.Forget(TrivialCodeBase, 8192);
            return gate;
        }
        virtual_memory.SetProtection(TrivialCodeBase, 4096, 0x20);
        trivial_code_mapped = true;
    }
    const unsigned flag = state_base + (offset - 0x200) / 16;
    unsigned char code[64] = {
        0x83, 0x3d, 0, 0, 0, 0, 0, // Check whether this export has been called.
        0x75, 0x0f,
        0xc7, 0x05, 0, 0, 0, 0, 1, 0, 0, 0,
    };
    memcpy(code + 2, &flag, 4);
    memcpy(code + 11, &flag, 4);
    Relative(code + 19, address + 19, gate);
    constexpr unsigned prefix = 24;
    static_assert(sizeof TlsGetValueCode + prefix + 5 <= sizeof code, "TLS body fits its slot");
    memcpy(code + prefix, body, length);
    Relative(code + prefix + length, address + prefix + length, gate);
    if (cpu->Write(address, code, prefix + length + 5) != CpuError::Ok ||
        cpu->InvalidateCode(address, sizeof code) != CpuError::Ok) return gate;
    // An older saved state may still have a jump in this gate. The direct
    // body must fall back to a trapping entry, including on native x86.
    const unsigned char trap = 0xf4;
    if (cpu->Write(gate, &trap, 1) != CpuError::Ok ||
        cpu->InvalidateCode(gate, 16) != CpuError::Ok) return gate;
    BindRoute(item);
    item.fast_return = TrivialBodyGate;
    return address;
}
// Gives the gate of EnterCriticalSection or LeaveCriticalSection a jump into
// the guest code above and points that code's way out at the second half of
// the same gate. The engine is at rest: ArmFastGate runs in Dispatch.
bool Application::ArmCriticalBody(Import &item, unsigned gate)
{
    const bool enter = !strcmp(item.name, "EnterCriticalSection");
    // The bodies of the thread block's words.
    const unsigned char *body = nullptr;
    unsigned body_length = 0, body_at = 0, arguments = 1;
    if (!strcmp(item.name, "GetLastError")) { body = GetLastErrorCode; body_length = sizeof GetLastErrorCode; body_at = GetLastErrorAt; arguments = 0; }
    else if (!strcmp(item.name, "SetLastError")) { body = SetLastErrorCode; body_length = sizeof SetLastErrorCode; body_at = SetLastErrorAt; }
    else if (!strcmp(item.name, "TlsGetValue")) { body = TlsGetValueCode; body_length = sizeof TlsGetValueCode; body_at = TlsGetValueAt; }
    if ((!body && !enter && strcmp(item.name, "LeaveCriticalSection")) || item.route != RouteApi ||
        (item.argc & ~0x800000ffu) || (item.argc & 255) != arguments || (item.argc & 0x80000000u))
        return false;
    if (strcasecmp(item.dll, "KERNEL32.DLL")) return false;
    if (!critical_code_mapped)
    {
        if (cpu->Map(CriticalCodeBase, 4096, Prot::Read | Prot::Exec) != CpuError::Ok)
            return false;
        critical_code_mapped = true;
        for (unsigned i = 0; i < sizeof critical_chunks / sizeof *critical_chunks; ++i)
            if (critical_chunks[i]) MapCriticalChunk(CriticalRecordBase + i * CriticalChunkBytes);
    }
    unsigned char code[0x80];
    const unsigned at = CriticalCodeBase + (body ? body_at : enter ? CriticalEnterAt : CriticalLeaveAt);
    const unsigned length = body ? body_length : enter ? sizeof CriticalEnterCode : sizeof CriticalLeaveCode;
    static_assert(sizeof CriticalEnterCode + 5 <= CriticalLeaveAt && sizeof CriticalLeaveCode + 5 <= sizeof code,
                  "the two bodies fit their places");
    static_assert(sizeof GetLastErrorCode + 5 <= SetLastErrorAt - GetLastErrorAt &&
                  sizeof SetLastErrorCode + 5 <= TlsGetValueAt - SetLastErrorAt && sizeof TlsGetValueCode + 5 <= 0x40,
                  "the word bodies fit their places");
    memcpy(code, body ? body : enter ? CriticalEnterCode : CriticalLeaveCode, length);
    Relative(code + length, at + length, gate + 8);
    unsigned char jump[16];
    memset(jump, 0xf4, sizeof jump);
    Relative(jump, gate, at);
    // uc_mem_write does not drop translated blocks.
    return cpu->Write(at, code, length + 5) == CpuError::Ok && cpu->Write(gate, jump, sizeof jump) == CpuError::Ok &&
           Check(cpu->InvalidateCode(gate, 16)) && Check(cpu->InvalidateCode(at, 0x80));
}
// Reads whether EnterCriticalSection or LeaveCriticalSection on this handle
// would take the uncontended path of the original, without changing anything.
// Only then may the gate of the call answer inside uc_emu_start; everything else, including a handle this port does
// not know and every case that raises the original access violation, goes the
// ordinary way through Dispatch.
// EnterMutex waits when the decremented counter is not zero and the
// section belongs to another thread. LeaveMutex wakes a waiter when
// the recursion reaches zero and the incremented counter stays at or below
// zero in signed terms.
bool Application::CriticalUncontended(ApiName name, unsigned handle)
{
    Win98Critical::Client client{};
    if (!handle || !Read(handle, &client, sizeof client) || (client.tag & 255) != 4)
        return false;
    const auto *p = FindCritical(client.object);
    if (!p)
        return false;
    const auto &m = p->data;
    if (API_IS(name, "LeaveCriticalSection"))
    {
        if (m.recursion > 1)
            return true;
        if (m.recursion != 1)
            return false;
        const unsigned available = m.available + 1;
        return available != 0 && available <= 0x80000000u;
    }
    return m.available == 1 || m.owner == ThreadId();
}
} // namespace X86
