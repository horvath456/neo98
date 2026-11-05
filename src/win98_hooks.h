// SPDX-License-Identifier: GPL-2.0-or-later
// USER hook chains. Native handles and task records replace Win16 heap offsets
// and selectors.
#pragma once
namespace Win98Hooks
{
using U = unsigned;
struct Record
{
    U next = 0;
    int type = 0;
    U task = 0, flags = 0, procedure = 0, active = 0;
};
struct Environment
{
    virtual bool Valid(U handle) const = 0;
    virtual Record Read(U handle) const = 0;
    virtual void Write(U handle, const Record &) = 0;
    virtual U Head(int type, U task) const = 0;
    virtual void SetHead(int type, U task, U handle) = 0;
    virtual U CurrentTask() const = 0;
    virtual U TaskFlags(U task) const = 0;
    virtual U CurrentHook(U task) const = 0;
    virtual void SetCurrentHook(U task, U handle) = 0;
    virtual bool DebugPresent(U task) const = 0;
    virtual U DebugHook(U handle, int code, U wp, U lp) = 0;
    virtual U Callback(U handle, U procedure, int code, U wp, U lp) = 0;
    virtual void Dispose(U handle) = 0;
    virtual bool HooksDisabled() const = 0;
};
// thread chain precedes the global chain.
inline U First(Environment &e, int type)
{
    const U h = e.Head(type, e.CurrentTask());
    return h ? h : e.Head(type, 0);
}
// the last thread hook falls through to its global chain.
inline U Next(Environment &e, U h)
{
    const auto r = e.Read(h);
    return !r.next && r.task ? e.Head(r.type, 0) : r.next;
}
// publish a fully initialized record at the head of its chain.
inline void Link(Environment &e, U h)
{
    auto r = e.Read(h);
    r.next = e.Head(r.type, r.task);
    e.SetHead(r.type, r.task, h);
    e.Write(h, r);
}
// same-process non-journal branch. Module unloading, journal playback
// and the Win16 heap's one-record cache belong to providers outside this port.
inline U Remove(Environment &e, U h)
{
    if (!e.Valid(h))
        return 0;
    auto r = e.Read(h);
    if (r.active)
    {
        r.procedure = 0;
        e.Write(h, r);
        return 1;
    }
    U previous = e.Head(r.type, r.task);
    if (previous == h)
        e.SetHead(r.type, r.task, r.next);
    else
    {
        while (previous)
        {
            auto p = e.Read(previous);
            if (p.next == h)
            {
                p.next = r.next;
                e.Write(previous, p);
                break;
            }
            previous = p.next;
        }
        if (!previous)
            return 0;
    }
    e.Dispose(h);
    return 1;
}
// Journal arbitration and cross-process callback address resolution
// are excluded; the application adapter rejects those registrations.
inline U Dispatch(Environment &e, U h, int code, U wp, U lp)
{
    for (; h; h = Next(e, h))
    {
        auto r = e.Read(h);
        if (!r.procedure)
            continue;
        const U task = e.CurrentTask();
        const U flags = e.TaskFlags(task);
        if (flags & 0x800)
            return 0;
        if ((r.flags & 4) && (flags & 4))
            continue;
        ++r.active;
        e.Write(h, r);
        U result = 0;
        if (r.type != 9 && e.DebugPresent(task))
            result = e.DebugHook(h, code, wp, lp);
        if (!result && e.Read(h).procedure)
        {
            const U old = e.CurrentHook(task);
            e.SetCurrentHook(task, h);
            result = e.Callback(h, e.Read(h).procedure, code, wp, lp);
            e.SetCurrentHook(task, old);
        }
        if (e.Valid(h))
        {
            r = e.Read(h);
            --r.active;
            e.Write(h, r);
            if (!r.procedure)
                Remove(e, h);
        }
        return result;
    }
    return 0;
}
// Public Win32 thunk argument conversion is outside this core.
inline U CallNext(Environment &e, U h, int code, U wp, U lp)
{
    return e.Valid(h) ? Dispatch(e, Next(e, h), code, wp, lp) : 0;
}
// The original tests AX before trying WH_MSGFILTER.
inline U Filter(Environment &e, U message, int code)
{
    if (e.HooksDisabled())
        return 0;
    const U result = Dispatch(e, First(e, 6), code, 0, message);
    return (result & 0xffff) ? result : Dispatch(e, First(e, -1), code, 0, message);
}
} // namespace Win98Hooks
