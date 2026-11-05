// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_shell_window.h"
#include "x86_app.h"
#include <string.h>
namespace X86
{
unsigned Application::ShellWindow()
{
    struct Environment {
        unsigned record;
        void Lock() {} // No yield in the cooperative USER getter.
        void Unlock() {}
        unsigned ShellWindowRecord() { return record; }
        unsigned WindowHandle(unsigned window) { return window; }
    } e{User::RegisteredShellWindow()};
    // USER32 expands a WORD HWND; native USER retains its generation bits.
    return Win98ShellWindow::Get(e) ? e.record : 0;
}
unsigned Application::SetShellWindow(unsigned hwnd)
{
    if (hwnd && !User::IsWindow(hwnd)) { last_error=1400; return 0; }
    return User::RegisterShellWindow(hwnd,this,
        [](void *p,unsigned h) {auto&a=*static_cast<Application*>(p);unsigned v[]={h,1,0,0,0,0,0x13};a.PositionApi("SetWindowPos",v);},
        [](void *p,unsigned h,unsigned m,unsigned w,unsigned l) {static_cast<Application*>(p)->CallWindow(h,m,w,l);});
}
struct Application::ShellEnvironment final : Win98Shell::Environment
{
    Application &app;
    explicit ShellEnvironment(Application &a) : app(a) {}
    unsigned RegisterMessage(unsigned kind) override
    {
        const char *names[] = {"SHELLHOOK", "OTHERWINDOWCREATED", "OTHERWINDOWDESTROYED",
                               "ACTIVATESHELLWINDOW"};
        return app.RegisterMessage(names[kind]);
    }
    unsigned Install() override
    {
        // This token owns the native USER event subscription, not an x86 hook.
        app.shell_subscription = 0x98000000 | (++app.shell_serial & 0xffffff);
        return app.shell_subscription;
    }
    void Unhook(unsigned token) override
    {
        if (app.shell_subscription == token)
            app.shell_subscription = 0;
    }
    bool IsWindow(unsigned hwnd) override { return User::IsWindow(hwnd); }
    bool Post(unsigned h, unsigned m, unsigned w, unsigned l) override
    {
        return User::PostMessage(h, m, w, l);
    }
    unsigned Send(unsigned h, unsigned m, unsigned w, unsigned l) override
    {
        // An event raised on another process's fiber cannot enter this
        // process's window procedure synchronously; it is posted, as USER's
        // shell hook messages are.
        if (User::CurrentProcess() && User::CurrentProcess() != app.process_id) return User::PostMessage(h, m, w, l);
        return app.CallWindow(h, m, w, l);
    }
    unsigned Next(unsigned, unsigned, unsigned, unsigned) override { return 0; }
    unsigned AllocateInfo() override
    {
        const unsigned p = app.Allocate(12);
        const unsigned zero[3] = {};
        if (p)
            app.Store(p, zero, sizeof zero);
        return p;
    }
    unsigned PrepareInfo(unsigned allocation, unsigned hwnd) override
    {
        app.Write32(allocation, hwnd);
        return allocation;
    }
    void CopyInfo(unsigned destination, unsigned allocation) override
    {
        unsigned short rect[4] = {};
        if (app.Read(allocation + 4, rect, sizeof rect))
            app.Store(destination, rect, sizeof rect);
    }
    void FreeInfo(unsigned allocation) override { app.Free(allocation); }
};
unsigned Application::RegisterShell(unsigned hwnd, unsigned flags)
{
    ShellEnvironment e(*this);
    // SHELL32 ordinal 181 ends in CWDE after the original WORD return.
    // Retain our complete generation HWND instead of truncating it to WORD.
    const unsigned result = unsigned(Win98Shell::Signed(Win98Shell::Register(shell_state, e, hwnd, flags)));
    if (shell_subscription) User::SetShellHookOwner(this);
    return result;
}
void Application::ShellEvent(unsigned code, unsigned hwnd, unsigned parameter)
{
    if (!shell_subscription || shell_subscription != shell_state.hook)
        return;
    ShellEnvironment e(*this);
    Win98Shell::Hook(shell_state, e, code, hwnd, parameter);
}
#include "x86_shell_dragdrop.inc"
} // namespace X86
