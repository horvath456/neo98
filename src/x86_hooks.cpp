// SPDX-License-Identifier: GPL-2.0-or-later
// Native in-process providers for implemented USER hook chains.
#include "x86_app.h"
#include "arm64_cpu.h"
#include "win98_cbt_hooks.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
// A hook for all threads of the session. Windows keeps the DLL and the
// procedure and loads the DLL into every process an event of the hook's type
// happens in. A DLL is code for one kind of processor: a process of the other
// kind (ARM64 beside x86) cannot load it, and the hook is not called there.
struct SessionHook
{
    unsigned id = 0;
    int type = 0;
    Application *owner = nullptr;
    bool arm64 = false;
    unsigned offset = 0; // of the procedure in its module
    int kind = -1;       // of the procedure, for an ARM64 module (Arm64::CallbackTarget)
    char module[260] = {};
};
constexpr unsigned Missing = Application::ModuleCapacity;
SessionHook session_hooks[16];
unsigned session_hook_serial = 0, session_hook_generation = 0;
} // namespace
struct Application::HookEnvironment final : Win98Hooks::Environment
{
    Application &app;
    // Every use of the chains begins here, so this is where a process catches
    // up with the hooks of the session.
    explicit HookEnvironment(Application &a) : app(a)
    {
        if (app.session_hooks_seen != session_hook_generation) app.SyncSessionHooks();
    }
    HookSlot *Slot(unsigned h) const
    {
        if (!h)
            return nullptr;
        for (auto &slot : app.hooks)
            if (slot.handle == h)
                return &slot;
        return nullptr;
    }
    unsigned TaskIndex(unsigned task) const
    {
        for (unsigned i = 0; task && i < Application::ThreadCapacity; ++i)
            if (app.threads[i].id == task)
                return i + 1;
        return 0;
    }
    unsigned OwnerIndex(unsigned task) const
    {
        for (unsigned i = 1; task && i < 129; ++i)
            if (app.hook_owners[i] == task)
                return i;
        return 0;
    }
    bool AddOwner(unsigned task)
    {
        if (OwnerIndex(task))
            return true;
        for (unsigned i = 1; i < 129; ++i)
            if (!app.hook_owners[i])
            {
                app.hook_owners[i] = task;
                return true;
            }
        return false;
    }
    bool Valid(unsigned h) const override { return Slot(h) != nullptr; }
    Win98Hooks::Record Read(unsigned h) const override
    {
        auto *slot = Slot(h);
        return slot ? slot->record : Win98Hooks::Record{};
    }
    void Write(unsigned h, const Win98Hooks::Record &r) override
    {
        if (auto *slot = Slot(h))
            slot->record = r;
    }
    unsigned Head(int type, unsigned task) const override
    {
        const auto owner = OwnerIndex(task);
        return type >= -1 && type <= 12 && (!task || owner) ? app.hook_heads[owner][type + 1] : 0;
    }
    void SetHead(int type, unsigned task, unsigned h) override
    {
        app.hook_heads[OwnerIndex(task)][type + 1] = h;
    }
    unsigned CurrentTask() const override { return app.ThreadId(); }
    unsigned TaskFlags(unsigned) const override { return 0; }
    unsigned CurrentHook(unsigned task) const override { return app.current_hooks[TaskIndex(task)]; }
    void SetCurrentHook(unsigned task, unsigned h) override { app.current_hooks[TaskIndex(task)] = h; }
    bool DebugPresent(unsigned) const override { return false; } // WH_DEBUG registration is unavailable.
    unsigned DebugHook(unsigned, int, unsigned, unsigned) override { return 0; }
    unsigned Callback(unsigned, unsigned procedure, int code, unsigned wp, unsigned lp) override
    {
        const unsigned args[] = {static_cast<unsigned>(code), wp, lp};
        return app.Call(procedure, args, 3);
    }
    void Dispose(unsigned h) override
    {
        if (auto *slot = Slot(h))
        {
            const auto owner = OwnerIndex(slot->record.task);
            *slot = {};
            if (owner)
            {
                for (auto head : app.hook_heads[owner])
                    if (head)
                        return;
                app.hook_owners[owner] = 0;
            }
        }
    }
    bool HooksDisabled() const override { return false; }
};
#include "x86_cbt_hooks.inc"
// The type of the hook this thread is dispatching. The 16-bit callback needs it
// to know whether lParam points to a structure it has to narrow.
int Application::CurrentHookType() const
{
    const unsigned handle = current_hooks[current_thread + 1];
    if (!handle)
        return -2;
    for (const auto &slot : hooks)
        if (slot.handle == handle)
            return slot.record.type;
    return -2;
}
unsigned Application::MessageHook(int type, int code, unsigned wp, unsigned message)
{
    HookEnvironment e(*this);
    return Win98Hooks::Dispatch(e, Win98Hooks::First(e, type), code, wp, message);
}
bool Application::FilterMessage(User::Message &message, int code)
{
    return (MessageHook(-1, code, 0, message) & 0xffff) != 0;
}
unsigned Application::MessageHook(int type, int code, unsigned wp, User::Message &message)
{
    HookEnvironment e(*this);
    if (!Win98Hooks::First(e, type) && (type != -1 || !Win98Hooks::First(e, 6)))
        return 0;
    const unsigned scratch = Allocate(sizeof message);
    if (!scratch)
    {
        Fail("Hook message buffer allocation failed");
        return 0;
    }
    Store(scratch, &message, sizeof message);
    const unsigned result =
        type == -1 ? Win98Hooks::Filter(e, scratch, code) : MessageHook(type, code, wp, scratch);
    if (state != Failed)
        Read(scratch, &message, sizeof message);
    Free(scratch);
    return result;
}
// Brings the global chain of this process in step with the hooks of the
// session: a mirror entry for each hook whose module this process can run,
// with the module loaded here and the procedure at its place in it; gone
// hooks lose their entry.
void Application::SyncSessionHooks()
{
    // A thread that has ended runs no more guest code. Its cleanup still
    // walks the chains (RemoveThreadHooks), but a hook DLL loaded there could
    // not run its DLL_PROCESS_ATTACH (Call returns 0 on a finished thread),
    // and the hook would stay refused for the whole process: MSIDLE.DLL of
    // IE 5.01 in Explorer. The next live thread that uses a chain
    // catches up.
    if (session_hooks_busy || threads[current_thread].done) return;
    session_hooks_busy = true;
    session_hooks_seen = session_hook_generation;
    HookEnvironment e(*this);
    for (auto &mirror : session_hook_mirrors)
    {
        if (!mirror.session) continue;
        bool alive = false;
        for (const auto &hook : session_hooks) alive = alive || hook.id == mirror.session;
        if (alive) continue;
        Win98Hooks::Remove(e, mirror.local);
        mirror = {};
    }
    // A process without a Win32 processor (the DOS machine) loads no Win32
    // DLL. The process of a 16-bit program does: USER looks for the
    // hook's module in the current process whatever its kind and, when it is
    // not there, loads it by name with KERNEL.452 and calls the
    // procedure at its place in the new image. Here the module is loaded on
    // the flat stack of the thread, as LoadLibraryEx32W does it.
    const bool able = HasProcessor() && state != Failed && state != Exited;
    for (unsigned i = 0; able && i < 16; ++i)
    {
        // By value: loading a module runs guest code, and the table may change.
        const SessionHook hook = session_hooks[i];
        if (!hook.id || hook.arm64 != Arm64Process()) continue;
        bool mirrored = false;
        for (const auto &mirror : session_hook_mirrors) mirrored = mirrored || mirror.session == hook.id;
        if (mirrored) continue;
        const unsigned saved_error = last_error;
        CpuContext *flat = nullptr;
        unsigned flat_top = 0;
        if (!Win16FlatEnter(flat, flat_top)) break;
        const unsigned index = LoadModule(hook.module, Missing);
        const bool loaded = index != Missing && InitializeModule(index);
        Win16FlatLeave(flat, flat_top);
        if (state == Failed || state == Exited) break;
        if (!loaded)
        {
            // Windows goes on without the hook in a process the DLL does not load into.
            if (log)
            {
                char text[400];
                snprintf(text, sizeof text, "Session hook: %s does not load into this process (error %u)", hook.module, last_error);
                log(text);
            }
            last_error = saved_error;
            // Remembered as tried: an entry without a local hook.
            for (auto &mirror : session_hook_mirrors)
                if (!mirror.session) { mirror = {hook.id, 0}; break; }
            continue;
        }
        last_error = saved_error;
        // The hook holds the module for as long as the process lives.
        ++modules[index].references;
        if (hook_serial >= 65535) continue;
        for (auto &slot : hooks)
            if (!slot.handle)
            {
                slot.handle = 0x4b480000 | ++hook_serial;
                slot.record = {};
                slot.record.type = hook.type;
                slot.record.task = 0;
                slot.record.procedure = Arm64::CallbackEntry(*cpu, modules[index].handle + hook.offset, hook.kind);
                Win98Hooks::Link(e, slot.handle);
                for (auto &mirror : session_hook_mirrors)
                    if (!mirror.session) { mirror = {hook.id, slot.handle}; break; }
                break;
            }
    }
    session_hooks_busy = false;
}
// The process ends: the hooks it set for the session end with it.
void Application::RemoveSessionHooks()
{
    for (auto &hook : session_hooks)
        if (hook.owner == this)
        {
            hook = {};
            ++session_hook_generation;
        }
}
void Application::RemoveThreadHooks(unsigned thread)
{
    HookEnvironment e(*this);
    for (auto &slot : hooks)
        if (slot.handle && slot.record.task == thread)
            Win98Hooks::Remove(e, slot.handle);
}
// WH_KEYBOARD and WH_MOUSE run while GetMessage or PeekMessage hands an
// input message to the thread: HC_ACTION for a removed message, HC_NOREMOVE
// otherwise. A non-zero answer discards the message. WH_KEYBOARD carries the
// virtual key and the key data, WH_MOUSE the message number and a
// MOUSEHOOKSTRUCT (point, window, hit-test code, extra information). The
// emulation layer of the original DINPUT reads the keyboard this way when
// DINPUT.VXD is absent.
bool Application::InputHook(const User::Message &message, bool removed)
{
    HookEnvironment e(*this);
    const bool key = message.message >= 0x100 && message.message <= 0x108;
    const bool mouse = (message.message >= 0x200 && message.message <= 0x209) ||
                       (message.message >= 0xa0 && message.message <= 0xa9);
    if (key && Win98Hooks::First(e, 2))
        return MessageHook(2, removed ? 0 : 3, message.wparam, message.lparam) != 0;
    if (!mouse || !Win98Hooks::First(e, 7)) return false;
    const unsigned scratch = Allocate(20);
    if (!scratch) { Fail("Mouse hook buffer allocation failed"); return false; }
    const unsigned hit = mouse && message.message < 0x200 ? message.wparam : 1; // HTCLIENT
    const unsigned record[5] = {(unsigned)message.x, (unsigned)message.y, message.hwnd, hit, 0};
    Store(scratch, record, sizeof record);
    const unsigned result = MessageHook(7, removed ? 0 : 3, message.message, scratch);
    Free(scratch);
    return result != 0;
}
// WH_CALLWNDPROC runs before a sent message reaches its window procedure:
// wParam says whether the current thread sent it, lParam points to a
// CWPSTRUCT (lParam, wParam, message, hwnd). The answer is ignored.
void Application::CallWndProcHook(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    HookEnvironment e(*this);
    if (!Win98Hooks::First(e, 4)) return;
    const unsigned scratch = Allocate(16);
    if (!scratch) { Fail("Window procedure hook buffer allocation failed"); return; }
    const unsigned record[4] = {lp, wp, message, hwnd};
    Store(scratch, record, sizeof record);
    MessageHook(4, 0, 1, scratch);
    Free(scratch);
}
unsigned Application::HookApi(ApiName name, const unsigned *a)
{
    HookEnvironment e(*this);
    auto signed_word = [](unsigned value) { return int(value & 0x7fff) - int(value & 0x8000); };
    switch (name.id)
    {
    API_CASE("CallMsgFilterA") API_CASE("CallMsgFilter")
    {
        User::Message message = {};
        if (!Read(a[0], &message, sizeof message))
            return 0;
        // USER narrows nCode; 2EC9 sign-extends the returned AX.
        return signed_word(Win98Hooks::Filter(e, a[0], signed_word(a[1])));
    }
    API_CASE("CallNextHookEx")
        // USER and USER32: carry signed code/unsigned wp
        // through WORDs. This API restores a full 32-bit LRESULT afterward.
        return Win98Hooks::CallNext(e, a[0], signed_word(a[1]), e.Valid(a[0]) && e.Read(a[0]).type==5 ? a[2] : a[2] & 0xffff, a[3]);
    API_CASE("UnhookWindowsHookEx")
    {
        // The handle of a hook for the session is the one of its entry in
        // the process that set it; with it the hook leaves every process.
        for (auto &mirror : session_hook_mirrors)
            if (mirror.session && mirror.local == a[0])
                for (auto &hook : session_hooks)
                    if (hook.id == mirror.session && hook.owner == this)
                    {
                        hook = {};
                        ++session_hook_generation;
                        mirror = {};
                    }
        return Win98Hooks::Remove(e, a[0]);
    }
    API_CASE("SetWindowsHookExA")
    {
        const int type = static_cast<int>(a[0]);
        if (type < -1 || type > 12)
        {
            last_error = 1426;
            return 0;
        }
        // Win98 rejects a null procedure without changing LastError.
        if (!a[1])
            return 0;
        // Thread message/filter, keyboard, mouse, window-procedure and CBT
        // window lifecycle hooks have native producers. Global DLL injection,
        // journal and CBT input/interactive events remain unavailable.
        if (type != -1 && type != 2 && type != 3 && type != 4 && type != 5 && type != 7)
        {
            last_error = 120;
            return 0;
        }
        // Win9x also accepts the executable's procedure for a global hook.
        // Its global chain is local to this loaded image; DLL hooks below
        // retain the session-wide module-loading route.
        const bool executable_global=!a[3]&&a[2]==image.Base();
        if (!a[3]&&!executable_global)
        {
            // A hook for all threads of the session. Its procedure lives in
            // a DLL (ERROR_HOOK_NEEDS_HMOD), which every process of the
            // module's kind loads when an event of the type happens there.
            unsigned index = Missing;
            for (unsigned i = 1; i < module_watermark; ++i)
                if (modules[i].kind == 3 && modules[i].handle == a[2] && a[2]) index = i;
            int kind = -1;
            const unsigned procedure = Arm64::CallbackTarget(*cpu, a[1], kind);
            if (index == Missing || !modules[index].pe || procedure < a[2] ||
                procedure - a[2] >= modules[index].pe->SizeOfImage())
            {
                last_error = 1428;
                return 0;
            }
            SessionHook *free_hook = nullptr;
            for (auto &hook : session_hooks)
                if (!hook.id && !free_hook) free_hook = &hook;
            if (!free_hook)
            {
                last_error = 8;
                return 0;
            }
            free_hook->id = ++session_hook_serial;
            free_hook->type = type;
            free_hook->owner = this;
            free_hook->arm64 = Arm64Process();
            free_hook->offset = procedure - a[2];
            free_hook->kind = kind;
            snprintf(free_hook->module, sizeof free_hook->module, "%s", modules[index].path);
            const unsigned id = free_hook->id;
            ++session_hook_generation;
            SyncSessionHooks();
            for (const auto &mirror : session_hook_mirrors)
                if (mirror.session == id && mirror.local) return mirror.local;
            // No room for the entry in this process: the hook is not set.
            for (auto &hook : session_hooks)
                if (hook.id == id) { hook = {}; ++session_hook_generation; }
            last_error = 8;
            return 0;
        }
        const unsigned task = e.TaskIndex(a[3]);
        // A 16-bit hook procedure arrives as the mark of its far pointer
        // (Win16Procedure, src/win16_user.cpp), not as an executable flat
        // address; Application::Call enters it through Win16HookCallback.
        if ((!executable_global&&(!task||threads[task-1].done)) ||
            (!virtual_memory.Executable(a[1]) && !Win16IsMark(a[1])))
        {
            last_error = 87;
            return 0;
        }
        if (hook_serial < 65535)
            for (auto &slot : hooks)
                if (!slot.handle)
                {
                    if (a[3]&&!e.AddOwner(a[3]))
                        break;
                    slot.handle = 0x4b480000 | ++hook_serial;
                    slot.record = {};
                    slot.record.type = type;
                    slot.record.task = a[3];
                    slot.record.procedure = a[1];
                    Win98Hooks::Link(e, slot.handle);
                    return slot.handle;
                }
        last_error = 8;
        return 0;
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}
} // namespace X86
