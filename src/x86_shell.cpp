// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_shell_window.h"
#include "x86_app.h"
#include <stdio.h>
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
    const unsigned result = User::RegisterShellWindow(hwnd,this,
        [](void *p,unsigned h) {auto&a=*static_cast<Application*>(p);unsigned v[]={h,1,0,0,0,0,0x13};a.PositionApi("SetWindowPos",v);},
        [](void *p,unsigned h,unsigned m,unsigned w,unsigned l) {static_cast<Application*>(p)->CallWindow(h,m,w,l);});
    if (result && hwnd)
        RunPendingShellCommand();
    return result;
}
namespace
{
// The command line of an Explorer that started as the shell without it
// (Application::ShellCommandLine), until the shell window is there.
char pending_shell_command[1024];
const char *PathLeaf(const char *path)
{
    const char *leaf = path;
    for (const char *p = path; *p; ++p)
        if (*p == '\\' || *p == '/' || *p == ':') leaf = p + 1;
    return leaf;
}
bool SameText(const char *a, const char *b)
{
    for (;; ++a, ++b)
    {
        const unsigned x = (unsigned char)*a, y = (unsigned char)*b;
        if ((x >= 'a' && x <= 'z' ? x - 32 : x) != (y >= 'a' && y <= 'z' ? y - 32 : y)) return false;
        if (!x) return true;
    }
}
}
// Explorer decides at its start whether it becomes the shell: when no shell
// window is there and SYSTEM.INI names it in shell= of [boot]. The shell
// takes its command line for a file to open, while an Explorer beside the
// shell opens the folder its switches name. Under Windows 98 the shell
// always runs first; a session that starts with another program has none,
// and an Explorer started from it with switches became the shell and said
// "Cannot find the file '/n,/e,C:\'", or for the Control Panel of
// CONTROL.EXE (an Explorer with /idlist) "Cannot find the file
// '/idlist,:...'". Such an Explorer starts as the shell without its
// arguments, and once it has registered its shell window it runs its own
// command line, which then finds the shell.
const char *Application::ShellCommandLine(const char *program, const char *command)
{
    // A shell that is still starting has its desktop window, the Progman
    // class, before it registers; Explorer itself takes that for a shell.
    if (!program || !command || pending_shell_command[0] || User::RegisteredShellWindow() ||
        User::FindWindow("Progman", nullptr))
        return command;
    const char *arguments = command;
    while (*arguments == ' ' || *arguments == '\t') ++arguments;
    if (*arguments == '"')
    {
        ++arguments;
        while (*arguments && *arguments != '"') ++arguments;
        if (*arguments) ++arguments;
    }
    else
        while (*arguments && *arguments != ' ' && *arguments != '\t') ++arguments;
    while (*arguments == ' ' || *arguments == '\t') ++arguments;
    if (!*arguments || strlen(command) >= sizeof pending_shell_command)
        return command;
    char shell[260] = {};
    ProfileString("boot", "shell", "", shell, sizeof shell, "SYSTEM.INI");
    if (char *space = strchr(shell, ' ')) *space = 0;
    if (!shell[0] || !SameText(PathLeaf(shell), PathLeaf(program)))
        return command;
    strcpy(pending_shell_command, command);
    if (log)
    {
        char text[1200];
        snprintf(text, sizeof text, "SHELL %s starts as the shell; once it is there it runs %s", program, command);
        log(text);
    }
    return nullptr;
}
void Application::RunPendingShellCommand()
{
    if (!pending_shell_command[0])
        return;
    char command[sizeof pending_shell_command];
    strcpy(command, pending_shell_command);
    pending_shell_command[0] = 0;
    // CreateProcessA(0, command, 0, 0, FALSE, 0, 0, 0, startup, information)
    // in the shell's own memory: STARTUPINFOA, PROCESS_INFORMATION, the line.
    const unsigned length = unsigned(strlen(command)) + 1, memory = Allocate(68 + 16 + length);
    if (!memory)
        return;
    unsigned startup[17] = {68};
    Store(memory, startup, sizeof startup);
    Store(memory + 84, command, length);
    const unsigned a[] = {0, memory + 84, 0, 0, 0, 0, 0, 0, memory, memory + 68};
    const unsigned started = Api("CreateProcessA", a);
    if (log)
    {
        char text[1200];
        snprintf(text, sizeof text, "SHELL runs %s: %s", command, started ? "started" : "failed");
        log(text);
    }
    if (started)
        for (unsigned i = 0; i < 2; ++i)
        {
            const unsigned close[] = {Read32(memory + 68 + i * 4)};
            Api("CloseHandle", close);
        }
    Free(memory);
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
