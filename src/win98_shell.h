// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Shell
{
// Native SHELL.DLL. Window handles are widened at the
// ARM boundary; counters, flags, message numbers and return AX remain WORDs.
struct State
{
    unsigned count = 0, hook = 0, message = 0, created = 0, destroyed = 0, activate = 0;
    unsigned shell = 0, taskman = 0, pending = 0, dirty = 0, windows[10] = {};
};
struct Environment
{
    virtual unsigned RegisterMessage(unsigned kind) = 0;
    virtual unsigned Install() = 0;
    virtual void Unhook(unsigned) = 0;
    virtual bool IsWindow(unsigned) = 0;
    virtual bool Post(unsigned hwnd, unsigned message, unsigned wp, unsigned lp) = 0;
    virtual unsigned Send(unsigned hwnd, unsigned message, unsigned wp, unsigned lp) = 0;
    virtual unsigned Next(unsigned hook, unsigned code, unsigned wp, unsigned lp) = 0;
    virtual unsigned AllocateInfo() = 0;
    virtual unsigned PrepareInfo(unsigned allocation, unsigned hwnd) = 0;
    virtual void CopyInfo(unsigned destination, unsigned allocation) = 0;
    virtual void FreeInfo(unsigned allocation) = 0;
};
inline int Signed(unsigned word) { return word & 0x8000 ? int(word & 65535) - 65536 : int(word & 65535); }
inline void Reset(State &s, Environment &e)
{
    if (s.hook)
    {
        e.Unhook(s.hook);
        s.hook = 0;
    }
}
inline unsigned Remove(State &s, Environment &e, unsigned hwnd)
{
    if (!s.count)
        return 0;
    for (unsigned i = 0; i < s.count; ++i)
        if (s.windows[i] == hwnd)
        {
            --s.count;
            if (s.count != i)
                s.windows[i] = s.windows[s.count];
            break;
        }
    if (!s.count)
    {
        e.Unhook(s.hook);
        s.hook = 0;
    }
    return 1;
}
inline unsigned Add(State &s, Environment &e, unsigned hwnd, unsigned ax)
{
    if (s.count >= 10)
        return 0;
    if (!s.count || s.taskman == hwnd)
    {
        Reset(s, e);
        s.message = e.RegisterMessage(0) & 65535;
        s.hook = e.Install();
        ax = (s.hook | (s.hook >> 16)) & 65535;
        if (!ax)
            return 0;
    }
    if (s.shell == hwnd && !s.created)
    {
        s.created = e.RegisterMessage(1) & 65535;
        s.destroyed = e.RegisterMessage(2) & 65535;
        ax = s.activate = e.RegisterMessage(3) & 65535;
    }
    s.windows[s.count++] = hwnd;
    return ax;
}
inline unsigned Register(State &s, Environment &e, unsigned hwnd, unsigned flags)
{
    flags &= 65535;
    switch (flags)
    {
    case 0:
        if (s.shell == hwnd)
            s.shell = 0;
        else if (s.taskman == hwnd)
            s.taskman = 0;
        return Remove(s, e, hwnd);
    case 2:
        s.shell = hwnd;
        break;
    case 3:
        s.taskman = hwnd;
        s.pending = 0;
        break;
    case 4:
        return !s.taskman;
    case 5:
        if (Signed(s.pending) > 0)
        {
            s.pending = (s.pending - 1) & 65535;
            if (s.dirty && Signed(s.pending) < 5 && e.Post(s.taskman, s.message, 0x42a, ~0u))
                s.dirty = 0;
        }
        return 1;
    }
    // AX survives the original Add helper if no import overwrites it.
    return Add(s, e, hwnd, ((flags == 2 || flags == 3) ? hwnd : flags - 5) & 65535);
}
inline void TaskmanPost(State &s, Environment &e, unsigned hwnd, unsigned code, unsigned wp)
{
    if (Signed(s.pending) < 50 && e.Post(hwnd, s.message, code, wp))
        s.pending = (s.pending + 1) & 65535;
    else
        s.dirty = 1;
}
inline void Broadcast(State &s, Environment &e, unsigned code, unsigned wp, unsigned lp)
{
    code &= 65535;
    if (code == 3)
    {
        if (s.shell)
        {
            e.Post(s.shell, s.activate, 0, 0);
            return;
        }
        if (wp == 1 && s.taskman)
        {
            Remove(s, e, s.taskman);
            Reset(s, e);
            return;
        }
    }
    if (code == 7 && s.taskman)
    {
        TaskmanPost(s, e, s.taskman, code, wp);
        return;
    }
    for (unsigned i = 0; i < s.count; ++i)
    {
        const unsigned hwnd = s.windows[i];
        // Original asynchronous loop skips the slot swapped in by Remove.
        if (!e.IsWindow(hwnd))
            Remove(s, e, hwnd);
        else if (hwnd == s.shell)
        {
            if (code == 1 || code == 2)
                e.Post(hwnd, code == 1 ? s.created : s.destroyed, wp, 0);
        }
        else
        {
            if (code == 4 && lp)
                code |= 0x8000;
            if (hwnd == s.taskman)
                TaskmanPost(s, e, hwnd, code, wp);
            else
                e.Post(hwnd, s.message, code, wp);
        }
    }
}
inline unsigned Send(State &s, Environment &e, unsigned code, unsigned wp, unsigned lp)
{
    unsigned result = 0, allocation = 0, forwarded = wp;
    if (code == 5)
    {
        allocation = e.AllocateInfo();
        if (!allocation)
            return 0;
        forwarded = e.PrepareInfo(allocation, wp);
    }
    for (unsigned i = 0; i < s.count;)
    {
        const unsigned hwnd = s.windows[i];
        if (!e.IsWindow(hwnd))
            Remove(s, e, hwnd);
        else
        {
            result |= e.Send(hwnd, s.message, code, forwarded);
            ++i;
        }
    }
    if (code == 5)
    {
        e.CopyInfo(lp, allocation);
        e.FreeInfo(allocation);
    }
    return result;
}
inline unsigned Hook(State &s, Environment &e, unsigned code, unsigned wp, unsigned lp)
{
    code &= 65535;
    if (code == 5)
        return Send(s, e, code, wp, lp);
    if (code == 6 || code == 7)
    {
        Broadcast(s, e, code == 6 && lp ? 0x8006 : code, wp, lp);
        return 1;
    }
    Broadcast(s, e, code, wp, lp);
    // The original leaves DX unspecified when its hook disappears here.
    return s.hook ? e.Next(s.hook, code, wp, lp) : 0;
}
} // namespace Win98Shell
