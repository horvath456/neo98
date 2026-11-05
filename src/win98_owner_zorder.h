// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Original Win98 owner z-order. USER is the owned-window pass of the
// window-position engine, reached from the apply loop for every deferred entry
// that really changes the sibling order. emits the group,
// resolves an owner at the window's own sibling level, Windows 98 walks the
// siblings a window owns, Windows 98 tests ownership and Windows 98 tests whether the
// requested insert-after window already follows a candidate.
//
// The deferred-entry list, the window fields, the window-handle check and the
// global at USER ds:00C4h are explicit providers. The original list lives in
// the USER local heap and grows through LocalReAlloc; that storage is not part
// of this implementation.
namespace Win98OwnerZOrder
{
using U = unsigned;
// Modeled fields of the original WND: next sibling at +00h, first child at
// +04h, parent at +08h, owner at +0Ch and the state byte at +2Fh whose bit 10h
// keeps a window out of the z-order walk.
struct Node
{
    U next = 0, first = 0, parent = 0, owner = 0, state = 0;
};
// A deferred window-position entry: window at +00h, insert-after at +04h and
// the SWP flags at +10h. The pass reads and writes only those three fields.
struct Entry
{
    U window = 0, after = 0, flags = 0;
};
// HWND_BOTTOM is the entry value 1. SWP_NOZORDER is 0004h, SWP_NOOWNERZORDER
// is 0200h, and the group entries carry SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE.
constexpr U Bottom = 1, NoZOrder = 0x0004, NoOwnerZOrder = 0x0200, GroupFlags = 0x0013, Excluded = 0x10;

// USER. A stale owner handle is dropped first. The owner chain is
// followed once and then the parent chain, until the result is a sibling of
// the window. A window that owns itself answers itself.
template <class E> U OwnerAtLevel(E &e, U window)
{
    const U parent = e.Read(window).parent;
    if (const U owner = e.Read(window).owner)
        if (!e.IsWindow(owner))
            e.ClearOwner(window);
    if (e.Read(window).owner == window)
        return window;
    U current = e.Read(window).owner;
    for (U steps = 0; current && steps <= e.Capacity(); ++steps)
    {
        if (e.Read(current).parent == parent)
            break;
        current = e.Read(current).parent;
    }
    return current;
}
// USER. The next sibling whose owner at that level is the given window.
// A null predecessor starts at the parent's first child. The original enters
// the comparison without a null check; every caller passes a real owner, so a
// missing first child ends the walk either way.
template <class E> U NextOwned(E &e, U parent, U owner, U after)
{
    U current = after ? e.Read(after).next : e.Read(parent).first;
    for (U steps = 0; current && steps <= e.Capacity(); ++steps)
    {
        if (OwnerAtLevel(e, current) == owner)
            return current;
        current = e.Read(current).next;
    }
    return 0;
}
// USER. True when the owner appears in the window's own parent chain or
// in the parent chain of any window along its owner chain.
template <class E> bool OwnedBy(E &e, U owner, U window)
{
    for (U steps = 0; window && steps <= e.Capacity(); ++steps)
    {
        U current = window;
        for (U walk = 0; current && walk <= e.Capacity(); ++walk)
        {
            if (current == owner)
                return true;
            current = e.Read(current).parent;
        }
        window = e.Read(window).owner == window ? 0 : e.Read(window).owner;
    }
    return false;
}
// USER. False when the insert-after window already follows the
// candidate, and for the HWND_BOTTOM value. A null insert-after answers true.
template <class E> bool NotInChain(E &e, U after, U list)
{
    if (!after)
        return true;
    if (after == Bottom)
        return false;
    for (U steps = 0; list && steps <= e.Capacity(); ++steps)
    {
        if (list == after)
            return false;
        list = e.Read(list).next;
    }
    return true;
}
// USER. Every window the root owns is deferred before the root itself,
// recursively, so the owner ends up underneath its own popups. Each appended
// entry except the one that lands on the caller's slot is placed behind the
// window of the entry before it, which keeps the emitted order.
template <class E> void AddOwned(E &e, U index, U after, U window, U root, U depth)
{
    if (depth > e.Capacity())
        return;
    U chain = window;
    for (U steps = 0; chain && steps <= e.Capacity(); ++steps)
    {
        const U next = OwnerAtLevel(e, chain);
        if (next == root)
            break;
        chain = next;
    }
    U current = 0;
    bool inserted = false;
    for (U steps = 0; steps <= e.Capacity(); ++steps)
    {
        current = NextOwned(e, e.Read(root).parent, root, current);
        if (!current)
            break;
        if (chain)
        {
            if (!inserted && NotInChain(e, after, current))
            {
                AddOwned(e, index, after, window, chain, depth + 1);
                inserted = true;
            }
            if (current == chain)
                continue;
        }
        AddOwned(e, index, 0, 0, current, depth + 1);
    }
    if (chain && !inserted)
        AddOwned(e, index, after, window, chain, depth + 1);
    if (!e.Append(Entry{root, 0, GroupFlags}))
        return;
    // Windows 98 compares the appended slot with the caller's index as a WORD. The
    // extra count check is a native bound: the original would read the entry
    // before the list for an index the caller never passes.
    const U count = e.Count();
    if (((count - index) & 65535u) != 1 && count >= 2)
    {
        Entry last = e.Get(count - 1);
        last.after = e.Get(count - 2).window;
        e.Set(count - 1, last);
    }
}
// USER. The pass runs only for a real sibling-order change without
// SWP_NOOWNERZORDER, and only for a window that has an owner or owns a
// sibling. It resolves the group's top-level owner, turns HWND_BOTTOM and an
// owned insert-after window into a concrete predecessor and then emits the
// group over its own entry slot.
template <class E> void Arrange(E &e, U index)
{
    if (e.Disabled())
        return;
    Entry entry = e.Get(index);
    if (entry.flags & (NoZOrder | NoOwnerZOrder))
        return;
    const U window = entry.window, after = entry.after;
    const bool owns = NextOwned(e, e.Read(window).parent, window, 0) != 0;
    if (!e.Read(window).owner && !owns)
        return;
    U root = window;
    for (U steps = 0; steps <= e.Capacity(); ++steps)
    {
        const U next = OwnerAtLevel(e, root);
        if (!next)
            break;
        root = next;
    }
    U last = after;
    const U parent = e.Read(window).parent;
    if (after == Bottom)
    {
        // The window goes to the bottom of the band: keep the group above the
        // last sibling that the root does not own, and stop at an excluded one.
        U current = e.Read(parent).first;
        for (U steps = 0; current && steps <= e.Capacity(); ++steps)
        {
            if (e.Read(current).state & Excluded)
                break;
            if (!OwnedBy(e, root, current))
                last = current;
            current = e.Read(current).next;
        }
        if (last == Bottom)
        {
            entry.flags |= NoZOrder;
            e.Set(index, entry);
        }
    }
    else if (after && OwnedBy(e, root, after))
    {
        // The caller asked to go behind one of the root's own windows. A
        // window that owns nothing and already follows its owner stays put.
        if (!owns)
        {
            U current = after;
            for (U steps = 0; current && steps <= e.Capacity(); ++steps)
            {
                if (current == e.Read(window).owner)
                    return;
                current = e.Read(current).next;
            }
        }
        last = 0;
        U current = e.Read(parent).first;
        for (U steps = 0; current && steps <= e.Capacity(); ++steps)
        {
            if (OwnedBy(e, root, current))
                break;
            last = current;
            current = e.Read(current).next;
        }
    }
    entry = e.Get(index);
    if (entry.flags & NoZOrder)
        return;
    e.SetCount(e.Count() - 1);
    AddOwned(e, index, after, window, root, 0);
    entry = e.Get(index);
    entry.after = last;
    e.Set(index, entry);
}
}
