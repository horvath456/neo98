// SPDX-License-Identifier: GPL-2.0-or-later
// Fills a CrashReport::Report from a running (or stopped) application tree.
// Everything here only reads: guest memory is taken with uc_mem_read instead of
// Application::Read, because the latter stops the process on a bad address and
// a diagnostic must not change what it reports. The formatting and the file
// output live elsewhere (src/crash_report.cpp, tools/crash_report_files.h).
#include "crash_unwind.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>

namespace X86
{
void *Application::crash_context = nullptr;
Application::CrashHook Application::crash_hook = nullptr;
bool Application::crash_hook_busy = false;

void Application::SetCrashHook(void *context, CrashHook hook)
{
    crash_context = context;
    crash_hook = hook;
}
void Application::NotifyCrash(const char *reason)
{
    if (!crash_hook || crash_hook_busy)
        return;
    // One report per stop. A report writer that itself runs into a Fail must
    // not start a second report on top of the first one.
    crash_hook_busy = true;
    crash_hook(crash_context, *this, reason);
    crash_hook_busy = false;
}

namespace
{
using CrashReport::Address;
using CrashReport::ModuleInfo;
using CrashReport::ProcessInfo;
using CrashReport::Report;
using CrashReport::ThreadInfo;

void Copy(char *out, unsigned capacity, const char *text)
{
    if (!capacity) return;
    unsigned i = 0;
    for (; text && text[i] && i + 1 < capacity; ++i) out[i] = text[i];
    out[i] = 0;
}
// The scheduler's block codes, as src/x86_threads.cpp::Runnable reads them.
const char *BlockName(unsigned block)
{
    switch (block)
    {
    case 0: return "runnable";
    case 1: return "message-wait";
    case 2: return "object-wait";
    case 3: return "sleep";
    case 4: return "loader-lock";
    case 5: return "critical-section";
    case 6: return "thread-control";
    case 7: return "multiwait";
    case 8: return "send-message-reply";
    case 9: return "console-read";
    case 10: return "pipe-read";
    case 11: return "pipe-write";
    case 12: return "mailslot-read";
    case 14: return "serial-read";
    case 15: return "comm-event";
    case 16: return "input-idle-wait";
    case 17: return "screen-wait";
    case 18: return "screen-relock";
    default: return "unknown";
    }
}
const char *HandleKind(unsigned kind)
{
    static const char *const kinds[] = {"free",   "event",   "thread",
                                        "mutex",  "semaphore", "mapping",
                                        "process", "process-thread", "change-notification",
                                        "foreign process"};
    return kind < 10 ? kinds[kind] : "?";
}
} // namespace

// Names an address as module+offset, with the nearest export where the module
// is an original PE that still has its file bytes.
struct Application::CrashNaming
{
    Application &app;
    void Describe(unsigned value, Address &out, bool want_symbol) const
    {
        out.value = value;
        out.code = 0;
        if (!value)
        {
            Copy(out.text, sizeof out.text, "-");
            return;
        }
        if (value >= GateBase && value < GateBase + app.import_count * 16)
        {
            const unsigned index = (value - GateBase) / 16;
            snprintf(out.text, sizeof out.text, "API-Gate %s!%s", app.imports[index].dll,
                     app.imports[index].name);
            out.code = 1;
            return;
        }
        if (app.trivial_code_mapped && value - X86::Application::TrivialCodeBase - 0x200 < 3 * 0x40)
        {
            const char *names[] = {"GetLastError", "SetLastError", "TlsGetValue"};
            const unsigned offset = value - X86::Application::TrivialCodeBase - 0x200;
            snprintf(out.text, sizeof out.text, "API-Body KERNEL32.DLL!%s+0x%x", names[offset / 0x40], offset % 0x40);
            out.code = 1;
            return;
        }
        if (app.image.Base() && value >= app.image.Base() &&
            value - app.image.Base() < app.image.SizeOfImage())
        {
            const unsigned offset = value - app.image.Base();
            char symbol[64] = {};
            unsigned distance = 0;
            if (want_symbol && app.image.NearestExport(value, symbol, sizeof symbol, distance))
                snprintf(out.text, sizeof out.text, "%s+0x%x (%s+0x%x)", app.program_name, offset,
                         symbol, distance);
            else
                snprintf(out.text, sizeof out.text, "%s+0x%x", app.program_name, offset);
            out.code = 1;
            return;
        }
        for (const auto &m : app.modules)
        {
            if (!m.kind || !m.pe || !m.pe->Base()) continue;
            if (value < m.pe->Base() || value - m.pe->Base() >= m.pe->SizeOfImage()) continue;
            const unsigned offset = value - m.pe->Base();
            const char *slash = strrchr(m.path, '\\');
            const char *leaf = slash ? slash + 1 : m.path;
            char symbol[64] = {};
            unsigned distance = 0;
            if (want_symbol && m.pe->NearestExport(value, symbol, sizeof symbol, distance))
                snprintf(out.text, sizeof out.text, "%s+0x%x (%s+0x%x)", leaf, offset, symbol,
                         distance);
            else
                snprintf(out.text, sizeof out.text, "%s+0x%x", leaf, offset);
            out.code = 1;
            return;
        }
        // Not code we can name: stack, heap or a native provider's handle.
        bool stack = value >= 0x20000000 && value < 0x24000000;
        for (unsigned i = X86::Application::FixedStackSlots; !stack && i < X86::Application::ThreadCapacity; ++i)
            stack = app.threads[i].stack_size && value - app.threads[i].stack_base < app.threads[i].stack_size;
        if (stack)
            snprintf(out.text, sizeof out.text, "guest stack");
        else if (value >= X86::Application::SystemPageBase && value < X86::Application::ThreadTebBase + X86::Application::ThreadCapacity * 4096)
            snprintf(out.text, sizeof out.text, "TEB");
        else
            Copy(out.text, sizeof out.text, "-");
    }
};

// The same naming outside a crash report: the diagnosis of a call into an
// export this port does not implement says where the guest called from.
void Application::DescribeCodeAddress(unsigned value, char *out, unsigned capacity)
{
    if (!out || !capacity)
        return;
    CrashReport::Address address;
    CrashNaming{*this}.Describe(value, address, true);
    snprintf(out, capacity, "%s", address.text);
}
void Application::CollectCrashReport(CrashReport::Report &report)
{
    report.process_count = 0;
    report.processes_omitted = 0;
    CollectCrashProcess(report, false);
    for (unsigned slot = 0; slot < ChildCapacity; ++slot)
    {
        bool done = false;
        if (auto *child = ChildApplication(slot, done); child && !done)
        {
            if (report.process_count < Report::ProcessCapacity) child->CollectCrashProcess(report, true);
            else ++report.processes_omitted;
        }
    }
    // Windows in Z order, then the global focus/capture state.
    unsigned nodes[Report::WindowCapacity], count = 0;
    if (User::WindowSnapshot(User::GetDesktopWindow(), nodes, Report::WindowCapacity, count))
        for (unsigned i = 0; i < count && report.window_count < Report::WindowCapacity; ++i)
        {
            auto &w = report.windows[report.window_count++];
            w.hwnd = nodes[i];
            w.parent = User::Parent(nodes[i]);
            w.owner = User::Owner(nodes[i]);
            w.style = User::Style(nodes[i]);
            w.extended = User::ExtendedStyle(nodes[i]);
            w.process = User::WindowProcess(nodes[i]);
            w.thread = User::WindowThread(nodes[i]);
            w.visible = User::IsVisible(nodes[i]) ? 1 : 0;
            w.enabled = User::IsEnabled(nodes[i]) ? 1 : 0;
            Gdi::RECT bounds{};
            if (User::GetWindowRect(nodes[i], &bounds))
            {
                w.left = bounds.left;
                w.top = bounds.top;
                w.right = bounds.right;
                w.bottom = bounds.bottom;
            }
            User::GetClassName(nodes[i], w.klass, sizeof w.klass);
            User::GetWindowText(nodes[i], w.text, sizeof w.text);
        }
    report.focus = User::GetFocus();
    report.active = User::ActivationQueue().active;
    report.foreground = User::GetForegroundWindow();
    report.capture = User::CaptureOperation(0);
    report.live_windows = User::LiveWindows();
    report.gdi_objects = Gdi::LiveObjects();
    ChooseCrashSuspect(report);
}

void Application::CollectCrashProcess(CrashReport::Report &report, bool child)
{
    if (report.process_count >= Report::ProcessCapacity)
    {
        ++report.processes_omitted;
        return;
    }
    auto &out = report.processes[report.process_count++];
    const CrashNaming naming{*this};
    out.id = process_id;
    out.child = child ? 1 : 0;
    out.state = (unsigned)state;
    static const char *const states[] = {"running", "waiting", "exited", "failed"};
    Copy(out.state_text, sizeof out.state_text, out.state < 4 ? states[out.state] : "?");
    Copy(out.name, sizeof out.name, program_name);
    Copy(out.error, sizeof out.error, error);
    out.api_calls = api_calls;
    out.callbacks = callbacks;
    out.exit_code = exit_code;
    out.depth = depth;
    out.current_thread = current_thread;
    out.image_base = image.Base();
    out.image_size = image.SizeOfImage();
    out.import_count = import_count;
    out.live_threads = LiveThreads();
    out.ring = &api_ring;
    // The command line the process itself sees, read without touching state.
    if (command_line)
    {
        char text[512] = {};
        for (unsigned i = 0; i + 1 < sizeof text; ++i)
        {
            if (!cpu || cpu->Read(command_line + i, text + i, 1) != CpuError::Ok) break;
            if (!text[i]) break;
        }
        text[sizeof text - 1] = 0;
        Copy(out.command, sizeof out.command, text);
    }
    for (unsigned i = 0; i < ModuleCapacity && out.module_count < CrashReport::ProcessInfo::ModuleCapacity; ++i)
    {
        const auto &m = modules[i];
        if (!m.kind) continue;
        auto &mod = out.modules[out.module_count++];
        Copy(mod.path, sizeof mod.path, m.path);
        mod.handle = m.handle;
        // Kind 1 is the process image, 3 an original PE under Unicorn; only
        // kind 2 is a native provider with no guest code behind it.
        mod.original = m.kind == 2 ? 0 : 1;
        mod.base = m.pe ? m.pe->Base() : m.handle;
        mod.size = m.pe ? m.pe->SizeOfImage() : 0;
    }
    for (unsigned i = 0; i < ThreadCapacity && out.thread_count < CrashReport::ProcessInfo::ThreadCapacity; ++i)
    {
        if (!threads[i].fiber) continue;
        CollectCrashThread(i, out.threads[out.thread_count++], naming);
    }
}

void Application::CollectCrashThread(unsigned index, CrashReport::ThreadInfo &out,
                                     const CrashNaming &naming)
{
    const auto &t = threads[index];
    out.index = index;
    out.id = t.id;
    out.block = t.block;
    out.wait_handle = t.wait_handle;
    out.depth = t.depth;
    out.last_error = t.last_error;
    out.pending = t.pending;
    out.deadline = t.deadline;
    out.done = t.done ? 1 : 0;
    out.suspended = t.suspended ? 1 : 0;
    out.waiting = t.waiting ? 1 : 0;
    out.alertable = t.alertable ? 1 : 0;
    out.apc_count = t.apc_count;
    out.current = index == current_thread ? 1 : 0;
    out.message_window = t.message_window;
    out.message_minimum = t.message_minimum;
    out.message_maximum = t.message_maximum;
    out.multiwait_count = t.multiwait.count;
    for (unsigned n = 0; n < 8 && n < t.multiwait.count; ++n) out.multiwait[n] = t.multiwait.objects[n];
    out.runnable = Runnable(index) ? 1 : 0;
    Copy(out.block_text, sizeof out.block_text, BlockName(t.block));
    // Registers: live for the thread that ran last, from its saved context for
    // every other one. Both paths use the same register numbers.
    static const int numbers[] = {Reg::Eip, Reg::Esp, Reg::Ebp, Reg::Eax,
                                  Reg::Ebx, Reg::Ecx, Reg::Edx, Reg::Esi,
                                  Reg::Edi, Reg::Eflags, Reg::Cs, Reg::Ds,
                                  Reg::Es, Reg::Ss, Reg::Fs, Reg::Gs};
    unsigned values[16] = {};
    for (unsigned n = 0; n < 16; ++n)
    {
        // A process without a processor has no registers; they stay zero.
        if (!HasProcessor()) break;
        if (index == current_thread) cpu->Get(numbers[n], values[n]);
        else if (t.context) cpu->ContextGet(t.context, numbers[n], values[n]);
    }
    out.eip = values[0]; out.esp = values[1]; out.ebp = values[2]; out.eax = values[3];
    out.ebx = values[4]; out.ecx = values[5]; out.edx = values[6]; out.esi = values[7];
    out.edi = values[8]; out.eflags = values[9]; out.cs = values[10]; out.ds = values[11];
    out.es = values[12]; out.ss = values[13]; out.fs = values[14]; out.gs = values[15];
    // The FS selector is the thread's GDT entry; its base is the TEB page.
    out.teb = TebOf(index);
    naming.Describe(out.eip, out.eip_address, true);
    if (out.eip >= GateBase && out.eip < GateBase + import_count * 16 &&
        !((out.eip - GateBase) & 15))
    {
        const auto &item = imports[(out.eip - GateBase) / 16];
        out.gate.value = out.eip;
        snprintf(out.gate.text, sizeof out.gate.text, "%s!%s", item.dll, item.name);
    }
    DescribeCrashWait(index, out);
    {
        User::Message probe = {};
        struct Binding{unsigned p=User::CurrentProcess(),q=User::CurrentThread();
                       ~Binding(){User::BindTask(p,q);}} binding;
        User::BindTask(process_id, t.id);
        out.queue_pending = User::PeekMessage(&probe, 0, 0, 0, false) ? 1 : 0;
    }
    // 64 stack dwords with the module each value points into, and the subset
    // that a call instruction in front of it makes a return address, as the
    // call chain.
    for (unsigned n = 0; n < 64 && out.esp; ++n)
    {
        unsigned value = 0;
        if (!cpu || cpu->Read(out.esp + n * 4, &value, 4) != CpuError::Ok) break;
        auto &slot = out.stack[out.stack_count++];
        naming.Describe(value, slot, false);
        if (!slot.code || out.return_count >= 12 || value == out.eip) continue;
        // A word in code is only a return address when the instruction before
        // it is a call. Data and function pointers that happen to lie on the
        // stack made up half of the chain before this check.
        unsigned char before[7] = {};
        unsigned available = 0;
        for (unsigned bytes = 7; bytes >= 2; --bytes)
            if (value >= bytes &&
                cpu->Read(value - bytes, before + (7 - bytes), bytes) == CpuError::Ok)
            {
                available = bytes;
                break;
            }
        const unsigned length = available ? CrashUnwind::CallLength(before, available) : 0;
        // Seven readable bytes cover every call form, so a negative answer is
        // final there. Fewer bytes (a page boundary in front of the address)
        // leave the link undecided; it stays in the list and says so.
        if (!length && available >= 7) continue;
        auto &entry = out.returns[out.return_count++];
        naming.Describe(value, entry, true);
        entry.certain = length ? 1 : 0;
    }
}

// What a blocked thread waits for, including the state of the object, so a
// report says which provider would have to signal it.
void Application::DescribeCrashWait(unsigned index, CrashReport::ThreadInfo &out)
{
    const auto &t = threads[index];
    char text[256];
    auto handle_text = [&](unsigned handle, char *buffer, unsigned capacity) {
        auto *entry = FindKernelHandle(handle);
        if (!entry)
        {
            snprintf(buffer, capacity, "Handle %08x (invalid)", handle);
            return;
        }
        const char *signal = "?";
        if (entry->kind == 1) signal = events[entry->object].signaled ? "signaled" : "not signaled";
        else if (entry->kind == 2) signal = threads[entry->object].done ? "exited" : "running";
        else if (entry->kind == 3) signal = kernel_mutexes[entry->object].owner ? "held" : "free";
        else if (entry->kind == 8) signal = change_notifications[entry->object].signaled ? "signaled" : "not signaled";
        snprintf(buffer, capacity, "Handle %08x, %s %u, %s", handle, HandleKind(entry->kind),
                 entry->object, signal);
    };
    // Multiwait stores resolved objects as (kind << 16) | (index + 1).
    // The original handle may already be closed; do not decode an object
    // key as a handle when describing the wait.
    auto object_text = [&](unsigned key, char *buffer, unsigned capacity) {
        const unsigned kind = key >> 16, object = (key & 0xffff) - 1;
        const unsigned limit = kind == 2 ? Application::ThreadCapacity : kind == 8 ? 16u : Application::KernelObjectSlots;
        if (!(key & 0xffff) || object >= limit)
        {
            snprintf(buffer, capacity, "Object %08x (invalid)", key);
            return;
        }
        const char *signal = "?";
        if (kind == 1) signal = events[object].signaled ? "signaled" : "not signaled";
        else if (kind == 2) signal = threads[object].done ? "exited" : "running";
        else if (kind == 3) signal = kernel_mutexes[object].owner ? "held" : "free";
        else if (kind == 4) signal = Win98Semaphore::CanAcquire(semaphores[object].data) ? "free" : "full";
        else if (kind == 8) signal = change_notifications[object].signaled ? "signaled" : "not signaled";
        else if (kind == 6 || kind == 7) signal = ChildSignaled(object, kind == 7) ? "exited" : "running";
        else if (kind == 9) signal = ForeignProcessSignaled(object) ? "exited" : "running";
        const char *name = "";
        if (kind == 1 && events[object].named) name = events[object].name;
        else if (kind == 3 && kernel_mutexes[object].named) name = kernel_mutexes[object].name;
        else if (kind == 4 && semaphores[object].named) name = semaphores[object].name;
        snprintf(buffer, capacity, "%s %u%s%s, %s", HandleKind(kind), object,
                 *name ? " " : "", name, signal);
    };
    switch (t.block)
    {
    case 0:
        Copy(out.wait_text, sizeof out.wait_text,
             t.done ? "nothing, the thread has exited"
                    : "nothing, the thread is runnable and should execute code");
        return;
    case 1:
        snprintf(text, sizeof text,
                 "Message for window %08x, range %08x..%08x; queue %s",
                 t.message_window, t.message_minimum, t.message_maximum,
                 out.queue_pending ? "has a message" : "is empty");
        break;
    case 2:
    {
        char detail[160];
        handle_text(t.wait_handle, detail, sizeof detail);
        snprintf(text, sizeof text, "WaitForSingleObject on %s", detail);
        break;
    }
    case 3:
        snprintf(text, sizeof text, "Sleep or wait deadline at %llu ms", t.deadline);
        break;
    case 16:
        snprintf(text, sizeof text, "WaitForInputIdle on a child, deadline at %llu ms", t.deadline);
        break;
    case 4:
        snprintf(text, sizeof text, "Loader lock held by thread %u", loader_owner);
        break;
    case 5:
    {
        // Which thread holds it and how many wait behind it is what a
        // deadlock report needs; the bare address says nothing.
        const auto *lock = FindCritical(t.wait_handle);
        if (lock)
            snprintf(text, sizeof text,
                     "Critical section %08x, owner thread %u, recursion %u, %u waiting",
                     t.wait_handle, lock->data.owner, lock->data.recursion, lock->waiting);
        else
            snprintf(text, sizeof text, "Critical section %08x no longer exists", t.wait_handle);
        break;
    }
    case 6:
        Copy(text, sizeof text, "Thread control stop after suspension or preemption");
        break;
    case 7:
    {
        unsigned n = (unsigned)snprintf(text, sizeof text,
                                        "MsgWaitForMultipleObjects on %u objects%s%s",
                                        t.multiwait.count, t.multiwait.all ? ", all" : ", any",
                                        t.multiwait.message ? ", with message mask" : "");
        for (unsigned i = 0; i < t.multiwait.count && i < 4 && n + 2 < sizeof text; ++i)
        {
            char detail[160];
            object_text(t.multiwait.objects[i], detail, sizeof detail);
            n += (unsigned)snprintf(text + n, sizeof text - n, "; %s", detail);
        }
        break;
    }
    case 8:
        SentWaitText(index, text, sizeof text);
        break;
    case 9:
        Copy(text, sizeof text, "Console input");
        break;
    case 10:
    case 11:
        snprintf(text, sizeof text, "Pipe %08x, %s", t.wait_handle,
                 t.block == 11 ? "write" : "read");
        break;
    case 12:
        snprintf(text, sizeof text, "Mailslot %08x", t.wait_handle);
        break;
    case 14:
    case 15:
        snprintf(text, sizeof text, "Serial port %08x", t.wait_handle);
        break;
    default:
        snprintf(text, sizeof text, "Block code %u has no description", t.block);
        break;
    }
    if (t.deadline && t.block != 3)
    {
        const unsigned n = (unsigned)strlen(text);
        snprintf(text + n, sizeof text - n, "; deadline %llu ms", t.deadline);
    }
    Copy(out.wait_text, sizeof out.wait_text, text);
}

// The thread the report points at first. A stopped process wins; after that a
// runnable thread that dispatches nothing, then any blocked thread.
void Application::ChooseCrashSuspect(CrashReport::Report &report)
{
    report.suspect_process = 0xffffffffu;
    report.suspect_thread = 0xffffffffu;
    for (unsigned p = 0; p < report.process_count; ++p)
    {
        const auto &proc = report.processes[p];
        if (proc.state != Failed) continue;
        for (unsigned t = 0; t < proc.thread_count; ++t)
            if (proc.threads[t].index == proc.current_thread)
            {
                report.suspect_process = p;
                report.suspect_thread = t;
                snprintf(report.suspect_text, sizeof report.suspect_text,
                         "The process stopped in Fail(\"%s\"). This is the thread that last ran.",
                         proc.error);
                return;
            }
    }
    for (unsigned p = 0; p < report.process_count; ++p)
    {
        const auto &proc = report.processes[p];
        for (unsigned t = 0; t < proc.thread_count; ++t)
        {
            const auto &th = proc.threads[t];
            if (th.done || th.suspended || !th.runnable || th.block) continue;
            report.suspect_process = p;
            report.suspect_thread = t;
            snprintf(report.suspect_text, sizeof report.suspect_text,
                     "Thread %u is runnable and not blocked. It receives time slices "
                     "but makes no API calls, suggesting a loop "
                     "at %s.",
                     th.id, th.eip_address.text);
            return;
        }
    }
    for (unsigned p = 0; p < report.process_count; ++p)
    {
        const auto &proc = report.processes[p];
        for (unsigned t = 0; t < proc.thread_count; ++t)
        {
            const auto &th = proc.threads[t];
            if (th.done || th.block == 1) continue;
            report.suspect_process = p;
            report.suspect_thread = t;
            snprintf(report.suspect_text, sizeof report.suspect_text,
                     "No captured thread is executing. Thread %u is waiting at the longest visible wait: %s.%s",
                     th.id, th.wait_text,
                     report.processes_omitted ? " Some processes are missing from this report; the "
                                                "recipient may be among them." : "");
            return;
        }
    }
    if (report.process_count && report.processes[0].thread_count)
    {
        report.suspect_process = 0;
        report.suspect_thread = 0;
        Copy(report.suspect_text, sizeof report.suspect_text,
             "All threads are waiting for input normally. The report shows the main thread.");
    }
}
} // namespace X86
