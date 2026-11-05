// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_focus.h"
using namespace Win98Activation;
using Service = unsigned (*)(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned);
struct Bridge final : Win98Focus::Environment
{
    Window *windows;
    Task *tasks;
    Queue *queues;
    unsigned char *alive;
    unsigned count, current;
    Service service;
    Bridge(Globals &g, Window *w, Task *t, Queue *q, unsigned char *a, unsigned n, unsigned c, Service s)
        : Win98Focus::Environment(g), windows(w), tasks(t), queues(q), alive(a), count(n), current(c),
          service(s)
    {
    }
    Window Read(unsigned h) const override { return h <= count ? windows[h] : Window{}; }
    bool Valid(unsigned h) const override { return h && h <= count && alive[h]; }
    unsigned CurrentTask() const override { return current; }
    Task ReadTask(unsigned t) const override { return t <= 4 ? tasks[t] : Task{}; }
    Queue ReadQueue(unsigned q) const override { return q <= 4 ? queues[q] : Queue{}; }
    void WriteTask(unsigned t, const Task &v) override { tasks[t] = v; }
    void WriteQueue(unsigned q, const Queue &v) override { queues[q] = v; }
    void WriteWindow(unsigned h, const Window &v) override { windows[h] = v; }
    unsigned Capacity() const override { return count + 1; }
    bool FocusHook(unsigned h, unsigned old) override { return service(15, h, old, 0, 0, 0); }
    void FocusEvent(unsigned h) override { service(16, h, 0, 0, 0, 0); }
    bool QueueBlocked(unsigned q) override { return service(17, q, 0, 0, 0, 0); }
    bool PrepareFocusForeground(unsigned h, unsigned t) override { return service(18, h, t, 0, 0, 0); }
    bool FocusActivate(unsigned h) override { return service(19, h, 0, 0, 0, 0); }
    bool HookPresent() override { return service(0, 0, 0, 0, 0, 0); }
    bool Hook(unsigned h, unsigned mouse, unsigned previous) override
    {
        return service(1, h, mouse, previous, 0, 0);
    }
    unsigned Message(unsigned h, unsigned m, unsigned w, unsigned other, unsigned iconic) override
    {
        return service(2, h, m, w, other, iconic);
    }
    bool TaskValid(unsigned t) override { return service(3, t, 0, 0, 0, 0); }
    void InputTask(unsigned t) override { service(4, t, 0, 0, 0, 0); }
    void Accessibility(unsigned h) override { service(5, h, 0, 0, 0, 0); }
    void SyncQueue(unsigned q) override { service(6, q, 0, 0, 0, 0); }
    void SpecialWindow(unsigned h) override { service(7, h, 0, 0, 0, 0); }
    void Position(unsigned h, unsigned after, unsigned flags) override { service(8, h, after, flags, 0, 0); }
    void AppMessage(unsigned t, unsigned other, bool active) override { service(9, t, other, active, 0, 0); }
    void Activated(unsigned h) override { service(10, h, 0, 0, 0, 0); }
    void Focus(unsigned h, unsigned t) override { service(11, h, t, 0, 0, 0); }
    void Foreground(unsigned h) override { service(12, h, 0, 0, 0, 0); }
    unsigned Show(unsigned h, unsigned command) override { return service(14, h, command, 0, 0, 0); }
    void Restore(unsigned h, unsigned t, unsigned q) override { service(13, h, t, q, 0, 0); }
};
extern "C" unsigned activation_call(Globals *g, Window *w, Task *t, Queue *q, unsigned char *alive,
                                    unsigned count, unsigned current, Service service, unsigned operation,
                                    unsigned h, unsigned a, unsigned b)
{
    Bridge e(*g, w, t, q, alive, count, current, service);
    switch (operation)
    {
    case 0:
        return Root(e, h);
    case 1:
        return OwnerRoot(e, h);
    case 2:
        return Popup(e, h);
    case 3:
        return Iconic(e, h);
    case 4:
        return Zoomed(e, h);
    case 5:
        return Candidate(e, h, a, b);
    case 6:
        Switch(e, h, a);
        return 0;
    case 7:
        return Activate(e, h, a, b);
    case 8:
        return OpenIcon(e, h);
    case 9:
        return CloseWindow(e, h);
    case 10:
        Win98Focus::Transfer(e, h, a);
        return 0;
    case 11:
        return Win98Focus::Set(e, h);
    default:
        return ~0u;
    }
}
