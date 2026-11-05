// SPDX-License-Identifier: GPL-2.0-or-later
// Native USER decisions with new native queue, callback and layout providers.
#include "win98_focus.h"
#include "win98_cbt_hooks.h"
#include "x86_app.h"
#include "win16_machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace X86
{
// NEO98_TRACE_ACTIVATE=1 logs each activation and WM_ACTIVATEAPP round with
// its originating process, distinguishing events from concurrent
// applications.
static bool ActivationTrace()
{
    static const bool on = getenv("NEO98_TRACE_ACTIVATE") != nullptr;
    return on;
}
struct Application::ActivationEnvironment final : Win98Focus::Environment
{
    Application &app;
    explicit ActivationEnvironment(Application &a) : Win98Focus::Environment(User::ActivationGlobals()), app(a)
    {
        globals.desktop = User::GetDesktopWindow();
        globals.foreground = User::ForegroundQueue();
        // ds:2AEh: a WinEvent hook exists.
        globals.accessibility = User::WinEventListening() ? 1 : 0;
        // ds:11AEh bit 0: the display has a palette (RC_PALETTE). Activation
        // then asks the new active window for its palette, WM_QUERYNEWPALETTE
        // ( in Win98Activation::SetActive).
        globals.palette = Gdi::DisplayDepth() <= 8 ? 1 : 0;
    }
    // Win98 gives every Win32 thread its own input queue, named here by the
    // thread id. `acting` is the queue the calling thread works in while
    // USER hands the foreground to another queue: the activation of
    // the new foreground window runs from the caller, in that window's queue.
    unsigned acting = 0;
    unsigned QueueOfTask(unsigned t) const { return acting && t == CurrentTask() ? acting : t; }
    // Windows 98 task identities are session-wide. Use
    // Application::AllocateThreadIdentity, the same identity stored on USER
    // windows. Process-local identities would collapse foreign tasks to zero
    // and broadcast WM_ACTIVATEAPP to unrelated applications.
    Application *TaskOwner(unsigned id, unsigned &slot) const
    {
        if (!id)
            return nullptr;
        for (unsigned i = 0; i < User::ProcessContextCount(); ++i)
        {
            auto *other = static_cast<Application *>(User::ProcessContextAt(i));
            if (!other)
                continue;
            for (unsigned t = 0; t < ThreadCapacity; ++t)
                if (other->threads[t].fiber && other->threads[t].id == id)
                {
                    slot = t;
                    return other;
                }
        }
        return nullptr;
    }
    Win98Activation::Window Read(unsigned h) const override { return User::ActivationWindow(h); }
    bool Valid(unsigned h) const override { return User::IsWindow(h); }
    unsigned CurrentTask() const override { return app.threads[app.current_thread].id; }
    Win98Activation::Task ReadTask(unsigned t) const override
    {
        unsigned slot = 0;
        auto *owner = TaskOwner(t, slot);
        Win98Activation::Task result = owner ? owner->activation_tasks[slot] : Win98Activation::Task{};
        result.queue = QueueOfTask(t);
        return result;
    }
    Win98Activation::Queue ReadQueue(unsigned q) const override
    {
        return q ? User::ThreadActivation(q) : Win98Activation::Queue{};
    }
    void WriteTask(unsigned t, const Win98Activation::Task &v) override
    {
        unsigned slot = 0;
        if (auto *owner = TaskOwner(t, slot))
            owner->activation_tasks[slot] = v;
    }
    void WriteQueue(unsigned q, const Win98Activation::Queue &v) override
    {
        if (ActivationTrace())
            fprintf(stderr, "ACTQUEUE process=%u thread=%u acting=%u queue=%u foreground=%u active=%u previous=%u focus=%u\n",
                    app.ProcessId(), app.ThreadId(), acting, q, User::ForegroundQueue(), v.active, v.previous, v.focus);
        if (q)
            User::WriteThreadActivation(q, v);
    }
    void WriteWindow(unsigned h, const Win98Activation::Window &v) override
    {
        User::WriteActivationWindow(h, v);
    }
    unsigned Capacity() const override { return 193; }
    bool FocusHook(unsigned h,unsigned old) override
    {
        // The foreground transfer temporarily carries the old queue's
        // focus in the shared record. HCBT_SETFOCUS, like WM_SETFOCUS,
        // reports only the previous focus within the receiving queue.
        if (old && ReadTask(Read(old).task).queue != ReadTask(CurrentTask()).queue)
            old = 0;
        return app.CbtEvent(9,h,old);
    }
    // EVENT_OBJECT_FOCUS on OBJID_CLIENT, from the caller.
    void FocusEvent(unsigned h) override { app.WinEventNotify(0x8005, h, 0xfffffffcu, 0, false); }
    // USER: SetFocus in a queue that is not the foreground moves
    // the foreground only with the foreground activation permission.
    bool QueueBlocked(unsigned) override { return app.ForegroundPermission(); }
    // USER passes 8: SetFocus itself will transfer focus to its
    // requested child after activation. Flag 2 focuses the root in between,
    // reentering an unrelated WM_SETFOCUS handler (MPC's hidden popup).
    bool PrepareFocusForeground(unsigned root, unsigned) override { return app.SwitchForeground(root, 8) != 0; }
    bool FocusActivate(unsigned h) override
    {
        if (ActivationTrace())
        {
            fprintf(stderr, "ACTIVATE focus process=%u thread=%u hwnd=%u foreground=%u\n", app.ProcessId(), app.ThreadId(), h,
                    User::ForegroundQueue());
            for (unsigned back = 0; back < 4; ++back)
                if (auto *c = app.api_ring.Recent(back))
                    fprintf(stderr, "ACTIVATE_CALL %u %s!%s %08x,%08x,%08x,%08x\n", c->sequence, c->dll, c->name,
                            c->args[0], c->args[1], c->args[2], c->args[3]);
        }
        return Win98Activation::Activate(*this, h, 0, 0);
    }
    bool HookPresent() override { return app.CbtPresent(); }
    bool Hook(unsigned h,unsigned mouse,unsigned previous) override { return app.CbtActivate(h,mouse,previous); }
    unsigned Message(unsigned h, unsigned m, unsigned wp, unsigned other, unsigned iconic) override
    {
        // USER deactivates another input queue with null HWND
        // parameters. The shared native foreground record must not expose
        // the other queue's active/focus window as an intra-queue transfer.
        if (m == 6 && other && ReadTask(Read(h).task).queue != ReadTask(Read(other).task).queue)
            other = 0;
        if ((m == 7 || m == 8) && wp && ReadTask(Read(h).task).queue != ReadTask(Read(wp).task).queue)
            wp = 0;
        // USER's Win16 activation lParam packs the other HWND and raw WS_MINIMIZE bit.
        // The Win32 ABI carries the minimized BOOL in wParam's high word instead.
        if (m == 6)
            wp |= (iconic ? 0x20u : 0u) << 16;
        // SendMessage: a window of another process answers on its own thread.
        return app.SendActivation(h, m, wp, m == 6 ? other : 0);
    }
    bool TaskValid(unsigned t) override
    {
        unsigned slot = 0;
        auto *owner = TaskOwner(t, slot);
        return owner && !owner->threads[slot].done;
    }
    void InputTask(unsigned) override {}     // One queue; no foreground thread routing service.
    // EVENT_SYSTEM_FOREGROUND for the new foreground window.
    void Accessibility(unsigned h) override { app.WinEventNotify(3, h, 0, 0, true); }
    void SyncQueue(unsigned) override {}     // Internal USER queue codes 5/6 are not produced here.
    void SpecialWindow(unsigned) override {} // Disabled by native globals.
    void Position(unsigned h, unsigned after, unsigned flags) override
    {
        unsigned position[1][7] = {{h,after,0,0,0,0,flags}};
        app.PositionWindows(1,position);
    }
    void AppMessage(unsigned task, unsigned other, bool active) override
    {
        if (ActivationTrace())
            fprintf(stderr, "ACTAPP process=%u task=%u other=%u active=%u\n", app.ProcessId(), task, other,
                    active ? 1u : 0u);
        // Only a named task has target windows. Sending without one could
        // reach every window whose task is unresolved in this session.
        if (!task)
            return;
        unsigned windows[User::WindowCapacity], count = 0;
        if (!User::WindowSnapshot(0, windows, User::WindowCapacity, count))
            return;
        for (unsigned i = 0; i < count; ++i)
            if (Valid(windows[i]) && Read(windows[i]).task == task)
                app.SendActivation(windows[i], 0x1c, active, other);
    }
    // The taskbar hears about the activation of the foreground queue only.
    void Activated(unsigned h) override
    {
        if (ReadTask(Read(h).task).queue == globals.foreground) User::NotifyActivation(h);
    }
    void Focus(unsigned h, unsigned task) override { Win98Focus::Transfer(*this, h, task); }
    // The task switcher gives the caller process flag 40h first, so
    // its SetForegroundWindow always passes the lock.
    void Foreground(unsigned h) override { app.SwitchForeground(h, 2); }
    unsigned Show(unsigned h, unsigned command) override
    {
        const bool iconic = Win98Activation::Iconic(*this, h);
        const unsigned result = app.ShowWindow(h, command);
        return iconic && command == 1 && Win98Activation::Iconic(*this, h) ? 0 : result;
    }
    void Restore(unsigned h, unsigned, unsigned) override { User::PostMessage(h, 0x112, 0xf120, 0); }
};
// A message of an activation for a window of another queue. Win98 does not
// run the change of another queue from the caller: Windows 98 leaves it in that
// queue, and the thread of the queue carries it out when it next reads its
// messages. A program that does not answer therefore never holds up the one
// that takes the foreground. Here the caller sends, which keeps the order of
// the messages as long as the other thread answers; when that thread sleeps
// in a wait of KERNEL32 that reads no messages (WaitForSingleObject, a
// critical section), the message is left in its queue without a wait, like
// SendNotifyMessage, and counts as accepted. DIRECTDB shows its progress
// dialog from a second thread and calls SetForegroundWindow while the first
// thread, which owns the active window, waits for that thread's event without
// a timeout: the send never came back and Outlook Express stood.
unsigned Application::SendActivation(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    const unsigned owner = User::WindowThread(hwnd), process = User::WindowProcess(hwnd);
    if (owner && User::IsWindow(hwnd) && (owner != ThreadId() || (process && process != process_id)))
    {
        // KERNEL32's Ctrl+Alt+Delete thread runs with VMM's system UI state. Its activation must not wait for an application
        // it is offering to end. Keep the notifications on that owner's queue.
        if (ThreadId() == close_program_thread_id)
        {
            SendNotify(hwnd, message, wp, lp);
            return 1;
        }
        auto *other = process && process != process_id ? static_cast<Application *>(User::ProcessContextOf(process)) : this;
        if (other)
            for (const auto &t : other->threads)
            {
                if (!t.fiber || t.done || t.id != owner)
                    continue;
                const bool asleep = t.block == 2 || t.block == 5 || (t.block == 7 && !t.multiwait.message);
                if (!asleep)
                    break;
                if (ActivationTrace())
                    fprintf(stderr, "ACTIVATE queued process=%u thread=%u hwnd=%u message=%x block=%u\n", ProcessId(), owner,
                            hwnd, message, t.block);
                SendNotify(hwnd, message, wp, lp);
                return 1;
            }
    }
    return Send(hwnd, message, wp, lp);
}
unsigned Application::SetFocus(unsigned hwnd)
{
    if (hwnd && !User::IsWindow(hwnd))
    {
        last_error = 1400;
        return 0;
    }
    ActivationEnvironment e(*this);
    return Win98Focus::Set(e, hwnd);
}
void Application::MouseFocus(unsigned hwnd, bool focus)
{
    // The press is routed on the input thread of this process. USER activates
    // a window by mouse in the queue that reads the press ( hands the
    // event to that queue), so a window of another thread of this process is
    // activated by that thread, before it gets the press. Activating it from
    // the routing thread sent the activation messages across threads while
    // the clicked thread already ran the click: the taskbar opened the
    // Date/Time dialog on the double click and took the foreground back from
    // it when the late WM_ACTIVATEAPP reached it.
    if (const unsigned owner = User::WindowThread(hwnd); owner && owner != ThreadId())
        for (unsigned i = 0; i < ThreadCapacity; ++i)
            if (threads[i].id == owner && threads[i].fiber && !threads[i].done)
            {
                User::PostQueueMouseActivation(owner, hwnd, focus);
                return;
            }
    ActivationEnvironment e(*this);
    const unsigned root = Win98Activation::Root(e, hwnd);
    if (!ActivateWindow(root, true) || !focus || !User::IsWindow(hwnd) || User::ActivationQueue().active != root)
        return;
    // The press is routed on the input thread of this process; the focus
    // changes in the queue of the window that was clicked.
    ActivationEnvironment f(*this);
    f.acting = User::WindowThread(hwnd) ? User::WindowThread(hwnd) : ThreadId();
    f.globals.foreground = User::ForegroundQueue();
    Win98Focus::Set(f, hwnd);
}
// The activation queue a window belongs to, and the focus of that queue.
static Win98Activation::Queue WindowQueue(unsigned hwnd) { return User::ThreadActivation(User::WindowThread(hwnd)); }
// WM_MOUSEACTIVATE, USER (called, where a queue takes a mouse
// message out of the hardware input). For a press of a button (0201h, 0204h,
// 0207h) on a window that is not itself the active window of its queue
// and whose top-level window is not the desktop, USER sends
// the window under the pointer WM_MOUSEACTIVATE with the handle of its
// top-level window in wParam (WND+46h) and the hit code and the mouse message
// in lParam. The answer, a signed word:
// below zero, 3 (MA_NOACTIVATE) or above 4: the press is delivered, nothing
// is activated;
// 0, 1 (MA_ACTIVATE) or 2 (MA_ACTIVATEANDEAT): the top-level window is
// activated unless it is the active one already; after 2
// the press is dropped;
// 4 (MA_NOACTIVATEANDEAT): the press is dropped, nothing is activated

// The caller drops the press for the answer 1 of this routine and
// hands it on for 0 and 3. Returns 0 (deliver, activate), 1 (drop) or 3
// (deliver, do not activate); the routing activates afterwards, where it did
// before this message existed here.
unsigned Application::MouseActivate(unsigned hwnd, unsigned hit, unsigned message)
{
    if (!User::IsWindow(hwnd))
        return 0;
    ActivationEnvironment e(*this);
    const unsigned root = Win98Activation::Root(e, hwnd);
    if (!root || root == User::GetDesktopWindow() || WindowQueue(hwnd).active == hwnd)
        return 0;
    const unsigned word = Send(hwnd, 0x21, root, (hit & 0xffff) | (message << 16)) & 0xffff;
    const int answer = int(word) - (word & 0x8000 ? 0x10000 : 0);
    if (state == Failed || state == Exited || !User::IsWindow(hwnd))
        return 1;
    if (answer == 4)
        return 1;
    if (answer < 0 || answer > 2)
        return 3;
    if (answer == 2)
    {
        MouseFocus(hwnd, false);
        return 1;
    }
    return 0;
}
static unsigned WindowQueueFocus(unsigned hwnd) { return WindowQueue(hwnd).focus; }
// USER. A window of the calling queue is activated in that queue; it takes the foreground with it only if grants it, that
// is once per started process or per pass of the queue. A window of another
// queue is reached only from the foreground queue, or when there is none,
// and the calling queue keeps flag 10h afterwards. A click is the
// input path and always brings its window to the front.
unsigned Application::ActivateWindow(unsigned hwnd, bool mouse)
{
    if (ActivationTrace())
    {
        fprintf(stderr, "ACTIVATE process=%u program=%s thread=%u hwnd=%u mouse=%u foreground=%u active=%u\n", ProcessId(),
                program_name, ThreadId(), hwnd, mouse ? 1u : 0u, User::ForegroundQueue(), User::ActivationQueue().active);
        for (unsigned back = 0; back < 8; ++back)
            if (auto *c = api_ring.Recent(back))
                fprintf(stderr, "ACTIVATE_CALL %u %s!%s %08x,%08x,%08x,%08x\n", c->sequence, c->dll, c->name,
                        c->args[0], c->args[1], c->args[2], c->args[3]);
    }
    if (!User::IsWindow(hwnd) || hwnd == User::GetDesktopWindow() ||
        (User::Style(hwnd) & 0xc0000000) == 0x40000000)
        return 0;
    if (mouse)
        return SwitchForeground(hwnd, 3);
    const unsigned current = ThreadId(), target = User::WindowThread(hwnd), foreground = User::ForegroundQueue();
    if (!target || target == current)
    {
        if (ForegroundPermission())
            return SwitchForeground(hwnd, 2);
        return ActivateInQueue(current, hwnd, 2);
    }
    if (foreground && current != foreground)
        return 0;
    const unsigned result = SwitchForeground(hwnd, 2);
    User::GrantQueuePass(current);
    return result;
}
// USER for the calling queue. Windows 98 uses up the queue's flag 10h
// and the process's start permission (bit 4) in one go; the answer counts
// only for a queue that is not the foreground. The two further grants of
// (queue byte 15h bit 10h) and (GetAppCompatFlags bit 1000h)
// have no source in NEO98.
bool Application::ForegroundPermission()
{
    const unsigned current = ThreadId();
    const bool pass = User::TakeQueuePass(current);
    const bool start = User::TakeStartPermission(process_id);
    if (ActivationTrace())
        fprintf(stderr, "FOREGROUND permission process=%u thread=%u pass=%u start=%u foreground=%u\n", ProcessId(), current,
                pass ? 1u : 0u, start ? 1u : 0u, User::ForegroundQueue());
    return !User::ForegroundQueue() || ((pass || start) && current != User::ForegroundQueue());
}
// USER in the given queue. The queue is the caller's own, or the
// queue of the window that just made the foreground.
unsigned Application::ActivateInQueue(unsigned queue, unsigned hwnd, unsigned flags)
{
    ActivationEnvironment environment(*this);
    environment.acting = queue;
    const auto q = User::ThreadActivation(queue);
    if (q.active == hwnd)
    {
        // Hiding a window takes the foreground from it but leaves it the
        // active window of its queue. A program that shows it again and asks
        // for the foreground must get it back: DINPUT refuses Acquire with
        // DIERR_OTHERAPPHASPRIO while GetForegroundWindow answers null.
        if (queue == User::ForegroundQueue() && User::GetForegroundWindow() != hwnd && User::IsVisible(hwnd))
            User::WriteActivationQueue(q);
        return 1;
    }
    const auto previous = environment.Read(q.active).task;
    return Win98Activation::Activate(environment, hwnd, flags, previous);
}
// USER: the queue of hwnd becomes the foreground, then hwnd is
// activated in it. The record of the old foreground is the one the
// activation works on, so its active window gets WM_NCACTIVATE and
// WM_ACTIVATE for the change; afterwards the old queue keeps no active
// window. A refused activation leaves the old foreground in place.
unsigned Application::SwitchForeground(unsigned hwnd, unsigned flags)
{
    if (!User::IsWindow(hwnd) || (User::Style(hwnd) & 0xc0000000) == 0x40000000)
        return 0;
    const unsigned target = User::WindowThread(hwnd), old = User::ForegroundQueue();
    if (ActivationTrace())
        fprintf(stderr, "FOREGROUND begin process=%u thread=%u hwnd=%u from=%u to=%u flags=%u\n",
                ProcessId(), ThreadId(), hwnd, old, target, flags);
    if (!target)
        return ActivateInQueue(ThreadId(), hwnd, flags);
    // USER hands a foreign queue the activation work. Its own thread
    // must run CBT hooks (including vetoes) before changing the active window.
    // Running here with only an `acting` queue override called the sender's
    // local hooks for Explorer windows and never called the target's hooks.
    // An internal queued operation also leaves a sleeping target nonblocking;
    // no private window-message number is exposed to guest procedures.
    if (target != ThreadId())
        return QueueForegroundActivation(hwnd, flags);
    if (target == old)
        return ActivateInQueue(target, hwnd, flags);
    const auto parked = User::ThreadActivation(target);
    const auto before = User::ActivationQueue();
    if (flags & ForegroundQueueOnly)
    {
        // USER: ShowWindow's flag 1 changes the foreground
        // queue without activating a different window in that queue yet.
        // An already active window only receives NCACTIVATE and positioning;
        // it must not run the activation hook or gain focus a second time.
        LeaveForeground(true, false);
        if (state == Failed || state == Exited || !User::IsWindow(hwnd) || User::ForegroundQueue()) return 0;
        User::SetForegroundQueue(target);
        User::WriteActivationQueue(parked);
        User::KeepQueueFocus(old, before.focus);
        ActivationEnvironment environment(*this);
        environment.AppMessage(target, old, true);
        if (User::ForegroundQueue() != target || !User::IsWindow(hwnd)) return 0;
        if (parked.active == hwnd)
        {
            SendActivation(hwnd, 0x86, 1, 0);
            if (User::IsWindow(hwnd))
            {
                unsigned position[1][7] = {{hwnd, 0, 0, 0, 0, 0, 3}};
                PositionWindows(1, position);
            }
        }
        return User::IsWindow(hwnd) && User::ForegroundQueue() == target;
    }
    User::SetForegroundQueue(target);
    // The receiving queue temporarily reuses the foreground record to
    // deactivate the previous window. Its owner still reads its own active
    // window and focus during WM_NCACTIVATE / WM_ACTIVATE callbacks.
    if (old) User::WriteThreadActivation(old, before);
    const unsigned result = ActivateInQueue(target, hwnd, flags);
    if (ActivationTrace())
        fprintf(stderr, "FOREGROUND switch process=%u hwnd=%u from=%u to=%u result=%u active=%u\n", ProcessId(), hwnd, old,
                target, result, User::ActivationQueue().active);
    if (User::ForegroundQueue() != target)
    {
        if (ActivationTrace()) fprintf(stderr, "FOREGROUND superseded process=%u expected=%u actual=%u\n", ProcessId(), target, User::ForegroundQueue());
        return result;
    }
    if (User::ActivationQueue().active != hwnd)
    {
        if (ActivationTrace()) fprintf(stderr, "FOREGROUND rollback process=%u from=%u to=%u expected=%u active=%u\n", ProcessId(), target, old, hwnd, User::ActivationQueue().active);
        User::SetForegroundQueue(old);
        User::WriteThreadActivation(target, parked);
        return 0;
    }
    // When a queue's window comes back, the focus goes back to the window
    // that had it when the queue lost the foreground and not to the
    // top-level window, which Explorer's folder would never hand on to its
    // list view.
    const unsigned kept = parked.focus ? parked.focus : User::KeptQueueFocus(target);
    User::KeepQueueFocus(target, 0);
    if (kept && kept != hwnd && User::IsWindow(kept) && User::IsVisible(kept))
    {
        ActivationEnvironment f(*this);
        f.acting = target;
        f.globals.foreground = target;
        if (Win98Activation::Root(f, kept) == hwnd && User::ActivationQueue().focus != kept)
            Win98Focus::Set(f, kept);
    }
    if (old)
    {
        // The old foreground's focus window got WM_KILLFOCUS in the
        // activation above, and its queue keeps no focus: the deactivation
        // of a queue moves +20h to +24h and clears the focus at +1Ch with
        // WM_KILLFOCUS. A SetFocus of its own thread on
        // that window must send WM_SETFOCUS again. Keeping the
        // window as the queue's focus made that SetFocus return at once
        // (focus already there), and the desktop's list view, whose
        // DefView answers WM_MOUSEACTIVATE with MA_NOACTIVATE and which only
        // takes the focus with SetFocus, never showed a selection after the
        // Start menu had been open. The window is remembered for
        // the return.
        Win98Activation::Queue left = {};
        left.previous = before.active;
        User::WriteThreadActivation(old, left);
        User::KeepQueueFocus(old, before.focus);
    }
    // The queue that takes the foreground may have had hwnd as its active
    // window all along (the desktop's Progman when the last folder window is
    // minimized): the activation then sends nothing, and the taskbar kept the
    // minimized window's button pressed. The window is the active one of the
    // foreground now, so the shell hears of it (HSHELL_WINDOWACTIVATED).
    if (parked.active == hwnd)
        User::NotifyActivation(hwnd);
    return result;
}
// A process ends while one of its threads owns the foreground. Win98 destroys
// the windows of the ending thread in USER's cleanup, and the active one
// hands activation to the next window; because the
// ending queue is the foreground, that moves the foreground too. NEO98 takes
// the windows down natively later, so the hand-off happens here, to the next
// eligible window of another process.
void Application::HandForegroundOn()
{
    const unsigned foreground = User::ForegroundQueue();
    bool mine = false;
    for (const auto &t : threads)
        if (t.fiber && foreground && t.id == foreground)
            mine = true;
    if (!mine)
        return;
    const unsigned active = User::ActivationQueue().active;
    if (!active || !User::IsWindow(active))
        return;
    ActivationEnvironment e(*this);
    unsigned next = active;
    for (unsigned n = 0; n < User::WindowCapacity; ++n)
    {
        next = Win98Activation::Candidate(e, next, 0, 0);
        if (!next || next == active)
            return;
        if (User::WindowProcess(next) != process_id)
            break;
    }
    if (!next || !User::IsWindow(next))
        return;
    // The ending queue gives up the foreground record; the new foreground
    // queue activates the window itself, the way posts the change
    // into it, since the ending thread cannot send to its own windows now.
    Win98Activation::Queue q = User::ActivationQueue();
    q.previous = q.active;
    q.active = 0;
    q.focus = 0;
    User::WriteActivationQueue(q);
    const unsigned thread = User::WindowThread(next);
    User::SetForegroundQueue(thread);
    User::PostQueueActivation(thread, next);
}
// USER with a null window: no queue of the guest is the foreground
// any more, because a window outside the guest took it (the host's
// application lost the activation). The press that did it
// went to the queue that held the mouse for a menu first, at a point outside
// the menu, which ends the menu. Then the active window of the old
// foreground is deactivated like for a window of another program:
// WM_NCACTIVATE, WM_ACTIVATE, WM_ACTIVATEAPP to the windows of its task, and
// WM_KILLFOCUS to the focus window. The queue keeps its focus window for the
// way back (SwitchForeground), and its last active window in `previous`.
void Application::LeaveForeground(bool any_queue, bool keep_focus)
{
    const unsigned queue = User::ForegroundQueue();
    if (!queue || (!any_queue && queue != ThreadId()))
        return;
    if (User::PopupActive() || User::BarModeActive())
        User::EndMenu();
    if (state == Failed || state == Exited || User::ForegroundQueue() != queue)
        return;
    ActivationEnvironment e(*this);
    const auto before = User::ActivationQueue();
    const unsigned old = before.active;
    if (ActivationTrace())
        fprintf(stderr, "FOREGROUND leave process=%u thread=%u active=%u focus=%u\n", ProcessId(), queue, old, before.focus);
    if (old && User::IsWindow(old))
    {
        const unsigned minimized = Win98Activation::Iconic(e, old) ? 0x20 : 0;
        const unsigned task = e.Read(old).task;
        // A refusal of WM_NCACTIVATE keeps the caption, not the foreground.
        e.Message(old, 0x86, 0, 0, minimized);
        if (User::IsWindow(old))
            e.Message(old, 6, 0, 0, minimized);
        e.AppMessage(task, 0, false);
        if (state == Failed || state == Exited)
            return;
    }
    // A window procedure may have taken the foreground elsewhere meanwhile.
    if (User::ForegroundQueue() != queue || User::ActivationQueue().active != old)
        return;
    const unsigned focus = User::ActivationQueue().focus;
    User::SetForegroundQueue(0);
    User::WriteActivationQueue({});
    Win98Activation::Queue left = {};
    left.previous = old;
    left.focus = keep_focus ? focus : 0;
    User::WriteThreadActivation(queue, left);
    if (focus && User::IsWindow(focus))
        Send(focus, 8, 0, 0);
}
// USER, SetForegroundWindow. Inside the foreground queue it is an
// activation; into another queue it needs queue flag 10h or an unlocked
// foreground. A locked caller only activates the window in its
// own queue; the flash request to the shell is not built.
unsigned Application::SetForegroundApi(unsigned hwnd)
{
    if (!hwnd || !User::IsWindow(hwnd))
        return 0;
    if (hwnd == User::GetDesktopWindow())
    {
        // The desktop belongs to the system, not to the caller's input
        // queue. Deactivate that queue before publishing the desktop.
        const unsigned previous_queue = User::ForegroundQueue();
        LeaveForeground(true);
        if (state == Failed || state == Exited || User::ForegroundQueue()) return 0;
        // Explicit desktop activation clears the old queue's focus. The
        // host-focus-loss path keeps it for restoration, but the native
        // foreground_state probe observes NULL after this API call.
        if (previous_queue)
        {
            auto previous = User::ThreadActivation(previous_queue);
            previous.focus = 0;
            User::WriteThreadActivation(previous_queue, previous);
        }
        auto queue = User::ActivationQueue();
        queue.active = hwnd; queue.focus = 0;
        User::WriteActivationQueue(queue);
        return 1;
    }
    const unsigned current = ThreadId(), target = User::WindowThread(hwnd), foreground = User::ForegroundQueue();
    if (target && target == foreground)
    {
        if (current == foreground)
            return ActivateWindow(hwnd) ? 1 : 0;
        if (User::ThreadActivation(target).active == hwnd)
            return 1;
        return SwitchForeground(hwnd, 2) ? 1 : 0;
    }
    if (User::QueuePass(current) || User::ForegroundUnlocked(process_id))
    {
        User::TakeQueuePass(current);
        User::TakeStartPermission(process_id);
        return SwitchForeground(hwnd, 2) ? 1 : 0;
    }
    if (!target || target == current)
        return ActivateInQueue(current, hwnd, 2) ? 1 : 0;
    return 0;
}
unsigned Application::ActivationApi(ApiName name, const unsigned *a)
{
    ActivationEnvironment e(*this);
    if (API_IS(name,"GetActiveWindow"))
    {
        const unsigned active = User::ThreadActivation(ThreadId()).active;
        return User::WindowThread(active) == ThreadId() ? active : 0;
    }
    if (API_IS(name,"SetActiveWindow"))
    {
        const unsigned old = User::ThreadActivation(ThreadId()).active;
        if (!a[0])
        {
            // Native null-target policy; the implemented core requires a target.
            if (old)
            {
                CallWindow(old, 0x86, 0, 0);
                if (User::IsWindow(old))
                    CallWindow(old, 6, 0, 0);
            }
            auto q = User::ThreadActivation(ThreadId());
            if (q.active == old)
            {
                q.previous = old;
                q.active = 0;
                User::WriteThreadActivation(ThreadId(), q);
                // Deactivation must not leave keyboard focus in the old
                // window (native Wine win.c:2298/2224). Win98 sometimes
                // fails to deactivate at all; we retain the completed
                // deactivation and clear its focus consistently.
                SetFocus(0);
            }
            return old;
        }
        if (!User::IsWindow(a[0]))
            return 0;
        ActivateWindow(a[0]);
        return old;
    }
    if (API_IS(name,"SetForegroundWindow"))
        return SetForegroundApi(a[0]);
    if (!User::IsWindow(a[0]))
        return 0;
    switch (name.id)
    {
    API_CASE("SwitchToThisWindow")
    {
        Win98Activation::Switch(e, a[0], a[1]);
        return 0;
    }
    API_CASE("GetLastActivePopup")
        return Win98Activation::Popup(e, a[0]);
    API_CASE("IsIconic")
        return Win98Activation::Iconic(e, a[0]);
    API_CASE("IsZoomed")
        return Win98Activation::Zoomed(e, a[0]);
    API_CASE("CloseWindow")
        return Win98Activation::CloseWindow(e, a[0]);
    API_CASE("OpenIcon")
    {
        const unsigned result = Win98Activation::OpenIcon(e, a[0]);
        return result & 0x8000 ? result | 0xffff0000u : result;
    }
    default:
        break;
    }
    return 0;
}
unsigned Application::ShowWindow(unsigned hwnd, unsigned command)
{
    if (!User::IsWindow(hwnd))
        return 0;
    const auto before = User::Style(hwnd);
    // USER: the first SW_SHOWNORMAL or SW_SHOW of a captioned,
    // unowned top-level window takes STARTUPINFO's wShowWindow when
    // STARTF_USESHOWWINDOW is set; SW_SHOWDEFAULT does the same, and without
    // a startup command it is SW_SHOWNORMAL.
    // the first show of such a window uses the startup command
    // up whatever its own command is (clearing the startup flags of the process); only SW_SHOWNORMAL and SW_SHOW are replaced by it.
    // A program that shows its main window with the nCmdShow of WinMain
    // (File Manager, maximized by a shell) left the command for the next
    // captioned window without an owner, and its message box came up
    // maximized.
    // USER first requires a Win32 task (task byte 14h, mask 04h).
    // A Win16 launcher receives nCmdShow through InitTask instead. Applying
    // its parent's SW_HIDE here hides even its modal initialization errors.
    if (!Win16Process() && !startup_show_used && (startup_info[11] & 1) &&
        (before & 0xc00000) == 0xc00000 && !User::Owner(hwnd) && (before & 0xc0000000) != 0x40000000)
    {
        if (command == 1 || command == 5) command = 10;
        else if (command != 10) startup_show_used = true;
    }
    if (command == 10)
    {
        if (!startup_show_used && (startup_info[11] & 1)) command = startup_info[12] & 0xffff;
        startup_show_used = true;
        if (command == 10) command = 1;
    }
    // USER rejects commands above SW_RESTORE and returns the old
    // visibility. SW_FORCEMINIMIZE is not SW_MINIMIZE on Win98.
    if (command > 9)
        return (before & 0x10000000u) != 0;
    const bool visible = before & 0x10000000;
    if(Win98CbtHooks::MinMaxRequest(before,command) &&
       (CbtEvent(1,hwnd,command)||state==Failed||!User::IsWindow(hwnd)))return visible;
    // USER. Minimize, maximize and restore run the state transition; a plain show or hide is one SetWindowPos with
    // SWP_SHOWWINDOW or SWP_HIDEWINDOW, preceded by WM_SHOWWINDOW.
    if (command == 2 || command == 3 || command == 6 || command == 7 ||
        ((command == 1 || command == 4 || command == 9) && (before & 0x21000000)))
        return ShowTransition(hwnd, command, before, visible);
    unsigned flags;
    if (command == 0)
    {
        if (!visible) return 0;
        flags = 0x83; // SWP_HIDEWINDOW|SWP_NOSIZE|SWP_NOMOVE
        if (WindowQueue(hwnd).active != hwnd) flags |= 0x14;
    }
    else if (command == 8) flags = 0x53;
    else
    {
        if (visible) return 1;
        flags = 0x43; // SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE
        if (command == 4) flags |= 0x14;
    }
    // SW_SHOWNA reaches this notification even for an already visible
    // window (USER); other plain show commands returned above.
    if (command == 8 || visible != (command != 0))
    {
        Send(hwnd, 0x18, command != 0, 0);
        if (!User::IsWindow(hwnd)) return visible;
    }
    if ((User::Style(hwnd) & 0xc0000000) == 0x40000000) flags |= 0x14;
    else if (command == 1 || command == 5)
    {
        // USER activates CS_SAVEBITS windows before the
        // positioning operation, which then carries NOACTIVATE|NOZORDER.
        const unsigned args[] = {hwnd, unsigned(-26)};
        if (Api("GetClassLongA", args) & 0x800)
        {
            ActivateWindow(hwnd);
            if (!User::IsWindow(hwnd) || state == Failed || state == Exited) return visible;
            flags |= 0x14;
        }
    }
    const unsigned parent = User::Parent(hwnd);
    if (parent && !User::IsVisible(parent))
        User::ShowWindow(hwnd, command ? 8 : 0); // under a hidden ancestor only the bit changes
    else
    {
        unsigned position[1][7] = {{hwnd, 0, 0, 0, 0, 0, flags}};
        PositionWindows(1, position);
    }
    if (!User::IsWindow(hwnd)) return visible;
    if (User::TakeSizeMove(hwnd))
    {
        // the deferred WM_SIZE and WM_MOVE of an overlapped window.
        const unsigned style = User::Style(hwnd);
        Gdi::RECT r = {};
        User::GetClientRect(hwnd, &r);
        Send(hwnd, 5, style & 0x20000000 ? 1 : style & 0x01000000 ? 2 : 0,
                   (unsigned(r.bottom) & 65535) << 16 | (unsigned(r.right) & 65535));
        if (!User::IsWindow(hwnd)) return visible;
        Gdi::POINT origin = {0, 0};
        User::ScreenToClient(hwnd, &origin);
        origin = {-origin.x, -origin.y};
        if (parent) User::ScreenToClient(parent, &origin);
        Send(hwnd, 3, 0, (unsigned(origin.y) & 65535) << 16 | (unsigned(origin.x) & 65535));
        if (!User::IsWindow(hwnd)) return visible;
    }
    if (command == 0)
    {
        // the active window of the calling queue, when that
        // is the foreground, hands the activation on ( code 3, to the
        // owner of a pop-up first). : then the focus leaves it.
        if (ThreadId() == User::ForegroundQueue() && User::ThreadActivation(ThreadId()).active == hwnd)
            ActivateNext(hwnd, 3);
        if (User::IsWindow(hwnd) && WindowQueueFocus(hwnd) == hwnd)
            SetFocus((User::Style(hwnd) & 0xc0000000) == 0x40000000 ? parent : 0);
    }
    // Plain showing does not restore focus after WINDOWPOSCHANGED. Any
    // activation and its focus change belong to PositionWindows above.
    return visible;
}
// USER sets bit 0 of the task byte 15h while the old active window of
// the queue gets WM_NCACTIVATE and WM_ACTIVATE (win98_activation.h). While it
// is set, DestroyWindow does not hand the activation on.
bool Application::TaskDeactivating(unsigned hwnd)
{
    ActivationEnvironment e(*this);
    return (e.ReadTask(e.Read(hwnd).task).flags15 & 1) != 0;
}
// USER with a code other than 1: Win98Activation::Target chooses the
// window (the owner of a pop-up, the window active before, the next
// top-level window), which is then activated like with code 1.
unsigned Application::ActivateNext(unsigned hwnd, unsigned code)
{
    ActivationEnvironment e(*this);
    const unsigned target = Win98Activation::Target(e, hwnd, code);
    if (ActivationTrace())
        fprintf(stderr, "ACTIVATE next process=%u hwnd=%u code=%u target=%u previous_queue=%u\n", ProcessId(), hwnd,
                code, target, e.globals.previous_queue);
    return target ? ActivateWindow(target) : 0;
}
// The window stays the active one of its queue although nothing took over:
// the queue forgets it and its focus goes (Windows 98 answers null).
void Application::ForgetActive(unsigned hwnd)
{
    const unsigned queue = User::WindowThread(hwnd);
    if (!User::IsWindow(hwnd) || User::ThreadActivation(queue).active != hwnd)
        return;
    auto q = User::ThreadActivation(queue);
    q.previous = hwnd;
    q.active = 0;
    User::WriteThreadActivation(queue, q);
    ActivationEnvironment e(*this);
    e.acting = queue ? queue : ThreadId();
    Win98Focus::Set(e, 0);
}
// USER: the MINMAXINFO of a window. The defaults are published with
// WM_GETMINMAXINFO, the window procedure may change them, then the work-area
// correction applies.
bool Application::QueryMinMax(unsigned hwnd, Win98MinMax::Info &info)
{
    info = {};
    if (!User::MinMaxDefaults(hwnd, info)) return false;
    int wire[10] = {info.reserved.x,   info.reserved.y,   info.max_size.x,
                    info.max_size.y,   info.max_position.x, info.max_position.y,
                    info.min_track.x,  info.min_track.y,  info.max_track.x,
                    info.max_track.y};
    auto* target = static_cast<Application*>(User::ProcessContextOf(User::WindowProcess(hwnd)));
    if (!target) target = this;
    const unsigned address = target->Allocate(sizeof wire);
    if (address)
    {
        target->Store(address, wire, sizeof wire);
        // MINMAXINFO belongs to the receiving process, unlike scalar messages.
        if (target == this) Send(hwnd, 0x24, 0, address);
        else SendCrossProcessRaw(target, hwnd, 0x24, 0, address);
        if (state == Failed || state == Exited) return false;
        // The receiver may have ended while it was asked.
        if (User::ProcessContextOf(User::WindowProcess(hwnd)) != target && target != this) return false;
        if (User::IsWindow(hwnd) && target->Read(address, wire, sizeof wire))
        {
            info.reserved = {wire[0], wire[1]};
            info.max_size = {wire[2], wire[3]};
            info.max_position = {wire[4], wire[5]};
            info.min_track = {wire[6], wire[7]};
            info.max_track = {wire[8], wire[9]};
        }
        target->Free(address);
    }
    return User::IsWindow(hwnd) && User::MinMaxAdjust(hwnd, info);
}
// USER, the state change of ShowWindow for the minimize, maximize and
// restore commands (Windows 98 calls it and leaves: there is no WM_SHOWWINDOW on
// this path). SW_SHOWNOACTIVATE, SW_MINIMIZE and SW_SHOWMINNOACTIVE
// carry SWP_NOACTIVATE while the foreground has an active
// window;
// SW_RESTORE and SW_SHOWNOACTIVATE of a minimized window
// that was maximized are a maximize, everything else that
// opens is the normal state;
// a minimize: nothing for a window that is minimized and
// visible; the focus leaves the window, then
// WS_MINIMIZE, flags 8120h (SWP_STATECHANGED,
// SWP_NOCOPYBITS, SWP_FRAMECHANGED);
// a maximize: nothing for a window that is maximized and
// visible; WM_GETMINMAXINFO;
// a minimized window that opens is asked WM_QUERYOPEN,
// after the WM_GETMINMAXINFO of a maximize;, the style bits, SWP_FRAMECHANGED;, SWP_SHOWWINDOW for a hidden window and for a minimize;
// one BeginDeferWindowPos, DeferWindowPos and
// EndDeferWindowPos: WM_WINDOWPOSCHANGING, WM_NCCALCSIZE,
// the activation, WM_WINDOWPOSCHANGED, out of which
// DefWindowProc sends WM_MOVE and WM_SIZE;
// SW_MINIMIZE of a top-level window hands the activation
// on;
// a window opened out of the minimized state takes the
// focus and, unless it is a child, gets WM_ACTIVATE once
// more.
// SetShowState supplies the icon rectangle; the placement still preserves
// sibling order for SW_MINIMIZE and SW_SHOWMINNOACTIVE.
unsigned Application::ShowTransition(unsigned hwnd, unsigned command, unsigned before, bool visible)
{
    const bool minimized = before & 0x20000000, zoomed = before & 0x01000000;
    const bool minimize = command == 2 || command == 6 || command == 7;
    // SW_NORMALNA is an internal USER command used when an MDI child gives
    // up maximization. Public ShowWindow still rejects it above.
    // SW_NORMALNA restores the previous MDI child without moving it above
    // the child being activated (native WM_WINDOWPOSCHANGING flags 0x8074).
    unsigned flags = command == 0xcc ? 0x14 : 0, after = 0;
    if ((command == 4 || command == 6 || command == 7) && User::ForegroundActiveWindow()) flags |= 0x10;
    Gdi::RECT target = {};
    bool opened = false;
    if (minimize)
    {
        if (minimized)
        {
            if (visible) return visible;
            flags |= 0x17; // only shown
            User::GetWindowRect(hwnd, &target);
            if (const unsigned parent = User::Parent(hwnd))
            {
                Gdi::POINT p = {target.left, target.top};
                User::ScreenToClient(parent, &p);
                target = {p.x, p.y, p.x + (target.right - target.left), p.y + (target.bottom - target.top)};
            }
        }
        else
        {
            flags |= 0x8120;
            if (command != 2)
            {
                after = User::MinimizeInsertAfter(hwnd);
                if (!after) flags |= 4;
            }
            ActivationEnvironment e(*this);
            if (Win98Activation::Root(e, WindowQueueFocus(hwnd)) == hwnd || WindowQueueFocus(hwnd) == hwnd)
                SetFocus((before & 0xc0000000) == 0x40000000 ? User::Parent(hwnd) : 0);
            if (!User::IsWindow(hwnd) || !User::SetShowState(hwnd, 6, target)) return visible;
        }
    }
    else
    {
        const bool maximize = command == 3 || ((command == 9 || command == 4) && minimized && User::RestoresMaximized(hwnd));
        Gdi::RECT maximized = {};
        if (maximize)
        {
            if (zoomed && visible) return visible;
            if (!zoomed) flags |= 0x8000;
            Win98MinMax::Info info = {};
            if (!QueryMinMax(hwnd, info)) return visible;
            maximized = {info.max_position.x, info.max_position.y,
                         info.max_position.x + info.max_size.x,
                         info.max_position.y + info.max_size.y};
        }
        else if (before & 0x21000000) flags |= 0x8000;
        if (minimized)
        {
            if (!Send(hwnd, 0x13, 0, 0) || !User::IsWindow(hwnd)) return visible;
            flags |= 0x100;
            opened = true;
        }
        flags |= 0x20;
        if (!User::SetShowState(hwnd, maximize ? 3 : 1, target)) return visible;
        if (maximize) target = maximized;
        // USER: a visible top-level window restored from an
        // iconic/zoomed state can take the foreground directly. Hidden
        // restoration instead uses the ordinary queue activation below.
        if (visible && (before & 0xc0000000u) != 0x40000000u)
        {
            // Task+40h counts visible, non-iconic top-level windows:
            // USER decrements it on hide/minimize, increments
            // it on restoration. Derive the count from the updated styles.
            unsigned windows[User::WindowCapacity], total = 0, count = 0;
            User::WindowSnapshot(0, windows, User::WindowCapacity, total);
            for (unsigned i = 0; i < total; ++i)
            {
                const unsigned style = User::Style(windows[i]);
                if (User::WindowThread(windows[i]) == User::WindowThread(hwnd) &&
                    (style & 0x30000000u) == 0x10000000u && (style & 0xc0000000u) != 0x40000000u) ++count;
            }
            if (count == 1 && User::WindowThread(hwnd) != User::ForegroundQueue() &&
                (!(flags & 0x10) || WindowQueue(hwnd).active == hwnd))
            {
                SwitchForeground(hwnd, ForegroundQueueOnly);
                if (state == Failed || state == Exited || !User::IsWindow(hwnd)) return visible;
            }
        }
    }
    if (!visible || minimize) flags |= 0x40;
    User::TakeSizeMove(hwnd);
    unsigned position[1][7] = {{hwnd, after, unsigned(target.left), unsigned(target.top), unsigned(target.right - target.left),
                                unsigned(target.bottom - target.top), flags}};
    PositionWindows(1, position);
    if (state == Failed || state == Exited || !User::IsWindow(hwnd)) return visible;
    if (command == 6 && (before & 0xc0000000) != 0x40000000 &&
        User::WindowThread(hwnd) == User::ForegroundQueue() && WindowQueue(hwnd).active == hwnd)
    {
        // USER uses for a found successor. Unlike the
        // fallback, this does not grant the minimizing queue a foreground
        // pass for its next restoration. Background queues keep their active
        // window when it is minimized.
        ActivationEnvironment handoff(*this);
        const unsigned next = Win98Activation::Target(handoff, hwnd, 3);
        if (next && User::WindowThread(next) != ThreadId()) SwitchForeground(next, 2);
        else ActivateNext(hwnd, 3);
        ForgetActive(hwnd);
    }
    ActivationEnvironment e(*this);
    // USER remembers an accepted WM_QUERYOPEN. 3A01 then
    // restores focus even when SWP_NOACTIVATE left this window inactive.
    if (opened && User::IsWindow(hwnd))
    {
        const bool child = (before & 0xc0000000) == 0x40000000;
        if ((child ? WindowQueueFocus(hwnd) : Win98Activation::Root(e, WindowQueueFocus(hwnd))) != hwnd) SetFocus(hwnd);
        if (User::IsWindow(hwnd) && (before & 0xc0000000) != 0x40000000) Send(hwnd, 6, 1, 0);
    }
    return visible;
}
// A top-level WM_CONTEXTMENU affects only the active window. Hit-test lParam
// in screen coordinates: HTCAPTION opens the system menu with SC_MAXIMIZE, or
// SC_RESTORE when minimized/maximized; HTSYSMENU defaults to SC_CLOSE. Other
// locations return zero. Track the menu with right-button support. The
// keyboard path with lParam=-1 is handled elsewhere.
unsigned Application::ContextMenuOfFrame(unsigned hwnd, unsigned lp)
{
    if (lp == 0xffffffffu || WindowQueue(hwnd).active != hwnd)
        return 0;
    const int x = (short)(lp & 0xffff), y = (short)(lp >> 16);
    const unsigned hit = User::FrameHitTest(hwnd, x, y);
    unsigned command = 0;
    if (hit == 2) command = User::Style(hwnd) & 0x21000000 ? 0xf120 : 0xf030;
    else if (hit == 3) command = 0xf060;
    else return 0;
    const unsigned arguments[] = {hwnd, 0};
    const unsigned menu = Api("GetSystemMenu", arguments);
    if (!menu)
        return 0;
    User::SetMenuDefault(menu, command, false);
    User::TrackPopup(menu, 0x202, x, y, hwnd);
    return 0;
}
// GetExpWinVer of an instance handle, as USER's CreateWindowEx asks it: the
// expected version word of the NE header for a 16-bit instance or module (a
// module of our side answers with the file of the tree Win98 loads for it),
// the subsystem version of the PE header for a 32-bit module. Zero when the
// handle names no module.
unsigned Application::InstanceVersion(unsigned instance)
{
    if (!instance)
        return 0;
    if (instance < 0x10000)
    {
        if (!win16.state)
            return 0;
        if (instance >= Win16SystemHandle)
        {
            const Win98Ne::Image *system = Win16SystemImage(instance);
            return system ? system->ExpectedVersion() : 0u;
        }
        for (unsigned i = 0; i < win16.state->module_count; ++i)
        {
            const auto &module = win16.state->modules[i];
            if (module.instance == instance || module.handle == instance)
                return module.image.ExpectedVersion();
        }
        return 0;
    }
    if (!virtual_memory.Access(instance, 0x40, false) || (Read32(instance) & 0xffff) != 0x5a4d)
        return 0;
    const unsigned header = instance + Read32(instance + 0x3c);
    if (header < instance || !virtual_memory.Access(header, 0x60, false) || Read32(header) != 0x4550)
        return 0;
    // MajorSubsystemVersion and MinorSubsystemVersion, optional header +30h.
    const unsigned version = Read32(header + 0x48);
    return ((version & 0xff) << 8) | ((version >> 16) & 0xff);
}
// USER, the key branch of DefWindowProc. Two bits of
// the thread's queue remember ALT going down alone (8) and F10 going down
// (10h); the release of that key sends WM_SYSCOMMAND SC_KEYMENU to the
// window of the top level, and WM_SYSCHAR sends it with the character.
unsigned Application::DefaultKeyMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    const unsigned thread = ThreadId();
    const bool alt = (lp & 0x20000000u) != 0;
    auto shift = [&] { const unsigned key = 0x10; return (Api("GetKeyState", &key) & 0x8000u) != 0; };
    auto f10 = [&] {
        // F10 goes down. With SHIFT a 4.0 window gets WM_CONTEXTMENU
        // for the keyboard (lParam -1).
        User::MenuKeyFlags(thread, 0, 0x10);
        if (shift() && User::ModernWindow(hwnd)) Send(hwnd, 0x7b, hwnd, 0xffffffffu);
        return 0u;
    };
    switch (message)
    {
    case 0x100:
        return wp == 0x79 ? f10() : 0;
    case 0x104:
        if (alt)
        {
            // a first ALT arms the bit, any other key clears it; a
            // repeat changes nothing.
            if (!(lp & 0x40000000u))
            {
                if (wp == 0x12) User::MenuKeyFlags(thread, 0, 8);
                else User::MenuKeyFlags(thread, 8, 0);
            }
            User::MenuKeyFlags(thread, 0x10, 0);
            // ALT+F4 closes the active window of the queue unless its
            // class carries CS_NOCLOSE.
            if (wp == 0x73)
            {
                const unsigned active = OwnQueue().active;
                const unsigned query[] = {active, unsigned(-26)};
                if (active && User::IsWindow(active) && !(Api("GetClassLongA", query) & 0x200u))
                    User::PostMessage(active, 0x112, 0xf060, 0);
            }
            return 0;
        }
        if (wp == 0x79) return f10();
        // SHIFT+ESC opens the system menu.
        if (wp == 0x1b && shift()) Send(hwnd, 0x112, 0xf100, ' ');
        return 0;
    case 0x101:
    case 0x105:
    {
        // WM_KEYUP and WM_SYSKEYUP of an armed ALT or F10.
        const unsigned flags = User::MenuKeyFlags(thread, 0, 0);
        if ((wp == 0x12 && (flags & 8)) || (wp == 0x79 && (flags & 0x10)))
        {
            unsigned top = hwnd;
            for (unsigned n = 0; n < 64 && (User::Style(top) & 0xc0000000u) == 0x40000000u && User::Parent(top); ++n)
                top = User::Parent(top);
            // The release still has to clear USER's key flags when the
            // system command returns, including across a saved menu loop.
            const bool kept = TakeFresh() && PushHostFrame(HostBarMenu, 2, thread);
            if (kept) GiveTail(TailRecord);
            Send(top, 0x112, 0xf100, 0);
            if (kept) PopHostFrame();
        }
        User::MenuKeyFlags(thread, 0x18, 0);
        return 0;
    }
    case 0x106:
        // WM_SYSCHAR.
        User::MenuKeyFlags(thread, 8, 0);
        if (wp == 0x0d && (User::Style(hwnd) & 0x20000000u))
        {
            User::PostMessage(hwnd, 0x112, 0xf120, 0);
            return 0;
        }
        if (alt && wp)
        {
            if (wp == 9 || wp == 0x1b) return 0;
            if (wp == ' ' && (User::Style(hwnd) & 0xc0000000u) == 0x40000000u)
            {
                const unsigned parent = User::Parent(hwnd);
                return parent ? Send(parent, message, wp, lp) : 0;
            }
            TailOfGate(TailZero);
            Send(hwnd, 0x112, 0xf100, wp);
            return 0;
        }
        if (wp != 0x1b)
        {
            const unsigned beep = 0;
            Api("MessageBeep", &beep);
        }
        return 0;
    }
    return 0;
}
// USER with : SC_KEYMENU. A child hands it to the first
// ancestor that is no child or has a system menu of its own; a window of the
// top level to the active window of its queue. That window needs a menu or a
// system menu, and a window of another thread gets the command posted.
unsigned Application::KeyMenuCommand(unsigned hwnd, unsigned lp)
{
    const bool gate = TakeFresh();
    unsigned target = hwnd;
    if ((User::Style(hwnd) & 0xc0000000u) == 0x40000000u)
    {
        for (unsigned n = 0; n < 64 && target && (User::Style(target) & 0xc0000000u) == 0x40000000u &&
                             !(User::Style(target) & 0x80000u); ++n)
            target = User::Parent(target);
    }
    else
        target = WindowQueue(hwnd).active;
    if (!target || !User::IsWindow(target))
        return 0;
    const unsigned style = User::Style(target);
    if ((style & 0xc0000000u) != 0x40000000u && !User::GetMenu(target) && !(style & 0x80000u))
        return 0;
    if (User::WindowThread(target) != ThreadId())
    {
        User::PostMessage(target, 0x112, 0xf100, lp);
        return 0;
    }
    const unsigned arguments[] = {target, 0};
    const unsigned system = Api("GetSystemMenu", arguments);
    if (state == Failed || state == Exited || !User::IsWindow(target))
        return 0;
    const bool kept = gate && PushHostFrame(HostBarMenu, 1);
    User::KeyMenu(target, system, lp & 0xffff);
    if (kept) PopHostFrame();
    if (User::MenuBeepRequest())
    {
        const unsigned beep = 0;
        Api("MessageBeep", &beep);
    }
    return 0;
}
unsigned Application::DefaultWindowMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    // DefWindowProc offers system commands to the calling thread's CBT
    // chain before acting on them. A nonzero hook result cancels the command.
    if (message == 0x112 && (CbtEvent(8, wp, lp) || state == Failed || !User::IsWindow(hwnd)))
        return 0;
    if (message == 0x55) return lp == 3 ? 1 : 0; // WM_NOTIFYFORMAT: NF_QUERY -> NFR_ANSI
    if (message == 0x83 && lp) // WM_NCCALCSIZE: first RECT in either form
    {
        Gdi::RECT rect{};
        if (Read(lp, &rect, sizeof rect))
        {
            User::CalculateClientRect(hwnd, rect);
            Store(lp, &rect, sizeof rect);
        }
        return 0;
    }
    // BM_* belongs to BUTTON's class procedure. Explorer deliberately sends
    // suppressed state changes to DefWindowProc instead of that procedure.
    if (message >= 0xf0 && message <= 0xf7) return 0;
    // WM_MOUSEACTIVATE, USER: a child window asks its parent and takes
    // an answer that is not zero; otherwise a left press on the caption is
    // MA_NOACTIVATE (the handling of WM_NCLBUTTONDOWN activates) and
    // everything else MA_ACTIVATE.
    if (message == 0x21)
    {
        if ((User::Style(hwnd) & 0xc0000000u) == 0x40000000u)
            if (const unsigned parent = User::Parent(hwnd))
                if (const unsigned answer = Send(parent, 0x21, wp, lp) & 0xffff)
                    return answer;
        return (lp & 0xffff) == 2 && (lp >> 16) == 0x201 ? 3 : 1;
    }
    if (message == 0x32) return User::SetWindowHotKey(hwnd, wp); // WM_SETHOTKEY
    if (message == 0x33) return User::WindowHotKey(hwnd);        // WM_GETHOTKEY
    if (message == 0x112 && (wp & 0xfff0) == 0xf140)
        return StartScreenSaver(); // SC_SCREENSAVE, src/x86_screen_saver.cpp
    if (message == 0x112 && (wp & 0xfff0) == 0xf100)
        return KeyMenuCommand(hwnd, lp); // SC_KEYMENU
    if (message == 0x112 && (wp & 0xfff0) == 0xf090)
    {
        // SC_MOUSEMENU: the press on the menu bar that WM_NCLBUTTONDOWN
        // turned into this command drops the popup of the item at the point.
        const bool kept = TakeFresh() && PushHostFrame(HostBarMenu);
        User::MouseMenu(hwnd, (short)(lp & 0xffff), (short)(lp >> 16));
        if (kept) PopHostFrame();
        return 0;
    }
    // The key branch runs first; a native control behind this procedure
    // still gets its keys below.
    if (message == 0x100 || message == 0x101 || message == 0x104 || message == 0x105 || message == 0x106)
        DefaultKeyMessage(hwnd, message, wp, lp);
    if (message == 0x112 && (wp & 0xfff0) == 0xf150)
    {
        // SC_HOTKEY: the window of the hot key comes to the front, restored
        // when it was minimized.
        // The hot key is input: like a click it brings its window to the
        // front whatever queue has the foreground, so the permission of
        // SetForegroundWindow does not apply (hot key of a PIF,
        // test_dos_vm_native scenario hotkey).
        unsigned target = lp ? lp : hwnd;
        if (User::Style(target) & 0x20000000u) ShowWindow(target, 9);
        if (User::IsWindow(target)) SwitchForeground(target, 2);
        return 0;
    }
    // USER, WM_CTLCOLOR. USER32 splits
    // WM_CTLCOLOR's type into the seven messages 0132..0138.
    if (message >= 0x132 && message <= 0x138) {
        const unsigned type=message-0x132;
        unsigned background=5,foreground=8;
        const unsigned face=15,window_text=8,highlight=20,scrollbar=0;
        // USER asks the window, bit 40h of byte 2Eh.
        const bool modern=User::ModernWindow(hwnd);
        if (type==3 && modern) {background=15;foreground=18;}
        else if ((type==0||type==4||type==6) && modern && Api("GetSysColor",&window_text)!=Api("GetSysColor",&face)) background=15;
        else if (type==5) {
            background=20;foreground=18;
            if (Api("GetSysColor",&highlight)!=Api("GetSysColor",&scrollbar) || Api("GetSysColor",&face)==Api("GetSysColor",&highlight)) {
                if (!system_brushes[31]) {
                    const unsigned char bits[16]={0xaa,0,0x55,0,0xaa,0,0x55,0,0xaa,0,0x55,0,0xaa,0,0x55,0};
                    unsigned memory=Allocate(sizeof bits);
                    if(memory){Store(memory,bits,sizeof bits);unsigned args[]={8,8,1,1,memory};
                        unsigned bitmap=GdiApi("CreateBitmap",args);Free(memory);
                        if(bitmap){system_brushes[31]=Gdi::CreatePatternBrush(bitmap);Api("DeleteObject",&bitmap);}}
                }
                Gdi::SetBkColor(wp,Api("GetSysColor",&highlight));
                Gdi::SetTextColor(wp,Api("GetSysColor",&face));
                return system_brushes[31];
            }
        }
        Gdi::SetBkColor(wp,Api("GetSysColor",&background));
        Gdi::SetTextColor(wp,Api("GetSysColor",&foreground));
        return Api("GetSysColorBrush",&background);
    }
    if(message==0xc||message==0xd||message==0xe)return WindowTextDefault(hwnd,message,wp,lp);
    if (message == 0xf)
        return DefaultPaint(hwnd);
    if (message == 0x14)
        return EraseBackground(hwnd, wp);
    if (message == 6 && (wp & 65535) && !(wp >> 16))
        SetFocus(hwnd);
    // USER DefWindowProc WM_SETCURSOR: the parent answers first, then the hit
    // code picks a sizing arrow and the client area the class cursor.
    if (message == 0x20)
        return DefaultSetCursor(hwnd, wp, lp);
    // USER DefWindowProc WM_NCHITTEST: the complete frame result set.
    if (message == 0x84)
        return User::FrameHitTest(hwnd, (short)(lp & 0xffff), (short)(lp >> 16));
    // USER DefWindowProc WM_NCLBUTTONDOWN turns the hit code into a system
    // command the window may intercept, except on the scrollbars, which it
    // tracks itself.
    // A sizing hit becomes SC_SIZE plus its WMSZ code;
    // every other command carries the hit code in its low bits.
    if (message == 0xa1)
    {
        if (wp >= 10 && wp <= 17) { TailOfGate(TailResult); return Send(hwnd, 0x112, wp - 0x1009, lp); } // SC_SIZE|WMSZ
        if (wp == 2)
        {
            // For a caption press, the top-level
            // window is activated with code 6, and
            // the press becomes SC_MOVE only if that succeeded or the window
            // is the active one of the calling queue already. That is why
            // DefWindowProc answers WM_MOUSEACTIVATE for a left press on the
            // caption with MA_NOACTIVATE : this handling of the press
            // brings the window to the front itself.
            ActivationEnvironment e(*this);
            const unsigned top = Win98Activation::Root(e, hwnd);
            if (User::IsWindow(top) && WindowQueue(top).active != top) MouseFocus(top, false);
            if (!User::IsWindow(hwnd)) return 0;
            if (User::IsWindow(top) && WindowQueue(top).active != top &&
                User::ThreadActivation(ThreadId()).active != top)
                return 0;
            TailOfGate(TailResult);
            return Send(hwnd, 0x112, 0xf010 | wp, lp);              // SC_MOVE
        }
        if (wp == 5) { TailOfGate(TailResult); return Send(hwnd, 0x112, 0xf090 | wp, lp); }              // SC_MOUSEMENU
        if (wp == 6) return Send(hwnd, 0x112, 0xf080 | wp, lp);              // SC_HSCROLL
        if (wp == 7) return Send(hwnd, 0x112, 0xf070 | wp, lp);              // SC_VSCROLL
        return 0;                                                            // HTGROWBOX: nothing
    }
    // WM_NCLBUTTONDBLCLK. A double click on the caption maximizes or
    // restores the window (USER, the command with the hit code in its
    // low bits), one on the system menu closes it, as DefWindowProc
    // documents it; any other frame part takes the path of a single press.
    if (message == 0xa3)
    {
        const unsigned style = User::Style(hwnd);
        if (wp == 2)
        {
            // SC_RESTORE for a minimized or maximized window
            // (style byte 33h, bits 21h), else SC_MAXIMIZE with WS_MAXIMIZEBOX.
            if (style & 0x21000000)
                return Send(hwnd, 0x112, 0xf120 | wp, lp); // SC_RESTORE
            if (!(style & 0x10000)) // no WS_MAXIMIZEBOX
                return 0;
            return Send(hwnd, 0x112, 0xf030 | wp, lp); // SC_MAXIMIZE
        }
        if (wp == 3)
            return (style & 0x80000) ? Send(hwnd, 0x112, 0xf060, lp) : 0; // SC_CLOSE
        return DefaultWindowMessage(hwnd, 0xa1, wp, lp);
    }
    if (message == 0x112 && ((wp & 0xfff0) == 0xf070 || (wp & 0xfff0) == 0xf080))
    {
        // USER. The bar tracker owns the pointer until the button
        // comes back up; the pressed region is restored on the way out.
        const bool kept = TakeFresh() && PushHostFrame(HostSizeMove);
        User::BeginScrollTrack(hwnd, (wp & 0xfff0) == 0xf070,
                               (short)(lp & 0xffff), (short)(lp >> 16));
        if (kept) PopHostFrame();
        return 0;
    }
    if (message == 0x112 && ((wp & 0xfff0) == 0xf010 || (wp & 0xfff0) == 0xf000) &&
        (User::Style(hwnd) & 0xc0000000) != 0x40000000)
    {
        // USER into the modal loop. A nonzero hit
        // nibble means the mouse started it, zero the system menu or a key.
        // Without "show window contents while dragging" USER draws an inverted
        // frame and moves the window on release.
        const bool mouse = (wp & 15) != 0;
        const bool kept = TakeFresh() && PushHostFrame(HostSizeMove);
        // Finish the window's pending paints before the outline locks the
        // desktop. A paint handler may skip drawing with an empty clip and
        // still validate its update region, leaving nothing to repaint on
        // unlock. Update only existing damage, including child windows.
        const unsigned style = User::Style(hwnd);
        if (!User::FrameTrackActive() && !(style & 0x21000000u) &&
            ((wp & 0xfff0) == 0xf010 || (style & 0x40000u)))
            UpdateWindowTree(hwnd, 0);
        User::BeginSizeMove(hwnd, wp & 0xffff, mouse, (short)(lp & 0xffff), (short)(lp >> 16));
        if (kept) PopHostFrame();
        return 0;
    }
    if (message == 0x112)
    {
        if (User::Style(hwnd) & 0x08000000u) return 0;
        switch (wp & 0xfff0)
        {
        case 0xf060:
            Send(hwnd, 0x10, 0, 0); return 0;
        case 0xf020:
            ShowWindow(hwnd, 6); return 0;
        case 0xf030:
            ShowWindow(hwnd, 3); return 0;
        case 0xf120:
            ShowWindow(hwnd, 9); return 0;
        }
    }
    // A nonclient right-button press applies only to the active window at
    // HTCAPTION or HTSYSMENU. Windows 98 captures the mouse until release,
    // checks the release position and sends WM_CONTEXTMENU. This procedure
    // records the initial hit instead of running a nested message loop, then
    // checks the same conditions on release. The press returns zero.
    if (message == 0xa4)
    {
        frame_right_window = 0;
        frame_right_hit = 0;
        if (WindowQueue(hwnd).active == hwnd && (wp == 2 || wp == 3))
        {
            frame_right_window = hwnd;
            frame_right_hit = wp;
        }
        return 0;
    }
    if (message == 0xa5)
    {
        const unsigned pressed = frame_right_window, hit = frame_right_hit;
        frame_right_window = 0;
        frame_right_hit = 0;
        if (pressed != hwnd || WindowQueue(hwnd).active != hwnd)
            return 0;
        const unsigned now = User::FrameHitTest(hwnd, (short)(lp & 0xffff), (short)(lp >> 16));
        if (now != hit)
            return 0;
        return Send(hwnd, 0x7b, hwnd, lp);
    }
    // Convert WM_RBUTTONUP to WM_CONTEXTMENU for this window, translating
    // client coordinates to screen coordinates and placing the window handle
    // in wParam.
    if (message == 0x205)
    {
        Gdi::POINT origin = {0, 0};
        User::ScreenToClient(hwnd, &origin);
        const int x = (short)(lp & 0xffff) - origin.x, y = (short)(lp >> 16) - origin.y;
        return Send(hwnd, 0x7b, hwnd, (unsigned(y) & 65535) << 16 | (unsigned(x) & 65535));
    }
    // Forward an unhandled child WM_CONTEXTMENU to its owner with the child
    // in wParam, allowing shell views to build context menus for list-view
    // clicks. Top-level windows respond only when active and the point hits
    // the caption or system menu.
    if (message == 0x7b)
    {
        if ((User::Style(hwnd) & 0xc0000000) == 0x40000000)
        {
            const unsigned parent = User::Parent(hwnd);
            return parent ? Send(parent, 0x7b, wp, lp) : 0;
        }
        return ContextMenuOfFrame(hwnd, lp);
    }
    // A WM_MOUSEWHEEL no window procedure took, at a window with a vertical
    // standard scroll bar: the lines as WM_VSCROLL, SB_LINEUP or SB_LINEDOWN
    // each and SB_ENDSCROLL at the end. Win98 itself only hands the message to
    // the parent (below); the mouse drivers of the time (IntelliPoint) did
    // this for the programs that do not know the wheel, 16-bit ones and old
    // Win32 ones, and so does the list box here.
    if (message == 0x20a && (User::Style(hwnd) & 0x00200000))
    {
        const int lines = WheelLines(hwnd, (int)(short)(wp >> 16));
        for (int i = 0, n = lines < 0 ? -lines : lines; i < n && i < 60 && User::IsWindow(hwnd); ++i)
            Send(hwnd, 0x115, lines > 0 ? 0 : 1, 0);
        if (lines && User::IsWindow(hwnd))
            Send(hwnd, 0x115, 8, 0);
        return 0;
    }
    // The same for a program that scrolls with a scroll bar of its own, a
    // SCROLLBAR control: the wheel over the bar, or over the window the bar
    // is the vertical scrollbar and moves it as its arrows do, WM_VSCROLL to
    // the bar's parent with the bar in lParam. With more than one
    // vertical bar nobody can say which is meant, and the message goes on.
    if (message == 0x20a)
    {
        const auto vertical = [&](unsigned h) {
            char name[16] = "";
            // SBS_VERT; not SBS_SIZEBOX or SBS_SIZEGRIP.
            if (!User::IsVisible(h) || !User::IsEnabled(h) || !User::GetClassName(h, name, sizeof name) ||
                (User::Style(h) & 0x19) != 1)
                return false;
            for (unsigned i = 0; i < 10; ++i)
                if ((name[i] | 0x20) != "scrollbar"[i] && (name[i] || "scrollbar"[i]))
                    return false;
            return true;
        };
        unsigned bar = 0, bars = 0;
        if (vertical(hwnd))
            bar = hwnd, bars = 1;
        else
            for (unsigned h = User::GetTopWindow(hwnd); h; h = User::GetWindow(h, 2))
                if (vertical(h))
                    bar = h, ++bars;
        const unsigned owner = bars == 1 ? User::Parent(bar) : 0;
        if (owner)
        {
            const int lines = WheelLines(bar, (int)(short)(wp >> 16));
            for (int i = 0, n = lines < 0 ? -lines : lines; i < n && i < 60 && User::IsWindow(bar); ++i)
                Send(owner, 0x115, lines > 0 ? 0 : 1, bar);
            if (lines && User::IsWindow(bar))
                Send(owner, 0x115, 8, bar);
            return 0;
        }
    }
    // Forward an unhandled WM_MOUSEWHEEL through the same path.
    if (message == 0x20a && (User::Style(hwnd) & 0xc0000000) == 0x40000000)
    {
        const unsigned parent = User::Parent(hwnd);
        return parent ? Send(parent, 0x20a, wp, lp) : 0;
    }
    if (message == 0x10)
        return Api("DestroyWindow", &hwnd);
    if (message == 0x80) // WM_SETICON: ICON_SMALL 0 / ICON_BIG 1
    {
        const bool large = wp == 1;
        const unsigned previous = User::SetWindowIcon(hwnd, large, lp);
        // USER: a window that gets a big icon and carries no small one
        // gets a small one too. asks CopyImage for the same icon at
        // SM_CXSMICON by SM_CYSMICON with LR_COPYFROMRESOURCE and remembers in
        // the window word at 38h that USER owns that copy. Without this step
        // WM_GETICON with ICON_SMALL answers nothing for a program that sets
        // only the big icon, which is what Delphi does.
        if (large && lp && !User::WindowIcon(hwnd, false) && !User::ClassAccess(hwnd, -34, 0, 2))
        {
            const unsigned copy[] = {lp, 1, SystemMetric(49), SystemMetric(50), 0x4000};
            if (const unsigned small = ImageApi("CopyImage", copy))
                User::SetWindowIcon(hwnd, false, small);
        }
        return previous;
    }
    if (message == 0x7f) // WM_GETICON: ICON_SMALL 0 / ICON_BIG 1 / ICON_SMALL2 2
        return User::WindowIcon(hwnd, wp == 1);
    if (message == 0x46 && lp)
    {
        // USER with : unless SWP_NOSIZE stands, a window of the
        // top level (neither WS_CHILD nor WS_POPUP) or one with WS_THICKFRAME
        // has the size of the WINDOWPOS held between its tracking sizes; the
        // window is asked with WM_GETMINMAXINFO for them. A
        // minimized window is held between the first point of the MINMAXINFO
        // and its maximized size.
        unsigned pos[7] = {};
        if (!Read(lp, pos, sizeof pos) || (pos[6] & 1)) return 0;
        const unsigned style = User::Style(hwnd);
        if ((style & 0xc0000000) && !(style & 0x00040000)) return 0;
        Win98MinMax::Info info = {};
        if (!QueryMinMax(hwnd, info) || !Read(lp, pos, sizeof pos)) return 0;
        const bool iconic = User::Style(hwnd) & 0x20000000;
        const auto low = iconic ? info.reserved : info.min_track, high = iconic ? info.max_size : info.max_track;
        int cx = int(pos[4]), cy = int(pos[5]);
        if (cx > high.x) cx = high.x;
        if (cx < low.x) cx = low.x;
        if (cy > high.y) cy = high.y;
        if (cy < low.y) cy = low.y;
        pos[4] = unsigned(cx); pos[5] = unsigned(cy);
        Store(lp, pos, sizeof pos);
        return 0;
    }
    if (message == 0x47 && lp)
    {
        // The WM_WINDOWPOSCHANGED worker
        // reads only the two client flags of the WINDOWPOS: skips
        // WM_MOVE when SWP_NOCLIENTMOVE (0x1000) stands, and Windows 98 sends
        // WM_SIZE when SWP_STATECHANGED (0x8000) stands or SWP_NOCLIENTSIZE
        // (0x800) does not. SWP_NOMOVE and SWP_NOSIZE are never consulted:
        // a SetWindowPos that keeps position and size but changes the frame
        // still reports the new client area. WM_SIZE carries the state in
        // wParam.
        unsigned pos[7] = {};
        Read(lp, pos, sizeof pos);
        Gdi::RECT client = {};
        User::GetClientRect(hwnd, &client);
        if (!(pos[6] & 0x1000) && User::IsWindow(hwnd))
        {
            Gdi::POINT origin = {0, 0};
            { User::ScreenToClient(hwnd, &origin); origin = {-origin.x, -origin.y}; }
            if (const unsigned parent = User::Parent(hwnd)) User::ScreenToClient(parent, &origin);
            CallWindow(hwnd, 3, 0, (unsigned(origin.y) & 65535) << 16 | (unsigned(origin.x) & 65535));
        }
        if ((!(pos[6] & 0x800) || (pos[6] & 0x8000)) && User::IsWindow(hwnd))
        {
            size_reported = hwnd;
            const unsigned style = User::Style(hwnd);
            // The port's minimized window keeps its rectangle; a program is
            // told the size nought, as before the transition went this way.
            CallWindow(hwnd, 5, style & 0x20000000 ? 1 : style & 0x01000000 ? 2 : 0,
                       style & 0x20000000 ? 0 : (unsigned(client.bottom) & 65535) << 16 | (unsigned(client.right) & 65535));
        }
        return 0;
    }
    return User::DefaultMessage(hwnd, message, wp, lp);
}
} // namespace X86

namespace X86
{
// The lines a delta of WM_MOUSEWHEEL scrolls, three a notch
// (SPI_GETWHEELSCROLLLINES), positive upwards. What does not make a line yet
// is kept for the next message to the same window in the same direction.
int Application::WheelLines(unsigned hwnd, int delta)
{
    // A control of the port or DefWindowProc counts the lines: the message
    // was understood, whatever its size.
    User::NoteWheelActivity(hwnd);
    static unsigned window = 0;
    static int rest = 0;
    if (window != hwnd || (rest && (rest < 0) != (delta < 0))) rest = 0;
    window = hwnd;
    rest += delta;
    const int lines = rest / 40;
    rest -= lines * 40;
    return lines;
}
} // namespace X86
