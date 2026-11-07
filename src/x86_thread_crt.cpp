// SPDX-License-Identifier: GPL-2.0-or-later
// Native process providers for the original MSVCRT per-thread data lifecycle.
#include "win98_thread_crt_family.h"
#include "x86_app.h"
#include "win98_crt_time.h"
#include "win98_crt_system_clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <new>
#include <string.h>
namespace X86
{
struct Application::CrtThreads
{
    Application &app;
    Win98ThreadCrt::State core;
    Win98CrtTime::State time_state;
    unsigned time_data=0,time_ftime_minute=0;
    unsigned long long time_clock_start=0;
    int time_daylight=0,time_ftime_daylight=-1;
    Win98Time::SystemTime time_previous{};
    char*time_environment=nullptr;
    struct Allocation
    {
        unsigned pointer, owner;
        Allocation *next;
    };
    Allocation *allocations = nullptr;
    unsigned live_allocations = 0;
    bool tls_allocated = false, locks_initialized = false;
    static constexpr unsigned TlsToken = 0x435254;
    explicit CrtThreads(Application &a) : app(a) {}

    unsigned Read(unsigned p) { return app.Read32(p); }
    unsigned ReadByte(unsigned p)
    {
        unsigned char value = 0;
        app.Read(p, &value, 1);
        return value;
    }
    void WriteByte(unsigned p, unsigned value)
    {
        const unsigned char byte = value;
        app.Store(p, &byte, 1);
    }
    void Write(unsigned p, unsigned value) { app.Write32(p, value); }
    unsigned GetLastError() { return app.last_error; }
    void SetLastError(unsigned value) { app.last_error = value; }
    unsigned CurrentThreadId() { return app.ThreadId(); }
    unsigned CurrentThreadHandle() { return app.ThreadApi("GetCurrentThread", nullptr); }
    struct PreserveError
    {
        Application &app;
        unsigned error;
        explicit PreserveError(Application &a) : app(a), error(a.last_error) {}
        ~PreserveError() { app.last_error = error; }
    };
    Allocation *Find(unsigned p)
    {
        for (auto *n = allocations; n; n = n->next)
            if (n->pointer == p) return n;
        return nullptr;
    }
    unsigned Calloc(unsigned count, unsigned size)
    {
        // Calling CrtApi(calloc) here would recurse through errno on failure.
        PreserveError preserve(app);
        if (count && size > ~0u / count) return 0;
        auto *node = static_cast<Allocation *>(malloc(sizeof(Allocation)));
        if (!node) return 0;
        *node = Allocation{};
        if (!app.crt_heap)
        {
            const unsigned args[] = {0, 0, 0};
            app.crt_heap = app.HeapApi("HeapCreate", args);
        }
        const unsigned args[] = {app.crt_heap, 8, count * size};
        const unsigned p = app.crt_heap ? app.HeapApi("HeapAlloc", args) : 0;
        if (!p) { free(node); return 0; }
        node->pointer = p;
        node->owner = CurrentThreadId();
        node->next = allocations;
        allocations = node;
        ++live_allocations;
        return p;
    }
    void Free(unsigned p)
    {
        if (!p) return;
        PreserveError preserve(app);
        // Optional PTD buffers also use the CRT heap, but have no PTD record.
        Allocation **link = &allocations;
        while (*link && (*link)->pointer != p) link = &(*link)->next;
        if (*link)
        {
            auto *node = *link;
            *link = node->next;
            free(node);
            --live_allocations;
        }
        const unsigned args[] = {app.crt_heap, 0, p};
        app.HeapApi("HeapFree", args);
    }
    unsigned TlsAlloc()
    {
        if (tls_allocated) { app.last_error = 8; return ~0u; }
        tls_allocated = true;
        return TlsToken;
    }
    bool ValidTls(unsigned index) const { return tls_allocated && index == TlsToken; }
    void TlsFree(unsigned index)
    {
        if (!ValidTls(index)) { app.last_error = 87; return; }
        for (auto &thread : app.threads) thread.crt_tls = 0;
        tls_allocated = false;
    }
    unsigned TlsGet(unsigned index)
    {
        if (!ValidTls(index)) { app.last_error = 87; return 0; }
        app.last_error = 0;
        return app.threads[app.current_thread].crt_tls;
    }
    bool TlsSet(unsigned index, unsigned data)
    {
        if (!ValidTls(index)) { app.last_error = 87; return false; }
        app.threads[app.current_thread].crt_tls = data;
        if (auto *node = Find(data)) node->owner = CurrentThreadId();
        return true;
    }
    void InitializeLocks()
    {
        // The Application already owns the native kernel/CRT locks. Their
        // allocation and final release bracket all heaps and CRT providers.
        locks_initialized = true;
    }
    void TerminateLocks() { locks_initialized = false; }
    unsigned CreateCrtThread(unsigned security, unsigned stack, unsigned ptd,
                             unsigned flags, unsigned tid_out)
    {
        const unsigned args[] = {security, stack, 0, 0, flags, tid_out};
        const unsigned handle = app.CreateThread(args, ptd);
        if (handle)
        {
            auto *slot = app.FindKernelHandle(handle);
            if (auto *node = Find(ptd)) node->owner = app.threads[slot->object].id;
        }
        return handle;
    }
    unsigned CreateLegacyThread(unsigned stack, unsigned ptd)
    {
        const unsigned handle = CreateCrtThread(0, stack, ptd, 4, ptd);
        if (handle) app.threads[app.FindKernelHandle(handle)->object].crt_legacy = true;
        return handle;
    }
    unsigned ResumeThread(unsigned handle) { return app.ThreadApi("ResumeThread", &handle); }
    void CloseThread(unsigned handle) { app.ThreadApi("CloseHandle", &handle); }
    bool InvokeLegacyThread(unsigned function, unsigned argument)
    {
        unsigned ignored = 0;
        // Call restores the full caller context, so cdecl and stdcall callees
        // share the argument frame without inventing a guest return trampoline.
        return InvokeThread(function, argument, ignored);
    }
    void InvokeNoArgs(unsigned hook) { app.Call(hook, nullptr, 0); }
    bool InvokeThread(unsigned function, unsigned argument, unsigned &result)
    {
        const bool kept = !app.depth && !app.pending &&
            app.PushHostFrame(Application::HostCrtThread, app.threads[app.current_thread].crt_legacy);
        if (kept) app.GiveTail(Application::TailRecord);
        result = app.Call(function, &argument, 1);
        if (kept) app.PopHostFrame();
        return app.state != Failed && app.state != Exited && !app.threads[app.current_thread].done;
    }
    void ExitThread(unsigned result) { app.ThreadApi("ExitThread", &result); }
    void Fatal(unsigned code)
    {
        char message[80];
        snprintf(message, sizeof message, "Original CRT fatal runtime error %u", code);
        app.Fail(message);
    }
};

#include "x86_crt_time.inc"
#include "x86_crt_system_clock.inc"

bool Application::InitializeThreadCrt()
{
    if (crt_threads) return true;
    void *storage = malloc(sizeof(CrtThreads));
    if (!storage) { Fail("CRT thread state allocation failed"); return false; }
    crt_threads = new (storage) CrtThreads(*this);
    // Native private TLS replaces an OS slot. action_table=0 explicitly means
    // that CRT signal dispatch is not ported; the optional FP hooks are unset.
    if (!Win98ThreadCrt::Initialize(*crt_threads, crt_threads->core))
    {
        Fail("Original CRT thread initialization failed");
        return false;
    }
    crt_threads->time_clock_start=Win98CrtSystemClock::Start(CrtClockValue());
    return state != Failed;
}
unsigned Application::CrtErrno()
{
    if (!crt_threads) { Fail("CRT thread state is not initialized"); return 0; }
    return Win98ThreadCrt::Errno(*crt_threads, crt_threads->core);
}
void Application::SetCrtErrno(unsigned value)
{
    if (const unsigned p = CrtErrno()) Write32(p, value);
}
unsigned Application::CrtThreadApi(ApiName name, const unsigned *a)
{
    if (API_IS(name,"_errno")) return CrtErrno();
    if (!crt_threads) { Fail("CRT thread state is not initialized"); return 0; }
    auto &e = *crt_threads;
    switch (name.id)
    {
    API_CASE("__threadid") return Win98ThreadCrt::ThreadId(e);
    API_CASE("__threadhandle") return Win98ThreadCrt::ThreadHandle(e);
    API_CASE("__doserrno") return Win98ThreadCrt::DosErrno(e, e.core);
    API_CASE("rand") return Win98ThreadCrt::Rand(e, e.core);
    API_CASE("srand") { Win98ThreadCrt::Srand(e, e.core, a[0]); return 0; }
    API_CASE("strtok") return Win98ThreadCrt::Strtok(e, e.core, a[0], a[1]);
    API_CASE("_beginthread") return Win98ThreadCrt::Begin(e, e.core, a[0], a[1], a[2]);
    API_CASE("_endthread") { Win98ThreadCrt::End(e, e.core); return 0; }
    API_CASE("_beginthreadex")
        return Win98ThreadCrt::BeginEx(e, e.core, a[0], a[1], a[2], a[3], a[4], a[5]);
    API_CASE("_endthreadex")
    {
        Win98ThreadCrt::EndEx(e, e.core, a[0]);
        return 0;
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}
void Application::RunCrtThread()
{
    auto &thread = threads[current_thread];
    const unsigned data = thread.crt_start_data;
    thread.crt_start_data = 0;
    if (data && crt_threads)
    {
        if (thread.crt_legacy) Win98ThreadCrt::Run(*crt_threads, crt_threads->core, data);
        else Win98ThreadCrt::RunEx(*crt_threads, crt_threads->core, data);
    }
}
void Application::FinishCrtThread(unsigned result, bool legacy)
{
    if (!crt_threads || state == Failed || state == Exited || threads[current_thread].done) return;
    if (legacy) Win98ThreadCrt::End(*crt_threads, crt_threads->core);
    else Win98ThreadCrt::EndEx(*crt_threads, crt_threads->core, result);
}
void Application::DetachCrtThread()
{
    if (crt_threads)
        Win98ThreadCrt::FreeData(*crt_threads, crt_threads->core, 0);
}
void Application::ReleaseCrtThreadData(unsigned index)
{
    if (!crt_threads) return;
    auto &e = *crt_threads;
    const unsigned previous_thread = current_thread, previous_error = last_error;
    current_thread = index;
    const unsigned owner = threads[index].id;
    // These records include a never-started PTD and PTDs displaced by a DLL's
    // lazy GetData before RunEx. The original RunEx does not free the old TLS.
    for (;;)
    {
        auto *node = e.allocations;
        while (node && node->owner != owner) node = node->next;
        if (!node) break;
        const unsigned p = node->pointer;
        Win98ThreadCrt::FreeData(e, e.core, p);
        // An aborted initialization may leave no usable TLS state. Its owned
        // allocation must still be reclaimed before the process heap closes.
        if (e.Find(p)) e.Free(p);
    }
    threads[index].crt_tls = threads[index].crt_start_data = 0;
    current_thread = previous_thread;
    last_error = previous_error;
}
void Application::FreeThreadCrt()
{
    if (!crt_threads) return;
    for (unsigned i = 0; i < ThreadCapacity; ++i) ReleaseCrtThreadData(i);
    auto &e = *crt_threads;
    // Native ownership also covers failed process attachment before publication.
    while (e.allocations) e.Free(e.allocations->pointer);
    Win98ThreadCrt::Terminate(e, e.core);
    free(crt_threads->time_environment);
    crt_threads->~CrtThreads();
    free(crt_threads);
    crt_threads = nullptr;
}
} // namespace X86
