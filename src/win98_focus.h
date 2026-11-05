// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_activation.h"
namespace Win98Focus
{
struct Environment : Win98Activation::Environment
{
    explicit Environment(Win98Activation::Globals &g) : Win98Activation::Environment(g) {}
    virtual bool FocusHook(unsigned target, unsigned previous) = 0;
    virtual void FocusEvent(unsigned target) = 0;
    virtual bool QueueBlocked(unsigned queue) = 0;
    virtual bool PrepareFocusForeground(unsigned target, unsigned task) = 0;
    virtual bool FocusActivate(unsigned root) = 0;
};
// USER: queue publication, also used by the native input adapter.
inline unsigned Publish(Win98Activation::Queue &q, unsigned h)
{
    if (!h && q.active)
        q.flags |= 2;
    else
        q.flags &= ~2u;
    const unsigned old = q.focus;
    q.focus = h;
    return old;
}
// USER. Publish the new focus before WM_KILLFOCUS, and observe reentrancy.
inline void Transfer(Environment &e, unsigned h, unsigned task)
{
    auto queue = e.ReadTask(task).queue;
    auto q = e.ReadQueue(queue);
    const unsigned old = Publish(q, h);
    e.WriteQueue(queue, q);
    if (h)
    {
        if (old)
            e.Message(old, 8, h, 0, 0);
        if (e.ReadQueue(e.ReadTask(task).queue).focus != h)
            return;
        if (e.globals.accessibility)
            e.FocusEvent(h);
        e.Message(h, 7, old, 0, 0);
    }
    else if (old)
    {
        if (e.globals.accessibility)
            e.FocusEvent(0);
        e.Message(old, 8, 0, 0, 0);
    }
}
// USER. Validation, hook, activation and transfer are distinct stages.
inline unsigned Set(Environment &e, unsigned h)
{
    const unsigned task = e.CurrentTask();
    auto queue = [&]() { return e.ReadTask(task).queue; };
    if (!h)
    {
        if (e.FocusHook(0, e.ReadQueue(queue()).focus))
            return 0;
        const auto old = e.ReadQueue(queue()).focus;
        Transfer(e, 0, task);
        return old;
    }
    if (e.ReadTask(e.Read(h).task).queue != queue())
        return 0;
    unsigned root = h;
    for (unsigned n = 0;; ++n)
    {
        const auto node = e.Read(root);
        if (n >= e.Capacity() || (node.style & 0x28000000))
            return 0;
        if ((node.style & 0xc0000000) != 0x40000000)
            break;
        root = node.parent;
    }
    auto q = e.ReadQueue(queue());
    if (q.focus == h)
        return h;
    if (e.FocusHook(h, q.focus))
        return 0;
    if (e.ReadQueue(queue()).active != root)
    {
        if (queue() != e.globals.foreground && e.QueueBlocked(queue()) &&
            !e.PrepareFocusForeground(root, task))
            return 0;
        if (e.ReadQueue(queue()).active != root && !e.FocusActivate(root))
            return 0;
        if (!e.Valid(h))
            return 0;
    }
    const auto old = e.ReadQueue(queue()).focus;
    Transfer(e, h, task);
    return old;
}
} // namespace Win98Focus
