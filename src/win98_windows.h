// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Windows
{
struct Node
{
    unsigned next = 0, first = 0, parent = 0, owner = 0, queue = 0, style = 0, extended = 0;
};
struct Tree
{
    void *context;
    Node (*get)(void *, unsigned);
    unsigned desktop, capacity;
    bool popup_search_blocked = false;
    Node Read(unsigned h) const { return h ? get(context, h) : Node{}; }
};
// USER. The original searches singly linked siblings for the predecessor.
inline unsigned Previous(const Tree &tree, unsigned stop, unsigned window)
{
    const auto parent = tree.Read(window).parent;
    if (!window || !parent)
        return 0;
    unsigned previous = 0, current = tree.Read(parent).first;
    for (unsigned steps = 0; current && current != stop && steps < tree.capacity; ++steps)
    {
        previous = current;
        current = tree.Read(current).next;
    }
    return previous;
}
// USER, including queue, visibility, owner ancestry and topmost rules.
inline unsigned Popup(const Tree &tree, unsigned window)
{
    if (tree.popup_search_blocked)
        return 0;
    const auto anchor = tree.Read(window);
    unsigned current = anchor.next;
    for (unsigned steps = 0; current != window && steps <= tree.capacity; ++steps)
    {
        if (!current)
        {
            current = tree.Read(tree.desktop).first;
            continue;
        }
        const auto candidate = tree.Read(current);
        if (candidate.queue == anchor.queue && !(candidate.style & 0x08000000) &&
            (candidate.style & 0x10000000))
        {
            unsigned owner = candidate.owner;
            for (unsigned depth = 0; owner && depth < tree.capacity; ++depth)
            {
                if (owner == window)
                    return current;
                owner = tree.Read(owner).owner;
            }
            if (!(candidate.extended & 8))
                return 0;
        }
        current = candidate.next;
    }
    return 0;
}
// USER. Public wrappers validate the input HWND first.
inline unsigned Get(const Tree &tree, unsigned window, unsigned command)
{
    const auto node = tree.Read(window);
    switch (command & 65535)
    {
    case 0:
        return node.parent ? tree.Read(node.parent).first : 0;
    case 1:
        return Previous(tree, 0, window);
    case 2:
        return node.next;
    case 3:
        return Previous(tree, window, window);
    case 4:
        return node.owner;
    case 5:
        return node.first;
    case 6:
        return Popup(tree, window);
    default:
        return 0;
    }
}
// USER. A zero argument selects the desktop's first child.
inline unsigned Top(const Tree &tree, unsigned window)
{
    return tree.Read(window ? window : tree.desktop).first;
}
// Visiting order from USER: bit 0 descends, bit 1 follows siblings.
// The native provider supplies bounded storage instead of Win16 LocalReAlloc.
inline bool Snapshot(const Tree &tree, unsigned window, unsigned flags, unsigned *out, unsigned capacity,
                     unsigned &count, unsigned depth = 0)
{
    while (window)
    {
        if (count == capacity || depth >= tree.capacity)
            return false;
        out[count++] = window;
        const auto node = tree.Read(window);
        if ((flags & 1) && node.first && !Snapshot(tree, node.first, 3, out, capacity, count, depth + 1))
            return false;
        if (!(flags & 2))
            break;
        window = node.next;
    }
    return true;
}
} // namespace Win98Windows
