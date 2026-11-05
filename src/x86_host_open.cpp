// SPDX-License-Identifier: GPL-2.0-or-later
// Open a host-selected program or document through Windows 98 START.EXE,
// which invokes ShellExecuteEx. Finder opens, drops and the Open Program menu
// therefore support programs, shortcuts, batch files, PIFs, DOS applications
// and documents through the guest shell. RequestOpen queues the filename; the
// first session process launches it on its own thread at the next message
// request.
#include "x86_app.h"
#include "user.h"
#include <stdio.h>
#include <string.h>
#if defined(__APPLE__) || defined(__linux__)
#include <strings.h>
#include <string>
#include <vector>
#endif

namespace X86
{
namespace
{
#if defined(__APPLE__) || defined(__linux__)
std::vector<std::string> open_requests;
struct SettingsRequest { std::string path; unsigned owner, process; };
std::vector<SettingsRequest> settings_requests;
#endif
void *dos_failure_context = nullptr; // of the host (src/state_rules/state.rules)
void (*dos_failure_hook)(void *, const char *, const char *) = nullptr;
}

#if defined(__APPLE__) || defined(__linux__)
void Application::RequestOpen(const char *guest_path)
{
    if (!guest_path || !*guest_path)
        return;
    open_requests.push_back(guest_path);
    // The thread that will start it may rest in GetMessage: WM_NULL wakes it
    // and means nothing to its window.
    unsigned window = User::FindWindow("Shell_TrayWnd", nullptr);
    if (!window)
        window = User::GetForegroundWindow();
    if (window && User::IsWindow(window))
    {
        User::NoteHostPress(window);
        User::PostMessage(window, 0, 0, 0);
    }
}
#else
// The Pi kernel has no host desktop sending files into its session.
void Application::RequestOpen(const char *) {}
#endif

void Application::SetDosFailureHook(void *context, DosFailureHook hook)
{
    dos_failure_context = context;
    dos_failure_hook = hook;
}
void Application::ReportDosFailure(const char *path, const char *error)
{
    if (dos_failure_hook) dos_failure_hook(dos_failure_context, path, error);
}
#if defined(__APPLE__) || defined(__linux__)
void Application::RequestDosSettings(const char *path, unsigned owner)
{
    if (!path || !*path) return;
    // A live DOS window opens the sheet on its own fiber. This also works
    // when a DOS game is the session's first process, without Explorer.
    const char *active = owner ? DosSettingsPath(owner) : nullptr;
    if (active && !strcasecmp(active, path))
    {
        User::PostMessage(owner, 0x8000 + 0x98, 0, 0);
        return;
    }
    settings_requests.push_back({path, owner, process_id});
    // START.EXE may have exited and orphaned a DOS process. Its null parent
    // does not make it the host's session process; wake this specific process.
    unsigned windows[User::WindowCapacity], count = 0;
    if (User::WindowSnapshot(0, windows, User::WindowCapacity, count))
        for (unsigned i = 0; i < count; ++i)
            if (User::WindowProcess(windows[i]) == process_id)
            {
                User::PostMessage(windows[i], 0, 0, 0);
                break;
            }
}

unsigned Application::OpenRequests() { return unsigned(open_requests.size()); }

bool Application::HasDosSettingsRequests() const
{
    if (parent_process || state == Failed || state == Exited) return false;
    // A modal settings sheet asks for messages too. Pending host input is
    // served after its current recorded operation, not inside that sheet.
    for (unsigned i = 0; i < threads[current_thread].host_frame_count; ++i)
        if (threads[current_thread].host_frames[i].kind == HostSettingsApi) return false;
    for (const auto &request : settings_requests)
        if (request.process == process_id) return true;
    return false;
}
void Application::ServeOpenRequests(bool recorded)
{
    if (parent_process || (open_requests.empty() && settings_requests.empty()) || state == Failed || state == Exited)
        return;
    if (!recorded)
        for (unsigned i = 0; i < threads[current_thread].host_frame_count; ++i)
            if (threads[current_thread].host_frames[i].kind == HostSettingsApi) return;
    // Take one request at a time: no vector of later requests lives on the
    // host stack while the current sheet is open.
    for (unsigned i = 0; i < settings_requests.size();)
    {
        if (settings_requests[i].process != process_id) { ++i; continue; }
        const auto request = settings_requests[i];
        settings_requests.erase(settings_requests.begin() + i);
        const unsigned owner = User::IsWindow(request.owner) ? request.owner : 0;
        if (recorded) GiveTail(TailFresh);
        const unsigned result = PifPropertySheet(request.path.c_str(), owner, 0, request.path.c_str());
        if (log)
        {
            char text[400];
            snprintf(text, sizeof text, "HOST DOS SETTINGS %.300s result=%u", request.path.c_str(), result);
            log(text);
        }
        if (state == Failed || state == Exited) return;
    }
    std::vector<std::string> requests;
    requests.swap(open_requests);
    for (const auto &path : requests)
    {
        const unsigned windows = Allocate(260);
        if (!windows)
            break;
        const unsigned directory_call[] = {windows, 260};
        Api("GetWindowsDirectoryA", directory_call);
        std::string folder = path;
        const auto slash = folder.rfind('\\');
        if (slash != std::string::npos)
            folder.resize(slash < 3 ? 3 : slash);
        const std::string line = std::string(ReadString(windows)) + "\\COMMAND\\START.EXE \"" + path + "\"";
        Free(windows);
        const unsigned command = String(line.c_str()), directory = String(folder.c_str()), startup = Allocate(0x44),
                       process = Allocate(16);
        unsigned started = 0;
        if (command && directory && startup && process)
        {
            unsigned char info[0x44] = {};
            info[0] = 0x44;
            Store(startup, info, sizeof info);
            const unsigned create[] = {0, command, 0, 0, 0, 0, 0, directory, startup, process};
            started = Api("CreateProcessA", create);
            if (started)
            {
                const unsigned first[] = {Read32(process)}, second[] = {Read32(process + 4)};
                Api("CloseHandle", first);
                Api("CloseHandle", second);
            }
        }
        if (log)
        {
            char text[420];
            snprintf(text, sizeof text, "HOST OPEN %.300s %s error=%u", path.c_str(), started ? "started" : "failed",
                     started ? 0u : last_error);
            log(text);
        }
        Free(command);
        Free(directory);
        Free(startup);
        Free(process);
        if (state == Failed || state == Exited)
            return;
    }
}
#else
void Application::RequestDosSettings(const char *, unsigned) {}
unsigned Application::OpenRequests() { return 0; }
bool Application::HasDosSettingsRequests() const { return false; }
void Application::ServeOpenRequests(bool) {}
#endif
} // namespace X86
