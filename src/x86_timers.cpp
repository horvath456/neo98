// SPDX-License-Identifier: GPL-2.0-or-later
// New native USER timers, delivered through the owning thread's message loop.
#include "x86_app.h"
#include <string.h>
namespace X86
{
unsigned Application::TimerApi(ApiName name, const unsigned *a)
{
    const bool set = API_IS(name,"SetTimer");
    const bool exact = exact_timer_request;
    exact_timer_request = false;
    if (a[0] && !User::IsWindow(a[0]))
    {
        last_error = 1400;
        return 0;
    }
    // A window timer belongs to the thread that owns the window, whoever
    // sets or kills it: Win98 USER keeps it in the window's queue. Mozilla's
    // event queue sets one from its socket threads on the main thread's
    // event window (plevent.c, when input is pending) and relies on the main
    // thread's WM_TIMER.
    unsigned owner = current_thread;
    if (a[0])
    {
        const unsigned id = User::WindowThread(a[0]);
        for (unsigned i = 0; i < sizeof threads / sizeof *threads; ++i)
            if (threads[i].fiber && !threads[i].done && threads[i].id == id) { owner = i; break; }
    }
    Timer *timer = nullptr;
    for (auto &t : timers)
        if (!t.caret && t.used && t.thread == owner && t.hwnd == a[0] && t.id == a[1])
        {
            timer = &t;
            break;
        }
    if (!set)
    {
        if (!timer)
            return 0;
        timer->used = false;
        return 1;
    }
    if (!timer)
    {
        for (auto &t : timers)
            if (!t.used && !t.pending)
            {
                timer = &t;
                t = Timer{};
                break;
            }
        if (!timer)
        {
            last_error = 8;
            return 0;
        }
        timer->hwnd = a[0];
        timer->id = a[0] ? a[1] : next_timer++;
        timer->thread = owner;
    }
    timer->used = true;
    if(!timer->pending)timer->wait_observed=false;
    timer->procedure = a[3];
    // USER.EXE counts its timers down from the callback of one system timer,
    // which it re-arms for the earliest due timer with
    // CreateSystemTimer. SYSTEM.DRV's CreateSystemTimer
    // raises every rate below 37h to 37h, so no USER timer of Win98
    // comes faster than every 55 ms; a longer one keeps its period on
    // average, because SYSTEM.DRV adds the rate back to the countdown.
    // A game that animates with a 25 ms WM_TIMER moved twice as
    // fast here as under Win98 (Bow and Arrow).
    constexpr unsigned SystemTimerFloor = 0x37;
    const unsigned floor = exact ? 10u : SystemTimerFloor;
    timer->interval = a[2] < floor ? floor : a[2] > 0x7fffffff ? 0x7fffffff : a[2];
    timer->deadline = Now() + (unsigned long long)timer->interval * 1000;
    return timer->id ? timer->id : 1;
}
bool Application::TimerReady(unsigned thread) const
{
    const auto &wait=threads[thread];
    if ((wait.message_minimum || wait.message_maximum) &&
        (wait.message_minimum>0x113 || wait.message_maximum<0x113)) return false;
    auto now = Now();
    for (const auto &timer : timers)
        if (!timer.caret && timer.thread == thread && (!wait.message_window || timer.hwnd==wait.message_window) &&
            (!timer.hwnd || User::IsWindow(timer.hwnd)) &&
            (timer.pending || (timer.used && now >= timer.deadline)))
            return true;
    return false;
}
bool Application::TimerMessage(User::Message &message, unsigned hwnd, unsigned minimum, unsigned maximum,
                               bool remove)
{
    if ((minimum || maximum) && (minimum > 0x113 || maximum < 0x113))
        return false;
    auto now = Now();
    for (auto &timer : timers)
    {
        if (timer.caret || timer.thread != current_thread || (hwnd && timer.hwnd != hwnd))
            continue;
        if (timer.hwnd && !User::IsWindow(timer.hwnd))
        {
            timer = Timer{};
            continue;
        }
        if (!timer.pending && timer.used && now >= timer.deadline)
        {
            timer.pending = true;
            timer.message = {timer.hwnd, 0x113,  timer.id, timer.procedure, (unsigned)(now / 1000),
                             (short)mouse_x,    (short)mouse_y};
            timer.deadline = now + (unsigned long long)timer.interval * 1000;
        }
        if (timer.pending)
        {
            message = timer.message;
            if (remove){
                timer.pending = false;timer.wait_observed=false;
            }
            return true;
        }
    }
    return false;
}
// The original DialogBox and menu loops call GetMessage, which reads the queue
// and the timer list. The native modal loops own their own wait, so they must
// read both sources too; otherwise a due WM_TIMER keeps the thread runnable
// while the loop finds nothing and never leaves its wait.
bool Application::ModalMessage(User::Message &message, bool remove)
{
    // A modal loop of USER (message box, dialog box, menu, move and size)
    // runs inside USER without an API call of the program. Each turn of it is
    // progress the host's hang observation counts (Progress); before, an
    // open message box read as a hang after three seconds. Without remove
    // the loop looks at the next message and leaves it in the queue, after
    // the input packets have been dispatched.
    ++native_turns;
    ProcessSentMessages();
    // The foreground left the guest (LeaveForeground): an open menu ends here.
    if (User::TakeQueueLeave(ThreadId())) LeaveForeground();
    if (User::PeekMessage(&message, 0, 0, 0, remove, current_thread != InputThread() || !InputPending()))
        return true;
    // Packets held back behind an unread press of another thread of this
    // process are not pending; the loop must not spin on them.
    while (current_thread == InputThread() && InputPending() && state != Failed && state != Exited)
    {
        DrainInput();
        if (User::PeekMessage(&message, 0, 0, 0, remove, !InputPending())) return true;
    }
    if (!TimerMessage(message, 0, 0, 0, remove))
        return false;
    if (remove) Win98MessageMetadata::Posted(threads[current_thread].message_metadata,
                                 {message.time, Win98MessageMetadata::Pack(message.x, message.y), 0}, 1);
    return true;
}
} // namespace X86
