// SPDX-License-Identifier: GPL-2.0-or-later
// The session exports of USER32: ExitWindowsEx and the small registration
// entries next to it. Every entry names the original it follows, a USER.EXE
// segment:offset, because USER32 only thunks them down to the 16-bit side.
#include "x86_app.h"
#include "win98_broadcast.h"
#include "win98_display_reposition.h"
#include "user_settings.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/image-resource/system_assets.h"
#endif
#include "registry.h"
#include <stdio.h>
#include <string.h>
namespace X86 {
namespace {
// ExitWindowsEx flags. USER tests bit 1 for EWX_REBOOT and
// bit 0 for EWX_SHUTDOWN; the remaining bits never reach a branch.
constexpr unsigned ExitShutdown = 1, ExitReboot = 2;
// Windows 98 sets bit 31 of the flags the broadcast carries as lParam when
// neither EWX_SHUTDOWN nor EWX_REBOOT is set, so the receivers can tell a
// logoff from an end of session.
constexpr unsigned LogoffBit = 0x80000000u;
// hands 43h to ExitKernel for a reboot and zero otherwise.
constexpr unsigned RebootCode = 0x43, ShutdownCode = 0;
// USER data word 0B8h. Windows 98 answers zero while an end of session runs, so
// a second call from a WM_QUERYENDSESSION handler cannot start another one.
bool session_ending = false;
// EnumDisplaySettings sentinels and the size stamps into dmSize.
constexpr unsigned CurrentSettings = 0xffffffffu, RegistrySettings = 0xfffffffeu;
constexpr unsigned DevModeSize = 0x94;
// The mode the display comes up in when HKCC\Display\Settings names none
// this driver has: the 800 by 600 screen of the host at 32 bits.
constexpr unsigned NativeWidth = 800, NativeHeight = 600, NativeDepth = 32;
// The answers of USER and its neighbours.
constexpr unsigned ChangeSuccessful = 0, ChangeFailed = unsigned(-1),
                   ChangeBadMode = unsigned(-2), ChangeNotUpdated = unsigned(-3),
                   ChangeBadFlags = unsigned(-4), ChangeBadParam = unsigned(-5);
// Windows 98 reads the flags. CDS_UPDATEREGISTRY 1, CDS_TEST 2, CDS_FULLSCREEN 4,
// CDS_GLOBAL 8, CDS_SET_PRIMARY 0x10, CDS_NORESET 0x10000000 (bit 1000h of
// the high word ), CDS_RESET 0x40000000.
constexpr unsigned ChangeUpdateRegistry = 1, ChangeTest = 2, ChangeFullScreen = 4,
                   ChangeNoReset = 0x10000000;
// HKEY_CURRENT_CONFIG, the key and open below it and the two
// values of the mode.
constexpr unsigned CurrentConfigRoot = 5;
constexpr const char *DisplaySettingsKey = "Display\\Settings";
// The dmFields bits of the four geometry members: dmBitsPerPel 0x40000,
// dmPelsWidth 0x80000, dmPelsHeight 0x100000, dmDisplayFrequency 0x400000.
constexpr unsigned FieldDepth = 0x40000, FieldWidth = 0x80000, FieldHeight = 0x100000;
// WM_DISPLAYCHANGE. USER sends it to every top-level window after the
// driver reports the new mode, with the depth in wParam and the two extents
// packed into lParam.
constexpr unsigned DisplayChange = 0x7e;
// EndTask flags. Windows 98 reads bit 2 for "no user interface", bit 1
// for "ask the window to close first" and bit 3 for "do not wait".
constexpr unsigned TaskNoUserInterface = 4, TaskCloseFirst = 2, TaskNoWait = 8;
// waits at most 20000 ms, sleeps 100 ms the first time and
// 200 ms after that.
constexpr unsigned TaskWaitLimit = 20000, TaskFirstWait = 100, TaskWait = 200;
// The answers gives. 3 is the default the dialog also returns for its
// End Task button, 2 comes from a failed termination and from a call without a
// window and without a task, 4 is the Wait button and 5 the task that is
// already ending.
constexpr unsigned TaskFailed = 2, TaskEnded = 3, TaskAlreadyEnding = 5;
// The confirmation dialog Windows 98 loads: RT_DIALOG 11 of the original
// USER.EXE, "Program Cannot Respond".
constexpr unsigned TaskDialogResource = 11, ResourceDialog = 5;
} // namespace

// The dialogs of USER.EXE are 16-bit templates, and the native reader in
// src/dialog_template.cpp knows only the 32-bit forms. The two differ in
// field widths and in the way a class and a caption are written, not in what
// they describe, so the template is rewritten once on the way in.
// The 16-bit header is a DWORD style, a BYTE item count, four WORD corners,
// the menu, the class and the caption as zero-terminated ANSI strings and,
// with DS_SETFONT, a point size and a typeface. An item is four WORD corners,
// a WORD id, a DWORD style, one class byte 80h to 85h or a class string, the
// text and a BYTE of creation data.
bool Application::ConvertDialogTemplate(const unsigned char *source, unsigned size,
                                        unsigned char *out, unsigned capacity, unsigned &length)
{
    unsigned at = 0, put = 0;
    auto byte = [&](unsigned &value) { if (at >= size) return false; value = source[at++]; return true; };
    auto word = [&](unsigned &value) {
        unsigned lo = 0, hi = 0;
        if (!byte(lo) || !byte(hi)) return false;
        value = lo | (hi << 8);
        return true;
    };
    auto dword = [&](unsigned &value) {
        unsigned lo = 0, hi = 0;
        if (!word(lo) || !word(hi)) return false;
        value = lo | (hi << 16);
        return true;
    };
    auto emit16 = [&](unsigned value) {
        if (put + 2 > capacity) return false;
        out[put++] = value & 255;
        out[put++] = (value >> 8) & 255;
        return true;
    };
    auto emit32 = [&](unsigned value) { return emit16(value & 65535) && emit16(value >> 16); };
    auto align = [&] { while (put & 3) { if (!emit16(0)) return false; } return true; };
    // A zero-terminated ANSI string becomes a zero-terminated UTF-16 one; the
    // marker FFh with a following word stays the ordinal it is.
    auto text = [&] {
        if (at < size && source[at] == 0xff)
        {
            unsigned ordinal = 0;
            ++at;
            if (!word(ordinal)) return false;
            return emit16(0xffff) && emit16(ordinal);
        }
        for (;;)
        {
            unsigned ch = 0;
            if (!byte(ch)) return false;
            if (!emit16(ch)) return false;
            if (!ch) return true;
        }
    };
    unsigned style = 0, items = 0, x = 0, y = 0, cx = 0, cy = 0;
    if (!dword(style) || !byte(items) || !word(x) || !word(y) || !word(cx) || !word(cy)) return false;
    if (!emit32(style) || !emit32(0) || !emit16(items) ||
        !emit16(x) || !emit16(y) || !emit16(cx) || !emit16(cy))
        return false;
    if (!text() || !text() || !text()) return false;         // menu, class, caption
    if (style & 0x40)                                        // DS_SETFONT
    {
        unsigned points = 0;
        if (!word(points) || !emit16(points) || !text()) return false;
    }
    for (unsigned item = 0; item < items; ++item)
    {
        unsigned id = 0, item_style = 0, klass = 0;
        if (!align()) return false;
        if (!word(x) || !word(y) || !word(cx) || !word(cy) || !word(id) || !dword(item_style))
            return false;
        if (!emit32(item_style) || !emit32(0) || !emit16(x) || !emit16(y) || !emit16(cx) ||
            !emit16(cy) || !emit16(id))
            return false;
        if (at >= size) return false;
        if (source[at] >= 0x80)
        {
            // 80h button, 81h edit, 82h static, 83h list box, 84h scroll bar,
            // 85h combo box; the 32-bit form writes the same numbers as atoms.
            if (!byte(klass) || !emit16(0xffff) || !emit16(klass)) return false;
        }
        else
        {
            // The 16-bit COMMCTRL registers its classes without the "32"
            // the 32-bit names carry; the controls here are COMCTL32's.
            static const char *const renamed[][2] = {
                {"msctls_hotkey", "msctls_hotkey32"}, {"msctls_trackbar", "msctls_trackbar32"},
                {"msctls_updown", "msctls_updown32"}, {"msctls_progress", "msctls_progress32"},
                {"msctls_statusbar", "msctls_statusbar32"}};
            const char *name = reinterpret_cast<const char *>(source + at);
            unsigned n = 0;
            while (at + n < size && source[at + n]) ++n;
            const char *wide = nullptr;
            for (const auto &r : renamed)
                if (n == strlen(r[0]) && !strncmp(name, r[0], n)) wide = r[1];
            if (wide)
            {
                at += n + 1;
                for (const char *p = wide;; ++p)
                {
                    if (!emit16((unsigned char)*p)) return false;
                    if (!*p) break;
                }
            }
            else if (!text()) return false;
        }
        if (!text()) return false;
        unsigned extra = 0;
        if (!byte(extra)) return false;
        if (extra) return false;                             // no original USER dialog carries any
        if (!emit16(0)) return false;
    }
    length = put;
    return true;
}

// shows the confirmation the user sees for a task that will not close
// on its own. The template is RT_DIALOG 11 of the original USER.EXE, the same
// source the cursors come from, and its three buttons carry
// the ids 4 (Wait), 3 (End Task) and 2 (Cancel), which are the answers EndTask
// hands back unchanged.
unsigned Application::EndTaskDialog(unsigned owner)
{
    const bool recorded = TakeFresh();
    const unsigned char *bytes = nullptr;
    unsigned size = 0;
    for (const auto &resource : OriginalImageResources)
        if (resource.type == ResourceDialog && resource.id == TaskDialogResource && !resource.name[0])
        {
            bytes = resource.data;
            size = resource.Size();
            break;
        }
    if (!bytes)
    {
        Fail("EndTask: USER.EXE dialog resource 11 is missing");
        return TaskEnded;
    }
    auto *converted = new unsigned char[4 * size + 64];
    if (!converted) { last_error = 8; return TaskEnded; }
    unsigned length = 0;
    unsigned answer = TaskEnded;
    if (!ConvertDialogTemplate(bytes, size, converted, 4 * size + 64, length))
    {
        delete[] converted;
        Fail("EndTask: the USER.EXE dialog template does not read");
        return TaskEnded;
    }
    if (owner && !User::IsWindow(owner)) owner = 0;
    const unsigned hwnd = CreateDialogTemplate(converted, length, owner, EndTaskDialogProcedure, 0, true);
    delete[] converted;
    if (!hwnd) return TaskEnded;
    const bool kept = recorded && PushHostFrame(HostEndTaskDialog, hwnd);
    if (kept) GiveTail(TailRecord);
    const unsigned result = RunModal(hwnd, kept);
    if (kept) PopHostFrame();
    return result == 2 || result == 3 || result == 4 ? result : TaskEnded;
}
unsigned Application::ConfirmEndTask(unsigned owner, unsigned task, bool recorded)
{
    const bool kept = recorded && PushHostFrame(HostEndTask, task);
    if (kept) GiveTail(TailFresh);
    const unsigned result = EndTaskDialog(owner);
    if (kept) PopHostFrame();
    return FinishEndTask(result, task);
}
unsigned Application::FinishEndTask(unsigned result, unsigned task)
{
    if (state == Failed || state == Exited) return TaskEnded;
    if (result == TaskEnded && !TerminateTask(task)) result = TaskFailed;
    return result;
}

// The dialog procedure USER installs. The template's buttons already
// carry the answers, so the only work is to hand the pressed id back.
unsigned Application::EndTaskDialogMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    if (message == 0x110) return 1;                 // WM_INITDIALOG
    if (message == 0x111 && (wp == 2 || wp == 3 || wp == 4))
    {
        const unsigned arguments[] = {hwnd, wp};
        Api("EndDialog", arguments);
        return 1;
    }
    (void)lp;
    return 0;
}

namespace {
bool close_program_pending = false, close_program_active = false;
unsigned close_program_window = 0;
unsigned close_program_previous = 0;
unsigned close_program_system_thread = 0;
}

// VMM 00090001's system UI state: the CAD thread owns the screen while the
// task snapshot is displayed. In particular a full-screen game cannot paint
// over its box. Threads resume before ExitWindowsEx sends session messages.
bool CloseProgramThreadRunnable(unsigned id)
{
    return !close_program_system_thread || id == close_program_system_thread;
}

void Application::RequestCtrlAltDelete()
{
    // KERNEL32 starts a system thread, rather than sending Delete
    // to the foreground application's queue. Repeated requests share its box.
    if (close_program_active)
    {
        if (close_program_window) RequestForeground(close_program_window);
        return;
    }
    if (!close_program_pending) close_program_previous = User::GetForegroundWindow();
    close_program_pending = true;
}

void Application::StartCloseProgramRequest()
{
    if (!close_program_pending || close_program_active) return;
    close_program_pending = false;
    if (!threads[current_thread].context || !Check(cpu->Restore(threads[current_thread].context))) return;
    const unsigned args[] = {0, 0, CloseProgramThread, 0, 0, 0};
    close_program_active = true;
    const unsigned handle = CreateThread(args);
    if (handle)
    {
        close_program_system_thread = threads[FindKernelHandle(handle)->object].id;
        ThreadApi("CloseHandle", &handle);
    }
    else close_program_active = false;
}

void Application::CloseProgramDialog()
{
    close_program_thread_id = ThreadId();
    const unsigned previous = close_program_previous;
    const bool kept = current_thread && ThreadId() == close_program_system_thread &&
                      Register(Reg::Eip) == CloseProgramThread && PushHostFrame(HostCloseProgram, 0, previous);
    const unsigned frame_at = threads[current_thread].host_frame_count ? threads[current_thread].host_frame_count - 1 : 0;
    // Original KERNEL32 RT_DIALOG 2, loaded as data only. Its DIALOGEX
    // template carries the caption, warning, list, and all four control ids.
    const unsigned module = LoadDatafileModule("KERNEL32.DLL");
    auto *data = DatafileAt(module);
    unsigned size = 0;
    const unsigned char *bytes = data ? data->pe->Resource(5, 2, &size) : nullptr;
    User::GrantQueuePass(ThreadId());
    const unsigned hwnd = bytes ? CreateDialogTemplate(bytes, size, 0, CloseProgramDialogProcedure, 0, true) : 0;
    if (module) FreeDatafileModule(module);
    close_program_window = hwnd;
    if (hwnd)
    {
        User::GrantQueuePass(ThreadId()); // the user requested this system window
        SetForegroundApi(hwnd);
        const unsigned args[] = {hwnd, 3};
        SetFocus(Api("GetDlgItem", args));
        if (log)
        {
            char trace[160];
            snprintf(trace, sizeof trace, "Close Program foreground=%x focus=%x thread=%u input=%u", User::GetForegroundWindow(), User::GetFocus(), ThreadId(), InputThread());
            log(trace);
        }
    }
    if (kept) { auto &frame = threads[current_thread].host_frames[frame_at]; frame.a = hwnd; frame.d = 1; GiveTail(TailRecord); }
    const unsigned answer = hwnd ? RunModal(hwnd, kept) : 0;
    if (kept) threads[current_thread].host_frames[frame_at].d = 2;
    FinishCloseProgramDialog(answer, previous);
    if (kept) PopHostFrame();
}
unsigned Application::FinishCloseProgramDialog(unsigned answer, unsigned previous)
{
    close_program_window = 0;
    close_program_active = false;
    close_program_system_thread = 0;
    if (answer != 1 && state != Failed && state != Exited)
    {
        const unsigned target = User::IsWindow(previous) ? previous : User::RegisteredShellWindow();
        if (target)
        {
            User::GrantQueuePass(ThreadId());
            SetForegroundApi(target);
        }
    }
    close_program_thread_id = 0;
    // only the Shut Down answer (1) asks ExitWindowsEx(1,0).
    if (answer == 1 && state != Failed && state != Exited)
    {
        const unsigned args[] = {1, 0};
        UserSessionApi("ExitWindowsEx", args);
    }
    return 0;
}
bool Application::SavedCloseProgramThread(unsigned index) const
{
    const auto &t = threads[index];
    if (!index || !t.host_waiting || t.host_frame_count < 2 || t.id != close_program_system_thread ||
        t.id != close_program_thread_id) return false;
    const auto &frame = t.host_frames[0];
    return frame.kind == HostCloseProgram && frame.d == 1 && frame.a &&
           frame.a == close_program_window && close_program_active && User::IsWindow(frame.a);
}
void Application::ResumeCloseProgramDialog()
{
    auto &t = threads[current_thread];
    t.host_resume = true; t.host_resume_at = 0; t.host_resume_top = depth; t.host_resume_level = 0; t.tail = TailNone;
    ResumeHostCall();
    t.host_resume = false;
    const unsigned result = 0;
    ThreadApi("ExitThread", &result);
    EndThreadStaticTls(current_thread);
}

unsigned Application::CloseProgramDialogMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    const unsigned item_args[] = {hwnd, 3};
    const unsigned list = Api("GetDlgItem", item_args);
    if (message == 0x110)
    {
        // /: top-level windows first, preferring visible
        // unowned windows with titles; deduplicates by process.
        unsigned windows[User::WindowCapacity], count = 0;
        unsigned seen[Win98SessionArena::HolderCapacity] = {}, used = 0;
        User::WindowSnapshot(0, windows, User::WindowCapacity, count);
        const unsigned text = Allocate(260);
        auto add = [&](unsigned pid, const char *title) {
            if (!pid || !title[0] || !text || used == Win98SessionArena::HolderCapacity) return;
            for (unsigned i = 0; i < used; ++i) if (seen[i] == pid) return;
            seen[used++] = pid;
            Store(text, title, strlen(title) + 1);
            const unsigned index = Send(list, 0x180, 0, text); // LB_ADDSTRING
            if (index != ~0u && index != ~1u) Send(list, 0x19a, index, pid);
        };
        for (unsigned pass = 0; pass < 2; ++pass)
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned window = windows[i];
                char title[260] = {}, klass[64] = {};
                User::GetClassName(window, klass, sizeof klass);
                if (!strcmp(klass, "Progman") || !strcmp(klass, "Shell_TrayWnd") || window == hwnd) continue;
                if (pass == 0 && (!User::IsVisible(window) || User::Owner(window))) continue;
                User::GetWindowText(window, title, sizeof title);
                if (!User::IsVisible(window))
                {
                    title[0] = 0;
                    for (auto *process : session_processes)
                        if (process && process->process_id == User::WindowProcess(window))
                        { snprintf(title, sizeof title, "%s", process->program_name); break; }
                }
                add(User::WindowProcess(window), title);
            }
        // The original adds tasks without suitable windows by executable name.
        for (auto *process : session_processes)
        {
            if (!process || process->state == Failed || process->state == Exited) continue;
            const char *name = process->program_name;
            for (const char *p = name; *p; ++p) if (*p == '\\' || *p == '/') name = p + 1;
            add(process->process_id, name);
        }
        if (text) Free(text);
        Send(list, 0x186, 0, 0); // LB_SETCURSEL,
        return 1;
    }
    if (message == 0x112 && (wp & 0xffff) == 0xf140) return 1; // SC_SCREENSAVE
    if (message != 0x111) return 0;
    const unsigned id = wp & 0xffff;
    if (id == 2 || id == 4)
    {
        const unsigned args[] = {hwnd, id == 4 ? 1u : 0u};
        Api("EndDialog", args);
        return 1;
    }
    if (id == 1 || (id == 3 && (wp >> 16) == 2))
    {
        const unsigned index = Send(list, 0x188, 0, 0);
        if (index == ~0u) return 1;
        const unsigned pid = Send(list, 0x199, index, 0);
        unsigned window = 0, windows[User::WindowCapacity], count = 0;
        User::WindowSnapshot(0, windows, User::WindowCapacity, count);
        for (unsigned i = 0; i < count; ++i)
            if (User::WindowProcess(windows[i]) == pid && windows[i] != hwnd && User::IsVisible(windows[i]))
            { window = windows[i]; break; }
        // Windows 98 chooses EndTask flags 0Eh for a responsive window:
        // request WM_CLOSE, do not wait, and do not show the secondary box.
        const unsigned args[] = {window, pid, 0, window ? 14u : 0u};
        const bool kept = TakeTail() == TailRecord && PushHostFrame(HostCloseProgramMessage, hwnd);
        if (kept) GiveTail(TailFresh);
        const unsigned answer = UserSessionApi("EndTask", args);
        if (kept) PopHostFrame();
        if (answer == TaskEnded && state != Failed && state != Exited)
        {
            const unsigned end[] = {hwnd, 0};
            Api("EndDialog", end);
        }
        return 1;
    }
    (void)lp;
    return 0;
}

Application::SessionEndHook Application::session_end_hook = nullptr;
void *Application::session_end_context = nullptr;
void Application::SetSessionEndHook(void *context, SessionEndHook hook)
{
    session_end_context = context;
    session_end_hook = hook;
}
bool Application::HasSessionEndHook() { return session_end_hook != nullptr; }
bool Application::session_committed = false;
unsigned Application::session_committed_flags = 0, Application::session_committed_code = 0;
bool Application::SessionCommitted(unsigned &flags, unsigned &code)
{
    flags = session_committed_flags;
    code = session_committed_code;
    return session_committed;
}

// USER hands WM_QUERYENDSESSION and WM_ENDSESSION to
// BroadcastSystemMessage with the recipients BSM_VXDS, BSM_NETDRIVER,
// BSM_INSTALLABLEDRIVERS and, once the shell window is gone, BSM_APPLICATIONS.
// NEO98 has receivers for the application windows alone, so the walk is the
// one of USER over the desktop's children, with the per-window
// decisions of win98_broadcast.h. BSF_QUERY stops at the first denial; that is
// what Windows 98 reads out of the result.
unsigned Application::SessionBroadcast(unsigned message, unsigned wp, unsigned lp, bool query)
{
    unsigned windows[User::WindowCapacity], count = 0;
    if (!User::WindowSnapshot(0, windows, User::WindowCapacity, count))
    {
        last_error = 8;
        return 0;
    }
    Win98Broadcast::Request request;
    request.message = message;
    request.wparam = wp;
    for (unsigned i = 0; i < count; ++i)
    {
        if (!User::IsWindow(windows[i]))
            continue;
        Win98Broadcast::Window window;
        window.handle = windows[i];
        window.queue = User::WindowThread(windows[i]);
        char klass[64] = {};
        User::GetClassName(windows[i], klass, sizeof klass);
        window.excluded = !strcmp(klass, "#32768");
        if (!Win98Broadcast::Accepts(request, window))
            continue;
        // USER includes BSF_NOHANG | BSF_FORCEIFHUNG when it
        // reaches the application broadcast. A worker's hidden OLE window
        // can belong to a thread waiting in KERNEL instead of pumping USER
        // messages. A plain Send waits forever there and leaves shutdown
        // behind its already dismissed dialog. Keep explicit vetoes, but
        // bound delivery to another queue through the existing timeout path.
        bool completed = true;
        unsigned result = 0;
        if (User::WindowProcess(windows[i]) && User::WindowProcess(windows[i]) != process_id)
            result = SendCrossProcess(windows[i], message, wp, lp, 5000, 0, &completed);
        else if (User::WindowThread(windows[i]) && User::WindowThread(windows[i]) != ThreadId())
            result = SendCrossThread(windows[i], message, wp, lp, 5000, 0, &completed);
        else
            result = Send(windows[i], message, wp, lp);
        if (state == Failed)
            return 0;
        if (!completed)
        {
            if (log)
            {
                char text[160];
                snprintf(text, sizeof text, "Session end skips unresponsive hwnd=%x class=%s", windows[i], klass);
                log(text);
            }
            continue;
        }
        if (query && !result)
        {
            if (log)
            {
                char text[256];
                snprintf(text, sizeof text, "Session end denied hwnd=%x class=%s process=%u", windows[i], klass,
                         User::WindowProcess(windows[i]));
                log(text);
            }
            return 0;
        }
    }
    return 1;
}

// USER, the end of the session both ExitWindows (the code
// as given and no flags) and ExitWindowsEx run.
unsigned Application::EndSession(unsigned flags, unsigned code)
{
    // a second call while the first one runs answers zero.
    if (session_ending)
        return 0;
    // NEO98 has no way of its own to leave the machine. The host owns the
    // restart path (tools/neo98_host.cpp) and
    // installs the provider. Without one the call cannot do what it
    // promises, so it fails before it disturbs a single window.
    if (!session_end_hook)
    {
        last_error = 120;
        return 0;
    }
    session_ending = true;
    // WM_QUERYENDSESSION, wParam zero, lParam the flags.
    const unsigned agreed = SessionBroadcast(0x11, 0, flags, true);
    if (state == Failed)
        return 0;
    // The machine is going: a program that ends on WM_ENDSESSION, the
    // shell above all, may take the process that runs this loop with it
    // (the host stops the guest when its first process ends). The host
    // reads this and still leaves the way would.
    if (agreed)
    {
        session_committed = true;
        session_committed_flags = flags;
        session_committed_code = code;
        // once everyone agreed, USER sends 508h to the window
        // of MSGSRV32.EXE, which plays "SystemExit" synchronously in its
        // MMTASK and answers the address of a word it sets
        // when the sound is over; USER yields until it is set. The sound
        // therefore ends before any program hears WM_ENDSESSION, and the
        // machine waits exactly as long as the sound lasts. An empty entry
        // of the scheme plays nothing and costs no time.
        PlaySessionSound("SystemExit", true);
        if (state == Failed)
            return 0;
    }
    // WM_ENDSESSION follows in either case; its wParam is the
    // result of the query, so a denied end of session is announced too.
    SessionBroadcast(0x16, agreed, flags, false);
    if (state == Failed)
        return 0;
    if (!agreed)
    {
        // Windows 98 clears the flag again and Windows 98 returns the zero.
        session_ending = false;
        return 0;
    }
    // pushes the code to KERNEL ExitKernel and never returns.
    session_end_hook(session_end_context, *this, flags, code);
    return 1;
}

// Map screen storage into guest memory on the first mode change or DCI call.
// Applications that never access framebuffer pixels directly need no mapping.
// The framebuffer belongs to the display session and has the same address in
// every process, surviving process exit. Use session-arena views rather than
// private process allocations. Keep two views per size class because old and
// new surfaces briefly coexist during a mode change. Power-of-two classes
// starting at 1 MiB limit the number of mappings; views persist for the
// session.
void Application::InstallScreenLease()
{
    User::SetScreenLeaseProvider(
        this,
        [](void *p, unsigned bytes, unsigned &guest, unsigned char *&host) {
            auto &app = *static_cast<Application *>(p);
            unsigned size = 1u << 20;
            while (size < bytes && size < Win98SessionArena::BlockLimit) size <<= 1;
            if (size < bytes) return false;
            for (unsigned slot = 0; slot < 2; ++slot)
            {
                char name[64];
                snprintf(name, sizeof name, "NEO98 display frame buffer %u MiB %c", size >> 20, 'A' + slot);
                const unsigned base = app.ReserveSessionView(name, size);
                if (!base) return false;
                if (base == User::ScreenGuestAddress()) continue; // the surface that still stands
                // Committed once for the session; a process that joined later
                // has the pages already (AttachSessionBlock).
                if (!app.virtual_memory.Access(base, size, true) &&
                    !app.CommitSessionView(base >> 12, size >> 12))
                    return false;
                unsigned char *bits = app.SessionBytes(base, size);
                if (!bits) return false;
                guest = base;
                host = bits;
                return true;
            }
            return false;
        },
        [](void *, unsigned, unsigned char *) {});
}

// Decode a DEVMODE of at least 0x78 bytes. Missing or zero values come from
// the registered desktop, not a temporary mode. Width and height form one
// optional pair.
bool Application::DisplayModeOf(unsigned devmode, int &width, int &height, int &depth)
{
    RegistryDisplayMode(width, height, depth);
    unsigned short size = 0;
    if (!Read(devmode + 36, &size, 2)) { last_error = 87; return false; }
    if (size < 0x78) return false;
    unsigned char mode[DevModeSize] = {};
    if (size > DevModeSize) size = DevModeSize;
    if (!Read(devmode, mode, size)) { last_error = 87; return false; }
    auto get32 = [&](unsigned at) {
        unsigned v = 0;
        for (unsigned i = 0; i < 4; ++i) v |= unsigned(mode[at + i]) << (8 * i);
        return v;
    };
    const unsigned fields = get32(40);
    if ((fields & FieldDepth) && get32(104)) depth = int(get32(104));
    if ((fields & (FieldWidth | FieldHeight)) == (FieldWidth | FieldHeight) &&
        get32(108) && get32(112))
    {
        width = int(get32(108));
        height = int(get32(112));
    }
    return User::ScreenModeSupported(width, height, depth);
}

// The host's screen stands in for the size of the registry while the host
// asks for it; the depth stays the registry's.
namespace
{
int host_display_width = 0, host_display_height = 0;
bool host_display_pending = false;
// A window the user picked outside the guest (Mission Control, the window
// menu of the Mac): it comes to the foreground like a window picked on the
// taskbar.
unsigned host_foreground = 0;
struct HostSizeRequest { unsigned hwnd = 0; int width = 0, height = 0; };
HostSizeRequest host_sizes[16];
// The window that was the foreground when the host's application lost the
// activation; it gets the foreground back when the application has it again
// and nothing else took it meanwhile.
unsigned host_left = 0;
bool host_return = false;
// The host wants the whole monitor as the work area; what the shell last
// asked for with SPI_SETWORKAREA.
bool host_work_whole = false, host_work_pending = false, host_work_known = false;
int host_work_asked[4] = {};
bool host_keeps_dos_windows = false;
}

// HKCC\Display\Settings stores Resolution as width,height and BitsPerPixel as
// depth, both REG_SZ. Missing or unsupported values use the setup default,
// 800x600x32.
void Application::RegistryDisplayMode(int &width, int &height, int &depth)
{
    StoredDisplayMode(width, height, depth);
    if (host_display_width && User::ScreenModeSupported(host_display_width, host_display_height, depth))
    {
        width = host_display_width;
        height = host_display_height;
    }
}
void Application::StoredDisplayMode(int &width, int &height, int &depth)
{
    width = int(NativeWidth); height = int(NativeHeight); depth = int(NativeDepth);
    auto &registry = Registry::System();
    unsigned key = Registry::Missing;
    bool created = false;
    if (registry.Open(registry.roots[CurrentConfigRoot], DisplaySettingsKey, false, key, created)) return;
    auto text = [&](const char *name, char *out, unsigned capacity) {
        const Registry::Value *value = nullptr;
        if (registry.Query(key, name, value) || !value || !value->data || !value->size) return false;
        unsigned n = value->size < capacity - 1 ? value->size : capacity - 1;
        memcpy(out, value->data, n);
        out[n] = 0;
        return true;
    };
    auto number = [](const char *&p) {
        while (*p == ' ') ++p;
        int v = 0, digits = 0;
        while (*p >= '0' && *p <= '9' && digits < 6) { v = v * 10 + (*p++ - '0'); ++digits; }
        return digits ? v : -1;
    };
    char resolution[32] = {}, bits[16] = {};
    if (!text("Resolution", resolution, sizeof resolution) || !text("BitsPerPixel", bits, sizeof bits)) return;
    const char *p = resolution;
    const int w = number(p);
    while (*p == ' ') ++p;
    if (*p++ != ',') return;
    const int h = number(p);
    p = bits;
    const int d = number(p);
    if (!User::ScreenModeSupported(w, h, d)) return;
    width = w; height = h; depth = d;
}
void Application::SetHostDisplay(int width, int height, bool apply)
{
    if (width > 0 && height > 0 && User::SetScreenPanel(width, height))
    {
        host_display_width = width;
        host_display_height = height;
    }
    else host_display_width = host_display_height = 0;
    host_display_pending = apply;
}
void Application::HostDisplay(int &width, int &height)
{
    width = host_display_width;
    height = host_display_height;
}
bool Application::HostDisplayPending() { return host_display_pending; }
void Application::RequestForeground(unsigned hwnd) { host_foreground = hwnd; }
void Application::RequestWindowSize(unsigned hwnd, int width, int height)
{
    for (auto &request : host_sizes) if (!request.hwnd || request.hwnd == hwnd || !User::IsWindow(request.hwnd)) {
        request = {hwnd, width, height}; return;
    }
}
bool Application::HostRequestPending() {
    for (const auto &request : host_sizes) if (request.hwnd) return true;
    return host_display_pending || host_foreground || host_return || host_work_pending;
}
void Application::SetHostWorkArea(bool whole)
{
    if (whole == host_work_whole) return;
    host_work_whole = whole;
    host_work_pending = true;
}
bool Application::HostWorkWhole() { return host_work_whole; }
void Application::SetHostKeepsDosWindows(bool keep) { host_keeps_dos_windows = keep; }
bool Application::HostKeepsDosWindows() { return host_keeps_dos_windows; }
namespace { unsigned host_zoomed_window = 0; }
void Application::SetHostZoomedWindow(unsigned window) { host_zoomed_window = window; }
unsigned Application::HostZoomedWindow() { return host_zoomed_window; }
namespace
{
unsigned dos_fullscreen_window = 0;
int dos_fullscreen_strip = 0;
struct DosWindowNote { unsigned window; int strip; } dos_windows[16] = {};
} // namespace
void Application::AskHostDosFullscreen(unsigned window, int strip)
{
    dos_fullscreen_window = window;
    dos_fullscreen_strip = strip;
    NoteDosWindow(window, strip);
}
bool Application::TakeHostDosFullscreen(unsigned &window, int &strip)
{
    if (!dos_fullscreen_window)
        return false;
    window = dos_fullscreen_window;
    strip = dos_fullscreen_strip;
    dos_fullscreen_window = 0;
    return true;
}
void Application::NoteDosWindow(unsigned window, int strip)
{
    DosWindowNote *free_note = nullptr;
    for (auto &note : dos_windows)
    {
        if (note.window == window) { note.strip = strip; return; }
        if (!free_note && (!note.window || !User::IsWindow(note.window))) free_note = &note;
    }
    if (free_note) *free_note = {window, strip};
}
int Application::DosWindowStrip(unsigned window)
{
    for (const auto &note : dos_windows)
        if (note.window == window && window)
            return note.strip;
    return -1;
}
bool Application::HostWorkAsked(const void *rect, void *out)
{
    if (rect) { memcpy(host_work_asked, rect, sizeof host_work_asked); host_work_known = true; }
    if (out && host_work_known) memcpy(out, host_work_asked, sizeof host_work_asked);
    return host_work_known;
}
void Application::HostActivation(bool active)
{
    const unsigned queue = User::ForegroundQueue();
    if (!active)
    {
        host_return = false;
        if (!queue) return;
        host_left = User::GetForegroundWindow();
        User::PostQueueLeave(queue, true);
        return;
    }
    // Back before the queue heard of the loss: nothing happened.
    if (queue && User::TakeQueueLeave(queue)) { host_left = 0; return; }
    host_return = host_left != 0;
}
void Application::ServeHostRequests()
{
    if (host_desktop_pending) ServeHostDesktopIcons();
    for (auto &pending : host_sizes) if (pending.hwnd) {
        const auto request = pending; pending = {};
        if (!User::IsWindow(request.hwnd)) continue;
        int sw = 0, sh = 0, depth = 0; User::ScreenMode(sw, sh, depth);
        Gdi::RECT r{}; User::GetWindowRect(request.hwnd, &r);
        int width = request.width < sw ? request.width : sw;
        int height = request.height < sh ? request.height : sh;
        if (width < 32) width = 32;
        if (height < 32) height = 32;
        int left = r.left < sw - width ? r.left : sw - width;
        int top = r.top < sh - height ? r.top : sh - height;
        if (left < 0) left = 0;
        if (top < 0) top = 0;
        unsigned position[1][7] = {{request.hwnd, 0, unsigned(left), unsigned(top),
                                   unsigned(width), unsigned(height), 0x14}};
        PositionWindows(1, position);
    }
    if (host_icon_wanted && !host_icon_tried)
    {
        host_icon_tried = true;
        unsigned large = 0, small = 0;
        if (modules[0].path[0])
            ShellExtractIconPair(modules[0].path, 0, (16u << 16) | 32u, (16u << 16) | 32u, 0, large, small);
        host_program_icon = large ? large : small;
    }
    if (host_display_pending)
    {
        host_display_pending = false;
        int width = 0, height = 0, depth = 0;
        RegistryDisplayMode(width, height, depth);
        ApplyDisplayMode(width, height, depth, 0);
    }
    if (host_work_pending)
    {
        // SPI_SETWORKAREA with the monitor, or with what the shell asked for
        // last: the metrics follow and maximized windows move.
        host_work_pending = false;
        const auto &settings = User::SystemSettings();
        int area[4] = {settings.monitor[0], settings.monitor[1], settings.monitor[2], settings.monitor[3]};
        if (!host_work_whole) HostWorkAsked(nullptr, area);
        if (const unsigned block = Allocate(sizeof area))
        {
            Store(block, area, sizeof area);
            const unsigned a[4] = {47, 0, block, 0};
            serving_host_work = true;
            Api("SystemParametersInfoA", a);
            serving_host_work = false;
            Free(block);
        }
    }
    if (host_return)
    {
        // A click on a guest window that brought the application back has
        // made that window the foreground already.
        host_return = false;
        const unsigned hwnd = host_left;
        host_left = 0;
        if (!User::ForegroundQueue() && !host_foreground && User::IsWindow(hwnd) && User::IsVisible(hwnd))
            host_foreground = hwnd;
    }
    if (const unsigned hwnd = host_foreground)
    {
        host_foreground = 0;
        if (!User::IsWindow(hwnd)) return;
        // What the taskbar does for a click on a button: the last active
        // popup of the window comes to the foreground.
        unsigned a[1] = {hwnd};
        const unsigned popup = ActivationApi(ApiName("GetLastActivePopup"), a);
        if (popup && User::IsWindow(popup)) a[0] = popup;
        // Under Win98 the user's click on the button went to the taskbar, so
        // the taskbar may set the foreground (: the process that took
        // the last press is not locked). Here the choice was made on the Mac
        // (the Dock, the Window menu, Mission Control) and any thread
        // serves it; it gets the permission the taskbar would have. Without
        // it the choice did nothing for 15 s after a press on another
        // program's window.
        User::GrantQueuePass(ThreadId());
        // A minimized window comes back as by its button: SwitchToThisWindow
        // queues SC_RESTORE for it.
        if (User::Style(a[0]) & 0x20000000u)
        {
            const unsigned s[2] = {a[0], 1};
            ActivationApi(ApiName("SwitchToThisWindow"), s);
        }
        else
        {
            ActivationApi(ApiName("SetForegroundWindow"), a);
            // The window chosen on the Mac comes to the front even when it
            // is the active window of the foreground already and another
            // window lies over it; an activation changes no order then.
            if (User::IsWindow(a[0]) && User::GetForegroundWindow() == a[0]) User::OrderWindow(a[0], 0);
        }
    }
}
bool Application::WriteRegistryDisplayMode(int width, int height, int depth)
{
    auto &registry = Registry::System();
    unsigned key = Registry::Missing;
    bool created = false;
    if (registry.Open(registry.roots[CurrentConfigRoot], DisplaySettingsKey, true, key, created)) return false;
    char resolution[32], bits[16];
    const unsigned r = unsigned(snprintf(resolution, sizeof resolution, "%d,%d", width, height)) + 1;
    const unsigned b = unsigned(snprintf(bits, sizeof bits, "%d", depth)) + 1;
    // The size of the host's screen is the host's and is not stored; another
    // size is the guest's own choice, and the host's screen gives way to it.
    int panel_width = 0, panel_height = 0;
    User::ScreenPanel(panel_width, panel_height);
    if (panel_width && width == panel_width && height == panel_height)
        return !registry.Set(key, "BitsPerPixel", 1, bits, b);
    host_display_width = host_display_height = 0;
    return !registry.Set(key, "Resolution", 1, resolution, r) && !registry.Set(key, "BitsPerPixel", 1, bits, b);
}
// Initialize the desktop mode from the registry once per session, before the
// first process creates windows. Fall back to 800x600x32 when the requested
// mode cannot be used as a desktop.
void Application::LoadDisplayStartMode()
{
    auto &settings = User::SystemSettings();
    if (settings.display_checked) return;
    settings.display_checked = true;
    int width = 0, height = 0, depth = 0;
    RegistryDisplayMode(width, height, depth);
    if (!User::DesktopModeSupported(width, height, depth)) return;
    int current_width = 0, current_height = 0, current_depth = 0;
    User::ScreenMode(current_width, current_height, current_depth);
    User::SetDesktopMode(width, height, depth);
    if (current_width != width || current_height != height || current_depth != depth)
        User::SetScreenMode(width, height, depth);
}

// USER around the change itself. CDS_TEST clears CDS_UPDATEREGISTRY. A null
// DEVMODE goes to the mode of the registry and refuses CDS_UPDATEREGISTRY and
// CDS_NORESET. With a DEVMODE the original writes the mode into
// HKCC\Display\Settings first, because the driver reads the mode it enables
// from there, and puts the old values back afterwards unless the caller asked
// for CDS_UPDATEREGISTRY and the change did not fail. CDS_TEST and CDS_NORESET
// stop before the driver: the registry keeps the new mode, the screen stays
// as it is, and the answer is DISP_CHANGE_SUCCESSFUL. That is how the Settings
// page of DESK.CPL stores 256 colours for the next start.
unsigned Application::ChangeDisplaySettings(unsigned devmode, unsigned flags)
{
    if (flags & ChangeTest) flags &= ~ChangeUpdateRegistry;
    int width = 0, height = 0, depth = 0;
    if (!devmode)
    {
        if (flags & (ChangeUpdateRegistry | ChangeNoReset)) return ChangeBadFlags;
        RegistryDisplayMode(width, height, depth);
        return ApplyDisplayMode(width, height, depth, flags);
    }
    if (!DisplayModeOf(devmode, width, height, depth)) return ChangeBadMode;
    if (flags & ChangeTest) return ChangeSuccessful;
    if (flags & ChangeUpdateRegistry)
    {
        int old_width = 0, old_height = 0, old_depth = 0;
        RegistryDisplayMode(old_width, old_height, old_depth);
        if (!WriteRegistryDisplayMode(width, height, depth)) return ChangeNotUpdated;
        if (flags & ChangeNoReset) return ChangeSuccessful;
        const unsigned result = ApplyDisplayMode(width, height, depth, flags);
        if (result != ChangeSuccessful) WriteRegistryDisplayMode(old_width, old_height, old_depth);
        return result;
    }
    if (flags & ChangeNoReset) return ChangeSuccessful;
    return ApplyDisplayMode(width, height, depth, flags);
}

// CDS_TEST only validates support. A successful mode change sends
// WM_DISPLAYCHANGE to top-level windows with depth in wParam and dimensions
// in lParam.
unsigned Application::ApplyDisplayMode(int width, int height, int depth, unsigned flags)
{
    if (!User::ScreenModeSupported(width, height, depth)) return ChangeBadMode;
    if (flags & ChangeTest) return ChangeSuccessful;
    // The registry mode belongs to the desktop. Other modes belong to the
    // application that selects them.
    int registry_width = 0, registry_height = 0, registry_depth = 0;
    RegistryDisplayMode(registry_width, registry_height, registry_depth);
    int desktop_width = 0, desktop_height = 0, desktop_depth = 0;
    User::DesktopMode(desktop_width, desktop_height, desktop_depth);
    bool desktop_changed = false;
    if (registry_width == width && registry_height == height && registry_depth == depth &&
        User::DesktopModeSupported(width, height, depth) &&
        (desktop_width != width || desktop_height != height || desktop_depth != depth))
        desktop_changed = true;
    int current_width = 0, current_height = 0, current_depth = 0;
    User::ScreenMode(current_width, current_height, current_depth);
    if (current_width == width && current_height == height && current_depth == depth && !desktop_changed)
        return ChangeSuccessful;
    // Switch immediately, matching Windows 98: retarget window DCs to the new
    // surface, recompute visible regions and invalidate windows. This also
    // applies to DCs currently used inside control callbacks.
    InstallScreenLease();
    // What the Direct3D rasterizer keeps for the screen and the video memory
    // belongs to the old mode.
    HalReleaseAll();
    // the monitor's rectangles before the change.
    const auto &settings = User::SystemSettings();
    const Win98DisplayReposition::Rect old_monitor{settings.monitor[0], settings.monitor[1], settings.monitor[2],
                                                   settings.monitor[3]};
    const Win98DisplayReposition::Rect old_work{settings.work[0], settings.work[1], settings.work[2], settings.work[3]};
    if (desktop_changed) User::SetDesktopMode(width, height, depth);
    if (!User::SetScreenMode(width, height, depth))
    {
        if (desktop_changed) User::SetDesktopMode(desktop_width, desktop_height, desktop_depth);
        return ChangeFailed;
    }
    DriverTrace("display mode", unsigned(width) << 16 | unsigned(height));
    RepositionTopLevel(old_monitor, old_work, (flags & ChangeFullScreen) != 0);
    // hands WM_DISPLAYCHANGE to the recipient walk with the
    // kind 1, and Windows 98 sends that kind without a reply
    // wait (27B6..27CC), the way SendNotifyMessage does: a window of the
    // calling thread gets the call at once, every other thread finds the
    // message in its queue. The caller does not wait for anybody. DDRAW sets
    // a mode with its critical section held, the one that is global to all
    // processes; a second DirectDraw program that waits for that section in
    // its own Lock or Blt cannot answer, and a blocking send hung both for
    // good.
    BroadcastMessage(Win98Broadcast::Notify, DisplayChange, unsigned(depth),
                     (unsigned(height) << 16) | (unsigned(width) & 65535));
    return ChangeSuccessful;
}

// USER, called once the new mode is in place. Every
// top-level window except a tool window is placed anew for the new monitor
// (win98_display_reposition.h): its saved normal rectangle first, when it is
// minimized or maximized, then its window
// rectangle. A maximized window is looked up without its frame (
// border units times SM_CXBORDER and SM_CYBORDER); one that covered the old
// work area follows the work area's edges. The changed ones move in one
// DeferWindowPos batch with SWP_NOZORDER|SWP_NOACTIVATE.
// Not built: the state bit 40h of byte +2Fh (mode 2, edges follow the
// monitor) is a mark of USER's own full-screen tracking that NEO98
// has no counterpart of, so every window starts in mode 0; the monitor
// handle at +54h does not exist here.
void Application::RepositionTopLevel(const Win98DisplayReposition::Rect &old_monitor,
                                     const Win98DisplayReposition::Rect &old_work, bool full_screen)
{
    namespace R = Win98DisplayReposition;
    const auto &settings = User::SystemSettings();
    const R::Rect new_monitor{settings.monitor[0], settings.monitor[1], settings.monitor[2], settings.monitor[3]};
    const R::Rect new_work{settings.work[0], settings.work[1], settings.work[2], settings.work[3]};
    // DS:374h and 376h, the size of the virtual screen, already the new one

    const int virtual_width = settings.monitor[2] - settings.monitor[0];
    const int virtual_height = settings.monitor[3] - settings.monitor[1];
    unsigned windows[User::WindowCapacity], count = 0;
    if (!User::WindowSnapshot(0, windows, User::WindowCapacity, count)) return;
    static unsigned positions[64][7];
    unsigned moves = 0;
    for (unsigned i = 0; i < count && moves < 64; ++i)
    {
        const unsigned hwnd = windows[i];
        if (!User::IsWindow(hwnd) || hwnd == User::GetDesktopWindow()) continue;
        const unsigned style = User::Style(hwnd), extended = User::ExtendedStyle(hwnd);
        if (extended & 0x80) continue; // WS_EX_TOOLWINDOW,
        unsigned mode = R::MoveScaled;
        Gdi::RECT saved = {};
        if ((style & 0x21000000) && User::NormalWindowRect(hwnd, &saved))
        {
            const R::Rect normal{saved.left, saved.top, saved.right, saved.bottom};
            if (R::MonitorOf(normal, &old_monitor, 1, virtual_width, virtual_height) != -1)
            {
                const R::Rect placed = R::Place(normal, old_monitor, old_work, new_monitor, new_work, full_screen, mode, style);
                User::SetNormalWindowRect(hwnd, Gdi::RECT{placed.left, placed.top, placed.right, placed.bottom});
            }
        }
        Gdi::RECT current = {};
        if (!User::GetWindowRect(hwnd, &current)) continue;
        const R::Rect window{current.left, current.top, current.right, current.bottom};
        R::Rect probe = window;
        if (style & 0x01000000) // WS_MAXIMIZE,
        {
            const int units = int(FrameBorderUnits(style, extended));
            const int cx = units * int(SystemMetric(5)), cy = units * int(SystemMetric(6));
            probe = {probe.left + cx, probe.top + cy, probe.right - cx, probe.bottom - cy};
        }
        if (R::MonitorOf(probe, &old_monitor, 1, virtual_width, virtual_height) == -1) continue;
        if ((style & 0x01000000) && R::W(old_work.right - old_work.left) <= R::W(window.right - window.left) &&
            R::W(old_work.bottom - old_work.top) <= R::W(window.bottom - window.top))
            mode = R::MoveWithWork;
        const R::Rect placed = R::Place(window, old_monitor, old_work, new_monitor, new_work, full_screen, mode, style);
        if (R::Equal(placed, window)) continue;
        unsigned *p = positions[moves++];
        p[0] = hwnd; p[1] = 0;
        p[2] = unsigned(placed.left); p[3] = unsigned(placed.top);
        p[4] = unsigned(placed.right - placed.left); p[5] = unsigned(placed.bottom - placed.top);
        p[6] = 0x14; // SWP_NOZORDER|SWP_NOACTIVATE
    }
    if (moves) PositionWindows(moves, positions);
}

// names a task by the process the window belongs to. NEO98 keeps one
// Application per process; a process that left has none or is marked Exited.
bool Application::TaskAlive(unsigned task)
{
    if (!task) return false;
    if (task == process_id) return state != Exited && state != Failed;
    auto *other = static_cast<Application *>(User::ProcessContextOf(task));
    return other && other->state != Exited && other->state != Failed;
}

// Windows 98 reads bit 8 of the queue flags, the mark USER sets on a task that is
// already on its way out.
bool Application::TaskEnding(unsigned task)
{
    if (!task) return false;
    if (task == process_id) return state == Exited;
    auto *other = static_cast<Application *>(User::ProcessContextOf(task));
    return other && other->state == Exited;
}

// hands the task to KERNEL's termination path. TerminateSelf marks
// its fibers done; their owning scheduler reaps them after this call returns.
bool Application::TerminateTask(unsigned task)
{
    if (!TaskAlive(task)) return true;
    auto *other = static_cast<Application *>(User::ProcessContextOf(task));
    if (!other) return false;
    other->TerminateSelf(0);
    return true;
}

unsigned Application::UserSessionApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    // USER is the whole of ExitWindowsEx: it turns the flags into the
    // pair (lParam of the two messages, ExitKernel code) and calls.
    // The second argument is reserved and the original never reads it.
    API_CASE("ExitWindowsEx")
    {
        unsigned flags = a[0], code = ShutdownCode;
        if (flags & ExitReboot)
            code = RebootCode;
        else if (!(flags & ExitShutdown))
            flags |= LogoffBit;
        return EndSession(flags, code);
    }
    // USER. EndTask has four arguments: the window, the task, a name
    // for the case without a window and the flags. The body is one straight
    // line: name the task, ask its window to close, wait for it, show the
    // confirmation and, if that says End Task, terminate the task.
    API_CASE("EndTask")
    {
        const bool recorded = TakeFresh();
        const unsigned hwnd = a[0], name = a[2], flags = a[3];
        unsigned task = a[1];
        // without a window and without a task there is nothing to end.
        if (!hwnd && !task) return TaskFailed;
        // and Windows 98 take the task out of the window's queue block.
        if (!task)
        {
            if (!User::IsWindow(hwnd)) return TaskFailed;
            task = User::WindowProcess(hwnd);
        }
        unsigned result = TaskEnded;
        (void)name;
        if (flags & (TaskCloseFirst | TaskNoUserInterface))
        {
            if ((flags & TaskCloseFirst) && hwnd)
            {
                if (!User::IsWindow(hwnd)) return TaskEnded;
                // Windows 98 walks to the end of the owned chain, so the message
                // reaches the dialog on top and not the frame behind it.
                unsigned target = hwnd;
                for (unsigned step = 0; step < User::WindowCapacity; ++step)
                {
                    const unsigned owned = User::OwnedWindow(target);
                    if (!owned || owned == target) break;
                    target = owned;
                }
                // a task that is already leaving answers 5.
                if (TaskEnding(task)) return TaskAlreadyEnding;
                const unsigned close[] = {target, 0x10, 0, 0};
                Api("PostMessageA", close);
                if (!(flags & TaskNoWait))
                {
                    // 100 ms once, then 200 ms steps, up to 20 s.
                    unsigned waited = 0;
                    while (waited < TaskWaitLimit)
                    {
                        const unsigned span = waited ? TaskWait : TaskFirstWait;
                        Api("Sleep", &span);
                        if (state == Failed || state == Exited) return TaskEnded;
                        if (!TaskAlive(task)) return TaskEnded;
                        waited += TaskWait;
                    }
                }
            }
            // the flag that forbids a user interface skips the box.
            if (!(flags & TaskNoUserInterface)) return ConfirmEndTask(hwnd, task, recorded);
        }
        else
            return ConfirmEndTask(hwnd, task, recorded);
        // only the End Task answer reaches the termination, and a
        // termination that fails is answered with 2.
        return FinishEndTask(result, task);
    }
    // USER. The export has no return value: the USER32 wrapper widens
    // nothing, and the 16-bit body only sets bits in the calling task's queue
    // block. skips the whole body when the reserved argument is not
    // zero. None of those bits has a native counterpart, so NEO98 remembers
    // the flags and nothing else.
    API_CASE("RegisterSystemThread")
    {
        if (!a[1]) system_thread_flags |= a[0] & 255;
        return 0;
    }
    // USER merges the caller's bits into USER's capability word and
    // refills the WNetGetCaps cache from it. NEO98 has no network provider, so
    // the word stays what the caller made of it and every WNet entry keeps
    // answering WN_NOT_SUPPORTED, which is what a zero word produces. Only the low sixteen bits of either argument are read.
    API_CASE("RegisterNetworkCapabilities")
    {
        const unsigned mask = a[0] & 65535, value = a[1] & 65535;
        network_capabilities = (value & mask) | (network_capabilities & ~mask);
        return 0;
    }
    // USER over. The mode list of the original comes from the
    // display driver through the VDD and from the registry. The driver NEO98
    // plays is the compositor's screen surface, so its list is the one a
    // linear frame buffer offers: 640 by 480 and 800 by 600 in 8, 16, 24 and
    // 32 bits (src/win98_display_modes.h). The walk ends after the last entry,
    // which returns FALSE.
    // USER32 (EnumDisplaySettingsExA, thunk index 19h) is the same
    // wrapper as (index 1Ah) with dwFlags pushed as a fourth word.
    // The only flag Win98 knows, EDS_RAWMODE, lifts the monitor filter, and
    // NEO98's list has none, so both forms answer alike. DESK.CPL asks this
    // form for the current mode of its Settings page (desk.cpl+0x123E1).
    API_CASE("EnumDisplaySettingsExA")
    API_CASE("EnumDisplaySettingsA")
    {
        const unsigned index = a[1];
        if (!a[2]) { last_error = 87; return 0; }
        int width = 0, height = 0, depth = 0;
        if (index == CurrentSettings) User::ScreenMode(width, height, depth);
        else if (index == RegistrySettings) RegistryDisplayMode(width, height, depth);
        else if (!User::ScreenModeAt(index, width, height, depth)) return 0;
        unsigned char mode[DevModeSize] = {};
        auto put16 = [&](unsigned at, unsigned v) { mode[at] = v & 255; mode[at + 1] = (v >> 8) & 255; };
        auto put32 = [&](unsigned at, unsigned v) {
            for (unsigned i = 0; i < 4; ++i) mode[at + i] = (v >> (8 * i)) & 255;
        };
        put16(36, DevModeSize);                       // dmSize,
        put32(40, 0x001c0020);                        // dmFields,
        put16(102, 96);                               // dmLogPixels
        put32(104, unsigned(depth));
        put32(108, unsigned(width));
        put32(112, unsigned(height));
        return Store(a[2], mode, DevModeSize) ? 1u : 0u;
    }
    // USER over.
    API_CASE("ChangeDisplaySettingsExA")
    {
        // any bit outside the accepted mask is DISP_CHANGE_BADFLAGS.
        if (a[3] & 0x0fffffc0) return ChangeBadFlags;
        // this export takes no window.
        if (a[2]) return ChangeBadParam;
        // A null mode means "go back to the registry setting".
        return ChangeDisplaySettings(a[1], a[3]);
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}
} // namespace X86
