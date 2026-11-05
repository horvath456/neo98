// SPDX-License-Identifier: GPL-2.0-or-later
// WinEvents: SetWinEventHook, UnhookWinEvent and NotifyWinEvent.
// USER32 SetWinEventHook marks the flags with 8000h for a Win32 caller, then
// delegates to USER. NotifyWinEvent returns immediately for an empty hook
// list. Hook records occupy 2Ch bytes in USER's shared heap and carry the
// signature 'Laur'. Here they occupy slots in a system-wide table, and each
// handle identifies its slot.
// Notification filters every hook against the event range, the process and thread
// filters and the two SKIPOWN flags, then either calls an in-context hook at
// once in the notifying thread (a Win32 hook of another process gets
// its module loaded there first) or posts an event record to the
// queue of the hook's owner, where the owner's next
// message retrieval delivers it. The procedure gets (hook, event, hwnd,
// idObject, idChild, idEventThread, dwmsEventTime).
// USER.EXE raises events of its own in about sixty places (window creation,
// focus, menus, dialogs, scroll bars, the caret, the Alt-Tab switcher). This
// port raises only the events a program raises with NotifyWinEvent.
//
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
struct EventHook
{
    bool used = false, dead = false;
    unsigned generation = 1, use = 0, minimum = 0, maximum = 0, order = 0;
    bool win32 = false, skip_thread = false, skip_process = false, in_context = false;
    unsigned process_filter = 0, thread_filter = 0, owner_thread = 0, owner_process = 0;
    unsigned procedure = 0, module = 0;
    Application *owner = nullptr;
    char path[260] = {};
};
constexpr unsigned EventHookCapacity = 64;
EventHook event_hooks[EventHookCapacity];
unsigned event_hook_count = 0, event_hook_order = 0;
// The event records of that wait in an owner's queue.
struct EventRecord
{
    bool used = false;
    unsigned hook = 0, event = 0, hwnd = 0, object = 0, child = 0, thread = 0, time = 0;
    Application *target = nullptr;
    unsigned target_thread = 0;
};
constexpr unsigned EventRecordCapacity = 256;
EventRecord event_records[EventRecordCapacity];
constexpr unsigned HandleBase = 0x7e000000u;
unsigned HandleOf(unsigned slot) { return HandleBase | (event_hooks[slot].generation & 0xffff) << 8 | (slot + 1); }
EventHook *EventHookOf(unsigned handle, unsigned &slot)
{
    if ((handle & 0xff000000u) != HandleBase) return nullptr;
    slot = (handle & 0xff) - 1;
    if (slot >= EventHookCapacity || !event_hooks[slot].used || (event_hooks[slot].generation & 0xffff) != ((handle >> 8) & 0xffff))
        return nullptr;
    return &event_hooks[slot];
}
void ReleaseEventHook(unsigned slot)
{
    // unlink, drop the event_records records of the hook, free.
    for (auto &p : event_records)
        if (p.used && p.hook == HandleOf(slot)) p = {};
    const unsigned generation = event_hooks[slot].generation + 1;
    event_hooks[slot] = {};
    event_hooks[slot].generation = generation;
    if (event_hook_count) --event_hook_count;
    User::SetWinEventListening(event_hook_count != 0);
}
} // namespace

unsigned Application::WinEventApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    // and USER.
    API_CASE("SetWinEventHook")
    {
        const unsigned flags = a[6] | 0x8000;
        if (flags & 0x7ff8)
            return 0;
        EventHook fresh;
        fresh.in_context = (flags & 4) != 0;
        fresh.win32 = true;
        if (fresh.in_context)
        {
            // A Win32 in-context hook needs its module and the module's file
            // name, which another process loads (KERNEL.487, then lstrlen).
            if (!a[2])
                return 0;
            const unsigned buffer = Allocate(0x100);
            if (!buffer)
                return 0;
            const unsigned query[3] = {a[2], buffer, 0x100};
            const unsigned length = ModuleApi("GetModuleFileNameA", query);
            if (length)
                snprintf(fresh.path, sizeof fresh.path, "%s", ReadString(buffer));
            Free(buffer);
            if (!length)
                return 0;
        }
        unsigned slot = EventHookCapacity;
        for (unsigned i = 0; i < EventHookCapacity; ++i)
            if (!event_hooks[i].used)
            {
                slot = i;
                break;
            }
        if (slot == EventHookCapacity)
            return 0; // the shared heap has no room:
        fresh.used = true;
        fresh.generation = event_hooks[slot].generation;
        fresh.minimum = a[0] & 0xffff;
        fresh.maximum = a[1] & 0xffff;
        fresh.skip_thread = (flags & 1) != 0;
        fresh.skip_process = (flags & 2) != 0;
        fresh.process_filter = a[4];
        fresh.thread_filter = a[5];
        fresh.owner_thread = ThreadId();
        fresh.owner_process = ProcessId();
        fresh.procedure = a[3];
        fresh.module = a[2];
        fresh.owner = this;
        // puts the new record at the head of the list.
        fresh.order = ++event_hook_order;
        event_hooks[slot] = fresh;
        ++event_hook_count;
        User::SetWinEventListening(true);
        return HandleOf(slot);
    }
    // a readable record with the signature, not dead, owned by the
    // calling thread. The record goes when no event of it is on its way.
    API_CASE("UnhookWinEvent")
    {
        unsigned slot = 0;
        EventHook *hook = EventHookOf(a[0], slot);
        if (!hook || hook->dead || hook->owner_thread != ThreadId())
            return 0;
        hook->dead = true;
        if (!hook->use)
            ReleaseEventHook(slot);
        return 1;
    }
    API_CASE("NotifyWinEvent")
    {
        if (!event_hook_count)
            return 0;
        if (a[1] && !User::IsWindow(a[1]))
            return 0;
        WinEventNotify(a[0], a[1], a[2], a[3], true);
        return 0;
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}


void Application::WinEventNotify(unsigned event, unsigned hwnd, unsigned object, unsigned child, bool window_queue)
{
    unsigned process = ProcessId(), thread = ThreadId();
    if (window_queue && hwnd)
    {
        process = User::WindowProcess(hwnd);
        thread = User::WindowThread(hwnd);
    }
    const unsigned time = ThreadApi("GetTickCount", nullptr);
    // The list from its head: the newest hook first.
    unsigned visited = ~0u;
    for (;;)
    {
        unsigned slot = EventHookCapacity, best = 0;
        for (unsigned i = 0; i < EventHookCapacity; ++i)
            if (event_hooks[i].used && event_hooks[i].order < visited && event_hooks[i].order > best)
            {
                best = event_hooks[i].order;
                slot = i;
            }
        if (slot == EventHookCapacity)
            break;
        visited = best;
        EventHook &hook = event_hooks[slot];
        if (hook.dead)
            continue;
        const unsigned word = event & 0xffff;
        if (word < hook.minimum || word > hook.maximum)
            continue;
        if (hook.process_filter && hook.process_filter != process)
            continue;
        if (hook.skip_process && hook.owner_process == process)
            continue;
        if (hook.thread_filter && hook.thread_filter != thread)
            continue;
        if (hook.skip_thread && hook.owner_thread == thread)
            continue;
        const unsigned handle = HandleOf(slot);
        if (hook.in_context)
        {
            // the procedure in this process. A hook of another
            // process has its module loaded here, and the procedure moves
            // with the module base.
            unsigned procedure = hook.procedure;
            if (hook.owner != this)
            {
                const unsigned text = Allocate(260);
                unsigned base = 0;
                if (text)
                {
                    Store(text, hook.path, (unsigned)strlen(hook.path) + 1);
                    base = Api("LoadLibraryA", &text);
                    Free(text);
                }
                if (!base)
                    continue;
                procedure = hook.procedure - hook.module + base;
            }
            ++hook.use;
            const unsigned arguments[7] = {handle, event, hwnd, object, child, thread, time};
            Call(procedure, arguments, 7);
            if (!--hook.use && hook.dead)
                ReleaseEventHook(slot);
            continue;
        }
        for (auto &p : event_records)
            if (!p.used)
            {
                p = {true, handle, event, hwnd, object, child, thread, time, hook.owner, hook.owner_thread};
                ++hook.use;
                break;
            }
    }
    // Out-of-context callbacks wait for the owner's message retrieval, even
    // when it is also the notifying thread. USER conditionally calls
    // 086E with its last argument zero: 08C4..08D0 skips records whose hook
    // is still present. That cleanup does not deliver these queued callbacks.
}
bool Application::HasWinEvents(unsigned thread_index) const
{
    for (const auto &p : event_records)
        if (p.used && p.target == this && p.target_thread == threads[thread_index].id)
            return true;
    return false;
}
// The queue of the current thread hands its event records to their event_hooks,
// oldest first, as the owner's message retrieval does.
void Application::ProcessWinEvents()
{
    for (unsigned round = 0; round < EventRecordCapacity; ++round)
    {
        EventRecord *next = nullptr;
        for (auto &p : event_records)
            if (p.used && p.target == this && p.target_thread == ThreadId())
            {
                next = &p;
                break;
            }
        if (!next || state == Failed || state == Exited)
            return;
        const EventRecord record = *next;
        *next = {};
        unsigned slot = 0;
        EventHook *hook = EventHookOf(record.hook, slot);
        if (!hook)
            continue;
        if (!hook->dead)
        {
            const unsigned arguments[7] = {record.hook, record.event, record.hwnd, record.object,
                                           record.child, record.thread, record.time};
            Call(hook->procedure, arguments, 7);
        }
        hook = EventHookOf(record.hook, slot);
        if (hook && !--hook->use && hook->dead)
            ReleaseEventHook(slot);
    }
}
// A process that ends takes its event_hooks and the events meant for it along.
void Application::FreeWinEvents()
{
    for (auto &p : event_records)
        if (p.used && p.target == this)
        {
            unsigned slot = 0;
            if (EventHook *hook = EventHookOf(p.hook, slot); hook && hook->use)
                --hook->use;
            p = {};
        }
    for (unsigned slot = 0; slot < EventHookCapacity; ++slot)
        if (event_hooks[slot].used && event_hooks[slot].owner == this)
            ReleaseEventHook(slot);
}
// NotifyWinEvent's early exit.
bool Application::WinEventHooksInstalled() { return event_hook_count != 0; }
} // namespace X86
