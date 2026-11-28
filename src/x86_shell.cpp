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
void Application::LogonAtStart()
{
    char shell[260] = {};
    ProfileString("boot", "shell", "", shell, sizeof shell, "SYSTEM.INI");
    if (char *space = strchr(shell, ' ')) *space = 0;
    if (!shell[0] || !SameText(PathLeaf(shell), PathLeaf(program_name))) return;
    // Windows asks for a logon at its start once a network client is
    // installed (a provider under NetworkProvider\Order: Microsoft Family
    // Logon, which Setup of Windows 98 installs, or Client for Microsoft
    // Networks, which Dial-Up Networking brings). Without one MPRSERV would
    // ask for a user name the setup of such a drive was never asked for. A
    // drive with Client for NetWare Networks has no NetWare driver here: its
    // provider would stop the logon at "The NetWare-compatible shell is not
    // available" before the shell starts.
    auto &registry = Registry::System();
    unsigned key = Registry::Missing;
    bool created = false;
    const Registry::Value *value = nullptr;
    if (registry.Open(registry.roots[2], "System\\CurrentControlSet\\Control\\NetworkProvider\\Order", false, key,
                      created) || registry.EnumValue(key, 0, value))
        return;
    if (!registry.Query(key, "NWNP32", value))
    {
        if (log) log("LOGON none at the start of the shell: the NetWare client of this drive has no driver");
        return;
    }
    logon_at_start = true;
}
// A system DLL the logon at the start loads into the shell, where it stays:
// its index, or ModuleCapacity.
unsigned Application::StartLogonModule(const char *dll)
{
    if (state == Failed || state == Exited || !LockLoader()) return ModuleCapacity;
    const unsigned index = LoadModule(dll);
    bool ready = false;
    if (index < ModuleCapacity)
    {
        ++modules[index].references;
        ready = InitializeModule(index);
        if (!ready) --modules[index].references;
    }
    UnlockLoader();
    return ready && state != Failed && state != Exited ? index : ModuleCapacity;
}
// MPRSERV logs on without a dialog when the password list of the user opens
// with an empty password. Windows Setup leaves the user none when its
// primary network logon is Microsoft Family Logon, and the first start
// under Windows Logon asks "Enter Windows Password"; an empty password
// there makes the list (MSPWL32's CreatePasswordCache), and the dialog does
// not come back. The user of Network\Logon without a list gets that list
// here, as if that first start had been answered so.
void Application::StartPasswordList()
{
    auto &registry = Registry::System();
    unsigned key = Registry::Missing;
    bool created = false;
    const Registry::Value *value = nullptr;
    char user[64] = {};
    if (registry.Open(registry.roots[2], "Network\\Logon", false, key, created) ||
        registry.Query(key, "username", value) || !value || value->size < 2 || value->size > sizeof user)
        return;
    memcpy(user, value->data, value->size);
    user[sizeof user - 1] = 0;
    if (!user[0]) return;
    const unsigned index = StartLogonModule("MSPWL32.DLL");
    if (index >= ModuleCapacity) return;
    const unsigned name = ModuleProcedure(index, "GetCacheFileName", 0),
                   create = ModuleProcedure(index, "CreatePasswordCache", 0),
                   close = ModuleProcedure(index, "ClosePasswordCache", 0);
    // The handle, the empty password, the user and the file of the list.
    const unsigned memory = name && create && close ? Allocate(8 + sizeof user + 260) : 0;
    if (!memory) return;
    const unsigned zero[2] = {};
    Store(memory, zero, sizeof zero);
    Store(memory + 8, user, sizeof user);
    const unsigned file = memory + 8 + sizeof user;
    // GetCacheFileName finds the list by [Password Lists] of SYSTEM.INI.
    const unsigned query[] = {memory + 8, file, 260};
    const unsigned attributes[] = {file};
    const bool listed = !Call(name, query, 3) && state != Failed && state != Exited;
    if (listed && Api("GetFileAttributesA", attributes) != ~0u)
    {
        Free(memory);
        return;
    }
    unsigned result = ~0u;
    if (state != Failed && state != Exited)
    {
        const unsigned arguments[] = {memory, memory + 8, memory + 4};
        result = Call(create, arguments, 3);
        if (!result && Read32(memory))
        {
            const unsigned closing[] = {Read32(memory), 1};
            Call(close, closing, 2);
        }
    }
    if (log)
    {
        char text[200];
        snprintf(text, sizeof text, "LOGON password list of %s with an empty password: %s, result %u", user,
                 result ? "not made" : "made", result);
        log(text);
    }
    Free(memory);
}
// MSGSRV32 calls USER's WNetLogon with no provider, whose thunk asks MPR to
// log on to every provider: MPR.DLL starts MPREXE.EXE, whose MPRSERV logs on
// through the primary network logon (Network\Logon, PrimaryProvider) and the
// password list of the user in Network\Logon\username. With the user of
// setup, whose list has an empty password, nothing is shown. MPR's own
// WNetLogonA(NULL, NULL) is the same request (1Ah) from a 32-bit process; it
// returns when MPREXE answered, so the shell starts after the logon as on
// Windows.
void Application::RunStartLogon()
{
    logon_at_start = false;
    StartPasswordList();
    const unsigned index = StartLogonModule("MPR.DLL");
    const unsigned logon = index < ModuleCapacity ? ModuleProcedure(index, "WNetLogonA", 0) : 0;
    unsigned result = ~0u;
    if (logon && state != Failed && state != Exited)
    {
        const unsigned arguments[] = {0, 0};
        result = Call(logon, arguments, 2);
    }
    if (log)
    {
        char text[160];
        snprintf(text, sizeof text, "LOGON WNetLogonA at the start of the shell: %s, result %u",
                 logon ? "called" : "MPR.DLL has none", result);
        log(text);
    }
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
// HSHELL_GETMINRECT, which USER's DefWindowProc asks for 0313h with lParam
// -1 (and nothing else here). SHELL's hook does what it does for every
// code 5 (Win98Shell::Send): a SHELLHOOKINFO of 12 bytes, the window and
// then the rectangle as four words, sent with its registered message to
// each of its windows, and the rectangle copied back. Explorer's taskbar
// fills it with the window's button in screen coordinates, cut to what
// shows of its band, and leaves it as it was for a window without one.
// The taskbar answers on its own thread in Explorer, and USER's caller
// waits for it: a send between the processes, with the info in the memory
// of the shell's process. False when no shell hook is registered.
bool Application::ShellMinRect(unsigned hwnd, Gdi::RECT &rect)
{
    rect = {};
    auto *shell = static_cast<Application *>(User::ShellHookOwner());
    if (!shell || !shell->shell_subscription || shell->shell_subscription != shell->shell_state.hook)
        return false;
    // The list as it stands now: the shell may change it, or end, while it
    // is asked.
    unsigned windows[sizeof shell->shell_state.windows / sizeof *shell->shell_state.windows];
    const unsigned count = shell->shell_state.count, message = shell->shell_state.message;
    for (unsigned i = 0; i < count; ++i)
        windows[i] = shell->shell_state.windows[i];
    const unsigned info = shell->Allocate(12);
    if (!info)
        return true;
    const unsigned zero[3] = {hwnd, 0, 0};
    shell->Store(info, zero, sizeof zero);
    for (unsigned i = 0; i < count; ++i)
    {
        if (!User::IsWindow(windows[i]))
            continue;
        if (shell == this)
            CallWindow(windows[i], message, 5, info);
        else
            SendCrossProcessRaw(shell, windows[i], message, 5, info);
        if (User::ShellHookOwner() != shell || state == Failed || state == Exited)
            return false;
    }
    short words[4] = {};
    shell->Read(info + 4, words, sizeof words);
    shell->Free(info);
    rect = {words[0], words[1], words[2], words[3]};
    return true;
}
#include "x86_shell_dragdrop.inc"
} // namespace X86
