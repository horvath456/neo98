// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Critical
{
struct Client
{
    unsigned tag, object;
};
struct Mutex
{
    unsigned type = 4, recursion = 0, owner = 0, reserved_c = 0;
    unsigned available = 1, reserved_14 = 0, process_list = 0, client = 0;
};
static_assert(sizeof(Client) == 8 && sizeof(Mutex) == 32, "Win98 critical-section records");
struct Environment
{
    virtual ~Environment() = default;
    virtual Client ReadClient(unsigned) = 0;
    virtual bool ProbeClient(unsigned) = 0;
    virtual void TouchClient(unsigned) = 0;
    virtual void WriteTag(unsigned, unsigned char) = 0;
    virtual void WriteObject(unsigned, unsigned) = 0;
    virtual Mutex *FindMutex(unsigned) = 0;
    virtual Mutex &Data(unsigned) = 0;
    virtual bool HeapContains(unsigned) = 0;
    virtual unsigned CurrentThread() = 0;
    virtual void GlobalLock() = 0;
    virtual void GlobalUnlock() = 0;
    virtual unsigned AllocateMutex() = 0;
    virtual void InitializeMutex(unsigned) = 0;
    virtual bool ProcessPresent() = 0;
    virtual unsigned CreateProcessList() = 0;
    virtual bool BindProcess(unsigned) = 0;
    virtual void DestroyProcessList(unsigned) = 0;
    virtual void DestroyMutex(unsigned) = 0;
    virtual void FreeMutex(unsigned) = 0;
    virtual void Wait(unsigned) = 0;
    virtual void Wake(unsigned) = 0;
    // False models an unhandled exception; true resumes at the original continuation.
    virtual bool Fault(unsigned code) = 0;
};
// KERNEL32: guarded client/object lookup, including the page-offset test.
inline bool Valid(Environment &e, unsigned h)
{
    if (!e.ProbeClient(h))
        return false;
    const auto c = e.ReadClient(h);
    if ((c.tag & 255) != 4)
        return false;
    const auto *m = e.FindMutex(c.object);
    return m && !((m->client ^ h) & 0xfff) && (m->type & 255) == 4 && e.HeapContains(c.object);
}
// uniprocessor fast path; the VxD wait service completes contended acquisition.
inline void EnterMutex(Environment &e, unsigned object)
{
    const unsigned thread = e.CurrentThread();
    auto &m = e.Data(object);
    if (!--m.available)
    {
        m.owner = thread;
        ++m.recursion;
    }
    else if (m.owner == thread)
        ++m.recursion;
    else
        e.Wait(object);
}
// Incrementing the count past the signed maximum does not wake a waiter.
inline void LeaveMutex(Environment &e, unsigned object)
{
    auto &m = e.Data(object);
    if (--m.recursion)
        ++m.available;
    else
    {
        m.owner = 0;
        ++m.available;
        if (!m.available || m.available > 0x80000000u)
            e.Wake(object);
    }
}
// close process reference, guarded backlink removal, destroy and free.
// The original SEH machinery is represented by ProbeClient at the guarded access.
inline void Free(Environment &e, unsigned object)
{
    e.GlobalLock();
    if (const unsigned handle = e.Data(object).process_list)
        e.DestroyProcessList(handle);
    const unsigned h = e.Data(object).client;
    if (h && e.ProbeClient(h))
    {
        const auto c = e.ReadClient(h);
        if ((c.tag & 255) == 4 && c.object == object)
        {
            e.WriteTag(h, 0);
            e.WriteObject(h, 0);
            e.Data(object).client = 0;
        }
    }
    e.DestroyMutex(object);
    e.FreeMutex(object);
    e.GlobalUnlock();
}
// MakeCriticalSectionGlobal, KERNEL32.
// Kernel mutex objects already inhabit shared kernel memory in Win98. Removing
// their process-list link prevents process teardown from destroying the object.
// Client tag/backlink, ownership, recursion and contention counters are untouched.
inline void MakeGlobal(Environment &e, unsigned h)
{
    e.GlobalLock();
    auto &m = e.Data(e.ReadClient(h).object);
    if (const unsigned handle = m.process_list)
    {
        m.process_list = 0;
        e.DestroyProcessList(handle);
    }
    e.GlobalUnlock();
}
// Public DeleteCriticalSection.
inline void Delete(Environment &e, unsigned h)
{
    if ((e.ReadClient(h).tag & 255) != 4 && !e.Fault(0xc0000005))
        return;
    e.GlobalLock();
    Free(e, e.ReadClient(h).object);
    e.GlobalUnlock();
}
// Public InitializeCriticalSection, and 's ignored spin argument.
inline void Initialize(Environment &e, unsigned h)
{
    e.TouchClient(h);
    e.GlobalLock();
    if (Valid(e, h))
        Delete(e, h);
    unsigned object = e.AllocateMutex();
    if (object)
        e.InitializeMutex(object);
    e.WriteObject(h, object);
    if (object && e.ProcessPresent())
    {
        const auto handle = e.CreateProcessList();
        e.Data(object).process_list = handle;
        if (handle && e.BindProcess(handle))
        {
            e.WriteTag(h, 4);
            e.Data(object).client = h;
        }
        else
        {
            Free(e, object);
            object = 0;
        }
    }
    e.GlobalUnlock();
    if (!object)
        e.Fault(0xc0000005);
}
// Public EnterCriticalSection / LeaveCriticalSection.
inline void Enter(Environment &e, unsigned h)
{
    if ((e.ReadClient(h).tag & 255) != 4 && !e.Fault(0xc0000005))
        return;
    EnterMutex(e, e.ReadClient(h).object);
}
inline void Leave(Environment &e, unsigned h)
{
    if ((e.ReadClient(h).tag & 255) == 4)
        LeaveMutex(e, e.ReadClient(h).object);
}
} // namespace Win98Critical
