// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Activation
{
struct Window
{
    unsigned next = 0, first = 0, parent = 0, owner = 0, popup = 0, task = 0, style = 0, extended = 0,
             flags = 0;
};
struct Task
{
    unsigned queue = 0, flags14 = 0, flags15 = 0;
};
struct Queue
{
    unsigned active = 0, previous = 0, focus = 0, flags = 0;
};
struct Globals
{
    unsigned desktop = 0, foreground = 0, excluded = 0, modal = 0;
    unsigned special_mode = 0, suppress_app = 0, previous_queue = 0, accessibility = 0, palette = 0,
             transitions = 0;
};
struct Environment
{
    Globals &globals;
    explicit Environment(Globals &state) : globals(state) {}
    virtual Window Read(unsigned hwnd) const = 0;
    virtual bool Valid(unsigned hwnd) const = 0;
    virtual unsigned CurrentTask() const = 0;
    virtual Task ReadTask(unsigned task) const = 0;
    virtual Queue ReadQueue(unsigned queue) const = 0;
    virtual void WriteTask(unsigned task, const Task &) = 0;
    virtual void WriteQueue(unsigned queue, const Queue &) = 0;
    virtual void WriteWindow(unsigned hwnd, const Window &) = 0;
    virtual unsigned Capacity() const = 0;
    virtual bool HookPresent() = 0;
    virtual bool Hook(unsigned hwnd, unsigned mouse, unsigned previous) = 0;
    // Original WORD wParam and packed Win16 lParam, exposed without truncating native HWNDs.
    virtual unsigned Message(unsigned hwnd, unsigned message, unsigned wparam, unsigned other,
                             unsigned iconic) = 0;
    virtual bool TaskValid(unsigned task) = 0;
    virtual void InputTask(unsigned task) = 0;
    virtual void Accessibility(unsigned hwnd) = 0;
    virtual void SyncQueue(unsigned queue) = 0;
    virtual void SpecialWindow(unsigned hwnd) = 0;
    virtual void Position(unsigned hwnd, unsigned after, unsigned flags) = 0;
    virtual void AppMessage(unsigned task, unsigned other_task, bool active) = 0;
    virtual void Activated(unsigned hwnd) = 0;
    virtual void Focus(unsigned hwnd, unsigned task) = 0;
    virtual void Foreground(unsigned hwnd) = 0;
    virtual void Restore(unsigned hwnd, unsigned task, unsigned queue) = 0;
    virtual unsigned Show(unsigned hwnd, unsigned command) = 0;
};
// USER. WS_CHILD without WS_POPUP follows the parent.
inline unsigned Root(const Environment &e, unsigned h)
{
    for (unsigned n = 0; h && n < e.Capacity() && (e.Read(h).style & 0xc0000000) == 0x40000000; ++n)
        h = e.Read(h).parent;
    return h;
}
// USER: owner ancestry is separate from the parent hierarchy.
inline unsigned OwnerRoot(const Environment &e, unsigned h)
{
    for (unsigned n = 0; h && n < e.Capacity() && e.Read(h).owner; ++n)
        h = e.Read(h).owner;
    return h;
}
inline unsigned Popup(const Environment &e, unsigned h) { return e.Read(h).popup ? e.Read(h).popup : h; }
inline unsigned Iconic(const Environment &e, unsigned h) { return (e.Read(h).style & 0x20000000) != 0; }
inline unsigned Zoomed(const Environment &e, unsigned h) { return (e.Read(h).style & 0x01000000) != 0; }
inline unsigned Previous(const Environment &e, unsigned stop, unsigned h)
{
    unsigned previous = 0;
    for (unsigned current = e.Read(e.Read(h).parent).first, n = 0;
         current && current != stop && n < e.Capacity(); ++n)
    {
        previous = current;
        current = e.Read(current).next;
    }
    return previous;
}
// USER, the next/previous eligible task-switch candidate.
inline unsigned Candidate(const Environment &e, unsigned h, unsigned backward, unsigned skip_topmost)
{
    const unsigned start = Root(e, h);
    unsigned current = start;
    for (unsigned n = 0; current && n <= e.Capacity(); ++n)
    {
        if (backward & 65535)
        {
            current = Previous(e, current, current);
            if (!current)
                current = Previous(e, 0, e.Read(e.globals.desktop).first);
        }
        else
        {
            current = e.Read(current).next;
            if (!current)
                current = e.Read(e.globals.desktop).first;
        }
        if (!current || current == start)
            return 0;
        const auto node = e.Read(current);
        if (current == e.globals.excluded || !(node.style & 0x10000000))
            continue;
        if ((skip_topmost & 65535) && ((node.extended & 8) || (node.flags & 0x10000000)))
            continue;
        if (e.Read(node.popup).style & 0x08000000)
            continue;
        if (current == e.Read(OwnerRoot(e, current)).popup)
            return current;
    }
    return 0;
}
// USER: a window that cannot take the activation, because it is null,
// gone, hidden or disabled.
inline bool Refuses(const Environment &e, unsigned h)
{
    return !h || !e.Valid(h) || (e.Read(h).style & 0x18000000) != 0x10000000;
}
// USER: the first top-level window after `after` (from the top of the
// z-order when null) whose owner is `owner`.
inline unsigned OwnedAfter(const Environment &e, unsigned after, unsigned owner)
{
    unsigned h = after ? e.Read(after).next : e.Read(e.globals.desktop).first;
    for (unsigned n = 0; h && n < e.Capacity(); ++n, h = e.Read(h).next)
        if (e.Read(h).owner == owner)
            return h;
    return 0;
}
// USER: the window after h in a walk over the owner tree of the
// top-level windows: h's highest owned window, else the next window after h
// with h's owner, else the same one owner level up. After the last unowned
// window the walk starts again with the first unowned one; null starts it.
inline unsigned NextOwned(const Environment &e, unsigned h)
{
    unsigned after = 0, owner = h;
    for (unsigned n = 0; n <= e.Capacity(); ++n)
    {
        if (const unsigned next = OwnedAfter(e, after, owner))
            return next;
        after = owner;
        if (!owner)
            break;
        owner = e.Read(after).owner;
    }
    return OwnedAfter(e, 0, 0);
}
// USER: the window before h in the same walk.
inline unsigned PreviousOwned(const Environment &e, unsigned h)
{
    unsigned before = 0;
    for (unsigned n = 0; n <= 2 * e.Capacity(); ++n)
    {
        const unsigned current = NextOwned(e, before);
        if (current == h && before)
            return before;
        before = current;
        if (!current)
            break;
    }
    return before;
}
// USER: the next top-level window after `start` in that walk that is
// visible, enabled and not `skip`; bit 1 of `flags` walks backwards, bit 2
// passes over WS_EX_TOOLWINDOW. The walk ends at the top-level ancestor of
// `start`.
inline unsigned NextTop(const Environment &e, unsigned start, unsigned skip, unsigned flags)
{
    unsigned stop = 0, h = start;
    bool test = false;
    if (!start)
    {
        h = stop = NextOwned(e, 0);
        test = true;
    }
    else
    {
        stop = start;
        for (unsigned n = 0; stop && n < e.Capacity() && e.Read(stop).parent != e.globals.desktop; ++n)
            stop = e.Read(stop).parent;
    }
    for (unsigned n = 0; n <= 2 * e.Capacity(); ++n)
    {
        if (!test)
        {
            h = flags & 1 ? PreviousOwned(e, h) : NextOwned(e, h);
            if (!h || h == stop)
                return 0;
        }
        test = false;
        if (h == skip)
            continue;
        const auto node = e.Read(h);
        if ((node.style & 0x18000000) != 0x10000000)
            continue;
        if ((flags & 2) && (node.extended & 0x80))
            continue;
        return h;
    }
    return 0;
}
// USER, the choice ActivateWindow makes before it activates.
// Code 1 (SetActiveWindow) and 6 take the window as it is; 2 and 4 take it
// when it can be active; 3 (a window that hides, dies or minimizes) starts
// with the owner of a pop-up. Then comes the window that was active before
// in the queue that had the foreground (ds:152h, its queue word 24h), then
// the next top-level window (first without tool windows) or rather
// its last active pop-up. Null means there is nothing to activate.
inline unsigned Target(const Environment &e, unsigned h, unsigned code)
{
    if (h && (e.Read(h).style & 0xc0000000) == 0x40000000)
        return 0;
    if (code == 1 || code == 6)
        return h;
    if ((code == 2 || code == 4) && !Refuses(e, h))
        return h;
    if (code >= 2 && code <= 4 && h && (e.Read(h).style & 0x80000000) && !Refuses(e, e.Read(h).owner))
        return e.Read(h).owner;
    if (code < 2 || code > 5)
        return 0;
    if (const unsigned queue = e.globals.previous_queue)
    {
        const unsigned previous = e.ReadQueue(queue).previous;
        if (!Refuses(e, previous))
            return previous;
    }
    for (unsigned flags = 2;; flags = 0)
    {
        if (const unsigned next = NextTop(e, h, code == 3 ? h : 0, flags))
        {
            const unsigned popup = e.Read(next).popup;
            return Refuses(e, popup) ? next : popup;
        }
        if (!flags)
            return 0;
    }
}
// USER. Foreground/position/restore services are explicit boundaries.
inline void Switch(Environment &e, unsigned h, unsigned alt_tab)
{
    alt_tab &= 65535;
    const unsigned queue = e.globals.foreground;
    if (queue && !alt_tab)
    {
        const auto old = e.ReadQueue(queue).active;
        if (old && Candidate(e, old, 0, 1))
        {
            const auto node = e.Read(old);
            if (!(node.extended & 8) && !(node.flags & 0x10000000))
                e.Position(old, 1, 0x4013);
        }
    }
    e.Foreground(h);
    if (e.Valid(h) && alt_tab && Iconic(e, h))
    {
        const auto task = e.Read(h).task;
        e.Restore(h, task, e.ReadTask(task).queue);
    }
}
// Return conventions differ for an already iconic window.
inline unsigned OpenIcon(Environment &e, unsigned h) { return Iconic(e, h) ? e.Show(h, 1) & 65535 : 0; }
inline unsigned CloseWindow(Environment &e, unsigned h)
{
    if (!Iconic(e, h))
        e.Show(h, 6);
    return 1;
}
// USER. Activation decisions and writes; external hooks, painting,
// queue signalling and message execution are supplied by the environment.
inline unsigned Activate(Environment &e, unsigned h, unsigned flags, unsigned other_task)
{
    flags &= 255;
    const auto task = e.CurrentTask();
    const auto queue = e.ReadTask(task).queue;
    if (!h || h == e.globals.desktop || e.ReadTask(e.Read(h).task).queue != queue)
        return 0;
    if (!e.globals.special_mode && e.globals.modal && h != e.globals.modal &&
        task != e.Read(e.globals.modal).task)
        return 0;
    if (e.ReadQueue(queue).active == h)
        return 0;
    if (e.HookPresent() && e.Hook(h, flags & 1, e.ReadQueue(queue).active))
        return 0;
    auto q = e.ReadQueue(queue);
    unsigned old = q.active;
    q.previous = old;
    e.WriteQueue(queue, q);
    if (old && queue == e.globals.foreground)
        e.globals.previous_queue = queue;
    if (q.active)
    {
        const unsigned old_task = e.Read(old).task;
        auto t = e.ReadTask(old_task);
        t.flags15 |= 1;
        e.WriteTask(old_task, t);
        const unsigned minimized = Iconic(e, q.active) ? 0x20 : 0;
        if (!e.Message(q.active, 0x86, 0, h, minimized) && h != e.globals.modal && e.Valid(old))
            return 0;
        const auto active = e.ReadQueue(queue).active;
        if (active)
            e.Message(active, 6, 0, h, minimized);
        if (e.TaskValid(old_task))
        {
            t = e.ReadTask(old_task);
            t.flags15 &= ~1u;
            e.WriteTask(old_task, t);
        }
    }
    q = e.ReadQueue(queue);
    if (q.active != q.previous || !e.Valid(h))
        return 0;
    if (!q.active)
        q.flags &= ~2u;
    q.active = h;
    e.WriteQueue(queue, q);
    if (queue == e.globals.foreground)
    {
        e.InputTask(e.Read(h).task);
        if (e.globals.accessibility)
            e.Accessibility(h);
    }
    e.SyncQueue(queue);
    old = e.ReadQueue(queue).previous;
    if (e.globals.palette & 1)
    {
        e.Message(h, 0x30f, 0, 0, 0);
        if (!e.Valid(h))
            return 0;
    }
    if (e.globals.special_mode && e.ReadTask(e.Read(h).task).queue == e.globals.foreground)
        e.SpecialWindow(h);
    if (!(flags & 8) && !(e.Read(h).style & 0x40000000))
    {
        unsigned first = e.Read(e.globals.desktop).first;
        for (unsigned n = 0; first && !(e.Read(first).style & 0x10000000) && n < e.Capacity(); ++n)
            first = e.Read(first).next;
        if (first != h)
        {
            e.Position(h, 0, 3);
            if (!e.Valid(h))
                return 0;
        }
    }
    if (!e.Valid(old))
        old = 0;
    bool guarded = false;
    if (!e.globals.suppress_app && !(e.ReadTask(task).flags14 & 0x20) &&
        (!old || e.Read(old).task != e.Read(h).task))
    {
        if (old)
        {
            const auto old_task = e.Read(old).task;
            auto t = e.ReadTask(old_task);
            t.flags14 |= 0x20;
            e.WriteTask(old_task, t);
            e.AppMessage(old_task, task, false);
            if (e.TaskValid(old_task))
            {
                t = e.ReadTask(old_task);
                t.flags14 &= ~0x20u;
                e.WriteTask(old_task, t);
            }
            if (!e.Valid(h))
                return 0;
        }
        guarded = true;
        auto t = e.ReadTask(task);
        t.flags14 |= 0x20;
        e.WriteTask(task, t);
        e.AppMessage(e.Read(h).task, other_task & 65535, true);
    }
    auto cleanup = [&]()
    {
        if (guarded)
        {
            auto t = e.ReadTask(task);
            t.flags14 &= ~0x20u;
            e.WriteTask(task, t);
        }
    };
    if (!e.Valid(h))
    {
        cleanup();
        return 0;
    }
    auto node = e.Read(h);
    if (node.flags & 0x40)
    {
        node.flags |= 0x100;
        e.WriteWindow(h, node);
    }
    const auto owner = OwnerRoot(e, h);
    node = e.Read(owner);
    node.popup = h;
    e.WriteWindow(owner, node);
    q = e.ReadQueue(queue);
    const auto previous = q.previous;
    const unsigned minimized = Iconic(e, q.active) ? 0x20 : 0;
    e.Message(h, 0x86, e.ReadTask(e.Read(h).task).queue == e.globals.foreground, previous, minimized);
    if (!e.Valid(h))
    {
        cleanup();
        return 0;
    }
    e.Message(h, 6, (flags & 1) ? 2 : 1, previous, minimized);
    if (!e.Valid(h))
    {
        cleanup();
        return 0;
    }
    e.Activated(h);
    if (!e.Valid(h))
    {
        cleanup();
        return 0;
    }
    node = e.Read(h);
    node.flags &= ~0x100u;
    e.WriteWindow(h, node);
    q = e.ReadQueue(queue);
    if ((flags & 2) && !(q.flags & 2) && Root(e, q.focus) != q.active)
        e.Focus(Iconic(e, q.active) ? 0 : q.active, task);
    e.globals.transitions = (e.globals.transitions + 1) & 65535;
    cleanup();
    return e.ReadQueue(queue).active == h;
}
} // namespace Win98Activation
