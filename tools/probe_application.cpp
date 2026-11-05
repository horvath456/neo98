// SPDX-License-Identifier: GPL-2.0-or-later
// Exploratory runner. Read-only host files are copied into the native RAM volume.
#define private public
#include "../src/x86_app.h"
#include "../src/sound.h"
#undef private
#include "crash_report_files.h"
#include "host_volume.h"
#include "registry_file.h"
#include "system_tree_host.h"
#include "host_gpu.h"
#include "printer_port_host.h"
#include <algorithm>
#include "host_net.h"
#include "../src/win98_wininit.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <strings.h>
#include <thread>
#include <vector>
static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return {};
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    if (n <= 0 || (unsigned long long)n > 0xffffffffull)
    {
        fclose(f);
        return {};
    }
    std::vector<unsigned char> b(n);
    if (fread(b.data(), 1, b.size(), f) != b.size())
        b.clear();
    fclose(f);
    return b;
}
static X86::Application *observed_app;
static PrinterPortHost::Directory *printer_sink = nullptr;
static unsigned printer_jobs = 0;
static void hex_text(const char *text);
// Crash reports are off unless --crash-report-dir is given: the probe is what
// the test runners use, and a test that expects a Fail must not fill the
// report directory.
static const char *crash_report_dir = nullptr;
static int probe_argc = 0;
static char **probe_argv = nullptr;
static std::chrono::steady_clock::time_point probe_start;
static void write_crash_report(X86::Application &app, const char *reason)
{
    if (!crash_report_dir) return;
    auto *report = new CrashReport::Report();
    CrashFiles::Describe(*report, reason, "probe_application", probe_argc, probe_argv);
    report->uptime_ms = (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - probe_start).count();
    app.CollectCrashReport(*report);
    char path[1024] = {};
    CrashFiles::Write(*report, crash_report_dir, User::Pixels(), 800, 600, path, sizeof path);
    delete report;
}
static void on_crash(void *, X86::Application &app, const char *reason)
{
    write_crash_report(app, reason);
}
// Diagnostic instruction-level sampling. A Unicorn basic-block hook counts the
// blocks each guest thread starts and records every stride-th block address.
// Nothing here changes guest state or scheduling; it only observes where the
// CPU spends its slices while Pump keeps reporting Running.
namespace sampling {
static unsigned thread_eip(X86::Application &app, unsigned i);
static void describe_handle(X86::Application &app, unsigned thread, unsigned handle);
static void describe_wait_object(X86::Application &app, unsigned thread, unsigned key);
static bool enabled = false;
static unsigned stride = 1024;
static unsigned long long blocks = 0;
static unsigned long long samples = 0;
static unsigned long long thread_blocks[16] = {};
struct Slot { unsigned long long key = 0; unsigned long long count = 0; };
static const unsigned kSlots = 1u << 16;
static Slot *slots = nullptr;
static unsigned used = 0;
static unsigned long long overflow = 0;
static unsigned restarted_at = 0;
static bool restarted = false;
static void record(unsigned thread, unsigned address)
{
    if (!slots)
        return;
    const unsigned long long key = ((unsigned long long)(thread + 1) << 32) | address;
    unsigned i = (unsigned)((key * 0x9e3779b97f4a7c15ull) >> 48) & (kSlots - 1);
    for (unsigned n = 0; n < kSlots; ++n, i = (i + 1) & (kSlots - 1))
    {
        if (slots[i].key == key) { ++slots[i].count; return; }
        if (!slots[i].key)
        {
            if (used * 4 >= kSlots * 3) { ++overflow; return; }
            slots[i].key = key; slots[i].count = 1; ++used; return;
        }
    }
    ++overflow;
}
static void block_hook(X86::Cpu &, unsigned address, unsigned, void *)
{
    if (!observed_app)
        return;
    const unsigned thread = observed_app->current_thread < 16 ? observed_app->current_thread : 15;
    ++blocks;
    ++thread_blocks[thread];
    if (blocks % stride)
        return;
    ++samples;
    record(thread, (unsigned)address);
}
// The interesting phase is the one after the API call count stops growing.
// Clearing the table once at that point keeps the histogram free of the whole
// startup and leaves only where the guest runs while nothing is dispatched.
static void restart(unsigned calls)
{
    if (!enabled || !slots || restarted)
        return;
    memset(slots, 0, (size_t)kSlots * sizeof(Slot));
    used = 0; overflow = 0; samples = 0; restarted = true; restarted_at = calls;
    for (auto &n : thread_blocks) n = 0;
}
static bool install(X86::Application &app)
{
    if (!enabled || !app.cpu)
        return false;
    slots = (Slot *)calloc(kSlots, sizeof(Slot));
    if (!slots)
        return false;
    return app.cpu->AddTrace(X86::TraceKind::Block, 1, 0, block_hook, nullptr) == X86::CpuError::Ok;
}
// One line per second while the run makes no progress: which thread the
// scheduler still considers runnable, where it stands and what it waits for.
static void stall_dump(X86::Application &app)
{
    printf("PROBE_SAMPLE_STALL state=%u calls=%u callbacks=%u current=%u depth=%u children=%u blocks=%llu\n",
           (unsigned)app.Status(), app.ApiCalls(), app.Callbacks(), app.current_thread, app.depth,
           X86::Application::LiveChildren(), blocks);
    for (unsigned i = 0; i < 16; ++i)
    {
        const auto &t = app.threads[i];
        if (!t.fiber)
            continue;
        printf("PROBE_SAMPLE_STALL_THREAD index=%u id=%u block=%u wait=%08x done=%u suspended=%u "
               "finished=%u waiting=%u pending=%08x depth=%u eip=%08x runnable=%u\n",
               i, t.id, t.block, t.wait_handle, t.done ? 1u : 0u, t.suspended,
               t.fiber->Finished() ? 1u : 0u, t.waiting ? 1u : 0u, t.pending, t.depth,
               thread_eip(app, i), app.Runnable(i) ? 1u : 0u);
        {
            const unsigned eip = thread_eip(app, i);
            if (eip >= X86::Application::GateBase && eip < X86::Application::GateBase + app.import_count * 16 && !((eip - X86::Application::GateBase) & 15))
                printf("PROBE_SAMPLE_STALL_GATE index=%u api=%s!%s\n", i, app.imports[(eip - X86::Application::GateBase) / 16].dll,
                       app.imports[(eip - X86::Application::GateBase) / 16].name);
        }
        // A thread that waits for the reply to a sent message: which window,
        // which message and which receiving thread.
        if (t.block == 8)
        {
            char text[256];
            app.SentWaitText(i, text, sizeof text);
            printf("PROBE_SAMPLE_SEND thread=%u %s\n", i, text);
        }
        if (t.block == 8 && t.send_window)
            if (auto *other = static_cast<X86::Application *>(User::ProcessContextOf(User::WindowProcess(t.send_window))); other && other != &app)
                for (unsigned n = 0; n < 16; ++n)
                {
                    const auto &u = other->threads[n];
                    if (!u.fiber) continue;
                    printf("PROBE_SAMPLE_SEND_RECEIVER process=%u thread=%u id=%u block=%u wait=%08x done=%u eip=%08x runnable=%u\n",
                           other->ProcessId(), n, u.id, u.block, u.wait_handle, u.done ? 1u : 0u, thread_eip(*other, n),
                           other->Runnable(n) ? 1u : 0u);
                    {
                        const unsigned eip = thread_eip(*other, n);
                        if (eip >= X86::Application::GateBase && eip < X86::Application::GateBase + other->import_count * 16 &&
                            !((eip - X86::Application::GateBase) & 15))
                            printf("PROBE_SAMPLE_SEND_RECEIVER_GATE thread=%u api=%s!%s\n", n,
                                   other->imports[(eip - X86::Application::GateBase) / 16].dll,
                                   other->imports[(eip - X86::Application::GateBase) / 16].name);
                    }
                    if (u.wait_handle) describe_handle(*other, n, u.wait_handle);
                }
        if (t.wait_handle) describe_handle(app, i, t.wait_handle);
        for (unsigned n = 0; n < t.multiwait.count && n < 64; ++n)
            describe_wait_object(app, i, t.multiwait.objects[n]);
    }
    fflush(stdout);
}
static unsigned thread_eip(X86::Application &app, unsigned i)
{
    unsigned eip = 0;
    if (i == app.current_thread && app.cpu) { app.cpu->Get(X86::Reg::Eip, eip); return eip; }
    if (app.threads[i].context) app.cpu->ContextGet(app.threads[i].context, X86::Reg::Eip, eip);
    return eip;
}
// Names the object a blocked thread waits for, so a stalled run says which
// provider would have to signal it.
static void describe_handle(X86::Application &app, unsigned thread, unsigned handle)
{
    static const char *const kinds[] = {"free", "event", "thread", "mutex", "semaphore", "mapping",
                                        "process", "process-thread", "change-notification", "?"};
    auto *entry = app.FindKernelHandle(handle);
    if (!entry) { printf("PROBE_SAMPLE_WAIT thread=%u handle=%08x kind=invalid object=0 state=?\n", thread, handle); return; }
    const char *state = "?";
    if (entry->kind == 1) state = app.events[entry->object].signaled ? "signaled" : "clear";
    else if (entry->kind == 8) state = app.change_notifications[entry->object].signaled ? "signaled" : "clear";
    else if (entry->kind == 2) state = app.threads[entry->object].done ? "done" : "running";
    // The name of a named object, because a wait on one of them says which
    // contract of the session the thread is stuck in.
    const char *name = "";
    if (entry->kind == 1 && app.events[entry->object].named) name = app.events[entry->object].name;
    else if (entry->kind == 3 && app.kernel_mutexes[entry->object].named) name = app.kernel_mutexes[entry->object].name;
    else if (entry->kind == 4 && app.semaphores[entry->object].named) name = app.semaphores[entry->object].name;
    printf("PROBE_SAMPLE_WAIT thread=%u handle=%08x kind=%s object=%u state=%s name=%s\n", thread, handle,
           kinds[entry->kind < 9 ? entry->kind : 9], entry->object, state, name);
}
// A thread in a multiwait keeps resolved objects, not handles: the entry of
// Application::Wait is (kind << 16) | (object + 1), and the handle it came
// from may already be closed. Reading it as a handle named every such wait
// "invalid" and hid which object a stalled run was waiting for.
static void describe_wait_object(X86::Application &app, unsigned thread, unsigned key)
{
    static const char *const kinds[] = {"free", "event", "thread", "mutex", "semaphore", "mapping",
                                        "process", "process-thread", "change-notification", "foreign-process", "?"};
    const unsigned kind = key >> 16, object = (key & 0xffff) - 1;
    // Threads and change notifications have 16 slots, the session-wide kernel
    // objects 256; a key outside them is not one of ours.
    const unsigned limit = (kind == 2 || kind == 8) ? 16u : 256u;
    if (!(key & 0xffff) || object >= limit)
    {
        printf("PROBE_SAMPLE_WAIT thread=%u object=%08x kind=invalid index=0 state=?\n", thread, key);
        return;
    }
    const char *state = "?", *name = "";
    if (kind == 1) {
        state = app.events[object].signaled ? "signaled" : "clear";
        if (app.events[object].named) name = app.events[object].name;
    }
    else if (kind == 2) state = app.threads[object].done ? "done" : "running";
    else if (kind == 3) { state = app.kernel_mutexes[object].owner ? "owned" : "free";
                          if (app.kernel_mutexes[object].named) name = app.kernel_mutexes[object].name; }
    else if (kind == 4) { if (app.semaphores[object].named) name = app.semaphores[object].name; }
    else if (kind == 8) state = app.change_notifications[object].signaled ? "signaled" : "clear";
    else if (kind == 6 || kind == 7) state = X86::Application::ChildSignaled(object, kind == 7) ? "signaled" : "running";
    else if (kind == 9) state = X86::Application::ForeignProcessSignaled(object) ? "signaled" : "running";
    printf("PROBE_SAMPLE_WAIT thread=%u object=%08x kind=%s index=%u state=%s name=%s\n", thread, key,
           kinds[kind < 10 ? kind : 10], object, state, name);
}
static void report(X86::Application &app)
{
    if (!enabled)
        return;
    printf("PROBE_SAMPLE_SUMMARY stride=%u blocks=%llu samples=%llu addresses=%u overflow=%llu "
           "restarted=%u restarted_at_call=%u\n",
           stride, blocks, samples, used, overflow, restarted ? 1u : 0u, restarted_at);
    for (unsigned i = 0; i < 16; ++i)
    {
        const auto &t = app.threads[i];
        if (!t.fiber && !thread_blocks[i])
            continue;
        printf("PROBE_SAMPLE_THREAD index=%u id=%u blocks=%llu block_code=%u wait_handle=%08x "
               "done=%u suspended=%u deadline=%llu multiwait=%u eip=%08x\n",
               i, t.id, thread_blocks[i], t.block, t.wait_handle, t.done ? 1u : 0u, t.suspended,
               (unsigned long long)t.deadline, t.multiwait.count, thread_eip(app, i));
        if (t.block == 2 && t.wait_handle) describe_handle(app, i, t.wait_handle);
        for (unsigned n = 0; n < t.multiwait.count && n < 64; ++n)
            describe_wait_object(app, i, t.multiwait.objects[n]);
    }
    if (!slots)
        return;
    // Sort by count, descending, without pulling in a container: the table is
    // small and this runs once at the end of an observation.
    unsigned *order = (unsigned *)malloc(kSlots * sizeof(unsigned));
    if (!order) return;
    unsigned count = 0;
    for (unsigned i = 0; i < kSlots; ++i)
        if (slots[i].key) order[count++] = i;
    for (unsigned i = 1; i < count; ++i)
    {
        const unsigned key = order[i]; unsigned n = i;
        while (n && slots[order[n - 1]].count < slots[key].count) { order[n] = order[n - 1]; --n; }
        order[n] = key;
    }
    for (unsigned n = 0; n < count && n < 400; ++n)
    {
        const auto &slot = slots[order[n]];
        const unsigned thread = (unsigned)(slot.key >> 32) - 1, address = (unsigned)slot.key;
        const char *module = "?"; unsigned rva = address;
        if (app.image.Base() && address >= app.image.Base() &&
            address - app.image.Base() < app.image.SizeOfImage())
        { module = app.program_name; rva = address - app.image.Base(); }
        for (unsigned i = 0; i < 32; ++i)
        {
            const auto &m = app.modules[i];
            if (!m.kind || !m.pe || !m.pe->Base()) continue;
            if (address < m.pe->Base() || address - m.pe->Base() >= m.pe->SizeOfImage()) continue;
            module = m.path; rva = address - m.pe->Base(); break;
        }
        printf("PROBE_SAMPLE thread=%u address=%08x count=%llu rva=%08x module=", thread, address,
               slot.count, rva);
        hex_text(module);
        putchar('\n');
    }
    free(order);
    for (unsigned i = 0; i < 32; ++i)
    {
        const auto &m = app.modules[i];
        if (!m.kind || !m.pe) continue;
        printf("PROBE_SAMPLE_MODULE index=%u base=%08x size=%08x path=", i, m.pe->Base(), m.pe->SizeOfImage());
        hex_text(m.path);
        putchar('\n');
    }
    printf("PROBE_SAMPLE_MAIN base=%08x size=%08x name=", app.image.Base(), app.image.SizeOfImage());
    hex_text(app.program_name);
    putchar('\n');
}
}
static void snapshot_property_sheet(X86::Application &app);
static void snapshot_cpp_exception(X86::Application &app);
static void logger(const char *s)
{
    puts(s);
    // Capture nested calls before Application::Call restores its caller's
    // register context. Only inspect the app explicitly owned by this probe.
    if (observed_app && strstr(s, "Unimplemented API") && strstr(s, "PropertySheet"))
        snapshot_property_sheet(*observed_app);
    if (observed_app && strstr(s, "Unimplemented API") && strstr(s, "__CxxFrameHandler"))
        snapshot_cpp_exception(*observed_app);
    fflush(stdout);
}
// Benchmark support for tools/bench_dispatch.py. The per-instruction hook is
// slow by design, so it is only installed on request and the timed runs of the
// benchmark leave it off.
static unsigned long long counted_instructions = 0;
static void count_instruction(X86::Cpu &, unsigned, unsigned, void *) { ++counted_instructions; }
// A watch point on a guest address. Every hit prints the integer registers, so
// a run can be compared against a behavior analysis of the same code without a
// debugger. The hook is installed for one address only, so it costs nothing
// outside that address; --trace-eip takes the address and an optional limit.
// Every hit also prints the top of the stack and the saved return address of a
// frame with EBP, which is what names the caller of a function under watch.
struct EipWatch
{
    unsigned address = 0;
    unsigned limit = 0;
    unsigned hits = 0;
};
// A range of guest memory written to a file at the end of the run:
// --dump-memory <hexaddr>:<hexsize>:<path>. A packed program (ASPack and the
// like) only shows its real code in memory, and a dump of the running image is
// what a disassembler needs.
struct MemoryDump
{
    unsigned address = 0, size = 0;
    const char *path = nullptr;
};
static std::vector<MemoryDump> memory_dumps;
static void write_memory_dumps(X86::Application &app)
{
    for (const auto &d : memory_dumps)
    {
        FILE *file = fopen(d.path, "wb");
        if (!file) { printf("PROBE_DUMP addr=%08x error=open\n", d.address); continue; }
        unsigned written = 0, missing = 0;
        unsigned char page[4096];
        for (unsigned at = 0; at < d.size; at += sizeof page)
        {
            const unsigned chunk = d.size - at < sizeof page ? d.size - at : (unsigned)sizeof page;
            if (app.cpu->Read(d.address + at, page, chunk) != X86::CpuError::Ok)
            {
                memset(page, 0, chunk);
                missing += chunk;
            }
            written += (unsigned)fwrite(page, 1, chunk, file);
        }
        fclose(file);
        printf("PROBE_DUMP addr=%08x size=%u written=%u unreadable=%u path=", d.address, d.size,
               written, missing);
        hex_text(d.path);
        putchar('\n');
    }
}
// Addresses whose first eight dwords every watch hit prints. --trace-memory
// takes a guest address; a watch point without it prints registers only.
static std::vector<unsigned> memory_watches;
static std::vector<EipWatch> eip_watches;
static void eip_watch_hook(X86::Cpu &engine, unsigned address, unsigned, void *user)
{
    X86::Cpu *cpu = &engine;
    auto *watch = static_cast<EipWatch *>(user);
    ++watch->hits;
    if (watch->limit && watch->hits > watch->limit)
        return;
    unsigned r[8] = {0};
    static const int ids[8] = {X86::Reg::Eax, X86::Reg::Ecx, X86::Reg::Edx, X86::Reg::Ebx,
                               X86::Reg::Esp, X86::Reg::Ebp, X86::Reg::Esi, X86::Reg::Edi};
    for (int i = 0; i < 8; ++i) cpu->Get(ids[i], r[i]);
    printf("PROBE_TRACE_EIP addr=%08x hit=%u eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x",
           (unsigned)address, watch->hits, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
    // The four words at the top of the stack and the saved return address of a
    // frame that has EBP, so a hit says who called. Reading only: uc_mem_read
    // does not stop the process on an unmapped address.
    unsigned word = 0;
    for (unsigned n = 0; n < 4; ++n)
        printf(" [esp+%u]=%08x", n * 4,
               cpu->Read(r[4] + n * 4, &word, 4) == X86::CpuError::Ok ? word : 0xbadbad00u);
    printf(" [ebp+4]=%08x\n",
           cpu->Read(r[5] + 4, &word, 4) == X86::CpuError::Ok ? word : 0xbadbad00u);
    for (unsigned base : memory_watches)
    {
        printf("PROBE_TRACE_MEM addr=%08x hit=%u at=%08x", (unsigned)address, watch->hits, base);
        for (unsigned n = 0; n < 8; ++n)
            printf(" %08x", cpu->Read(base + n * 4, &word, 4) == X86::CpuError::Ok ? word
                                                                                  : 0xbadbad00u);
        putchar('\n');
    }
}
static const char *leaf(const char *s)
{
    const auto *p = strrchr(s, '/');
    return p ? p + 1 : s;
}
static void hex_text(const char *text)
{
    for (const auto *p = reinterpret_cast<const unsigned char *>(text); *p; ++p)
        printf("%02x", *p);
}
// How many different colours the composed desktop shows inside one window's
// client area. tools/smoke_matrix.py calls a visible top level window with more
// than one colour a window with content and one with a single colour an empty
// window. This only reads the finished framebuffer,
// the same surface --screenshot writes, so it enters no guest code.
static void client_colours(unsigned hwnd)
{
    const auto *pixels = User::Pixels();
    Gdi::RECT client{};
    Gdi::POINT origin{0, 0};
    if (!pixels || !User::GetClientRect(hwnd, &client) || !User::ScreenToClient(hwnd, &origin))
        return;
    // ScreenToClient maps the screen origin into client coordinates, so its
    // negation is where the client area starts on the screen.
    const int left = client.left - origin.x, top = client.top - origin.y;
    const int right = client.right - origin.x, bottom = client.bottom - origin.y;
    const int x0 = left < 0 ? 0 : left, y0 = top < 0 ? 0 : top;
    const int x1 = right > 800 ? 800 : right, y1 = bottom > 600 ? 600 : bottom;
    unsigned colours[65] = {}, count = 0, sampled = 0;
    bool overflow = false;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            const unsigned i = (unsigned)(y * 800 + x) * 4;
            const unsigned value = (unsigned)pixels[i] | ((unsigned)pixels[i + 1] << 8) |
                                   ((unsigned)pixels[i + 2] << 16) | 0xff000000u;
            ++sampled;
            unsigned n = 0;
            for (; n < count; ++n)
                if (colours[n] == value) break;
            if (n < count) continue;
            if (count == 64) { overflow = true; continue; }
            colours[count++] = value;
        }
    printf(" client_x=%d client_y=%d client_w=%d client_h=%d visible_pixels=%u colours=%u%s", left,
           top, client.right - client.left, client.bottom - client.top, sampled, count,
           overflow ? "+" : "");
}
static void snapshot_windows()
{
    unsigned handles[192], count = 0;
    if (!User::WindowSnapshot(User::GetDesktopWindow(), handles, 192, count))
        return;
    for (unsigned i = 0; i < count; ++i)
    {
        const unsigned hwnd = handles[i];
        char klass[64];
        std::vector<char> text(User::TextLength(hwnd) + 1);
        User::GetClassName(hwnd, klass, sizeof klass);
        User::GetWindowText(hwnd, text.data(), text.size());
        // The final rectangle, not the one CreateWindowExA was called with: a
        // property sheet moves and grows its frame in its own WM_INITDIALOG.
        Gdi::RECT bounds{};
        User::GetWindowRect(hwnd, &bounds);
        printf("PROBE_WINDOW hwnd=%u parent=%u owner=%u visible=%u x=%d y=%d w=%d h=%d class=", hwnd,
               User::Parent(hwnd), User::Owner(hwnd), User::IsVisible(hwnd), bounds.left, bounds.top,
               bounds.right - bounds.left, bounds.bottom - bounds.top);
        hex_text(klass);
        printf(" text=");
        hex_text(text.data());
        bool has = false, drawn = false;
        if (User::CanvasInfo(hwnd, has, drawn)) printf(" canvas=%u drawn=%u", has ? 1u : 0u, drawn ? 1u : 0u);
        if (User::IsVisible(hwnd) && !User::Parent(hwnd) && hwnd != User::GetDesktopWindow())
            client_colours(hwnd);
        putchar('\n');
    }
}
static unsigned visible_dialog(const char *caption)
{
    if (!caption) return 0;
    unsigned nodes[192], count = 0;
    if (!User::WindowSnapshot(0, nodes, 192, count)) return 0;
    for (unsigned i = 0; i < count; ++i) {
        char name[64], text[512];
        User::GetClassName(nodes[i], name, sizeof name);
        User::GetWindowText(nodes[i], text, sizeof text);
        if (User::IsVisible(nodes[i]) && !strcmp(name, "#32770") && !strcmp(text, caption))
            return nodes[i];
    }
    return 0;
}
static bool screenshot(const char *path)
{
    if (!path) return true;
    // A run that never started has no desktop surface. Saying so is better than
    // dereferencing the null the compositor returns.
    const auto *pixels = User::Pixels();
    if (!pixels) { printf("PROBE_SCREENSHOT_SKIPPED path=%s\n", path); return true; }
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    fprintf(file, "P6\n800 600\n255\n");
    for (unsigned i = 0; i < 800 * 600; ++i) {
        unsigned char rgb[] = {pixels[i*4+2], pixels[i*4+1], pixels[i*4]};
        if (fwrite(rgb, 1, 3, file) != 3) { fclose(file); return false; }
    }
    return fclose(file) == 0;
}
static unsigned pointer_overlay_proof(X86::Application &app, int x, int y)
{
    User::ShowPointer(false);
    app.MouseButtons(x, y, 0);
    const auto *pixels = User::Pixels();
    if (!pixels) return 0;
    std::vector<unsigned char> underneath(pixels, pixels + 800u * 600u * 4u);
    User::ShowPointer(true);
    pixels = User::Pixels();
    unsigned changed = 0;
    for (unsigned i = 0; i < underneath.size(); i += 4)
        changed += memcmp(underneath.data() + i, pixels + i, 4) != 0;
    printf("PROBE_POINTER_OVERLAY x=%d y=%d shape=%08x visible=%u changed=%u\n",
           x, y, User::PointerShape(), User::PointerVisible() ? 1u : 0u, changed);
    return changed;
}
static void snapshot_cpp_exception(X86::Application &app)
{
    if (!app.cpu || app.Status() != X86::Application::Failed) return;
    auto read = [&](unsigned p, void *out, unsigned bytes) {
        return app.cpu->Read(p, out, bytes) == X86::CpuError::Ok;
    };
    auto dump = [&](const char *name, unsigned p, unsigned words) {
        unsigned data[20] = {};
        if (words > 20 || !read(p, data, words * 4)) return;
        printf("PROBE_CXX name=%s address=%08x data=", name, p);
        for (unsigned i = 0; i < words; ++i) printf("%08x,", data[i]);
        putchar('\n');
    };
    unsigned sp = 0, metadata = 0, args[5] = {}, record[20] = {};
    if (app.cpu->Get(X86::Reg::Esp, sp) != X86::CpuError::Ok ||
        app.cpu->Get(X86::Reg::Eax, metadata) != X86::CpuError::Ok ||
        !read(sp, args, sizeof args)) return;
    dump("arguments", sp, 5);
    dump("function", metadata, 10);
    dump("record", args[1], 20);
    if (args[2] >= 4) dump("frame", args[2] - 4, 8);
    dump("context-ebp-eip", args[3] + 0xb4, 2);
    dump("context-esp", args[3] + 0xc4, 1);
    if (!read(args[1], record, sizeof record)) return;
    if ((record[0] & 0x0ffffffe) == 0x006d007e && record[4] == 1) {
        unsigned delay[9] = {};
        dump("delay-load", record[5], 9);
        if (read(record[5], delay, sizeof delay) && delay[0] == sizeof delay) {
            for (unsigned i = 0; i < (delay[4] == 1 ? 2u : 1u); ++i) {
                char text[260] = {}; unsigned address = delay[i ? 5 : 3];
                for (unsigned n = 0; n < sizeof text; ++n) {
                    if (!read(address + n, text + n, 1)) break;
                    if (!text[n]) {
                        printf("PROBE_CXX_DELAY %s=", i ? "name" : "dll");
                        hex_text(text); putchar('\n'); break;
                    }
                }
            }
        }
        return;
    }
    if (record[0] != 0xe06d7363 || record[4] != 3) return;
    unsigned info[4] = {}, types[2] = {};
    dump("throw-info", record[7], 4);
    if (!read(record[7], info, sizeof info)) return;
    dump("catchable-array", info[3], 2);
    if (read(info[3], types, sizeof types) && types[0]) dump("catchable-type", types[1], 7);
}
static void snapshot_property_sheet(X86::Application &app)
{
    if (!app.cpu || app.Status() != X86::Application::Failed ||
        !strstr(app.Error(), "PropertySheet")) return;
    unsigned sp = 0, address = 0, header[16] = {};
    auto read = [&](unsigned p, void *out, unsigned bytes) {
        return app.cpu->Read(p, out, bytes) == X86::CpuError::Ok;
    };
    if (app.cpu->Get(X86::Reg::Esp, sp) != X86::CpuError::Ok ||
        !read(sp + 4, &address, 4) || !read(address, header, 4) ||
        header[0] < 40 || header[0] > sizeof header || (header[0] & 3) ||
        !read(address, header, header[0])) return;
    printf("PROBE_PROPSHEET address=%08x header=", address);
    for (unsigned i = 0; i < header[0] / 4; ++i) printf("%08x,", header[i]);
    putchar('\n');
    if (header[6] > 16) return;
    unsigned page_address = header[8];
    for (unsigned i = 0; i < header[6]; ++i) {
        unsigned page[16] = {};
        if (!(header[1] & 8)) {
            if (!read(page_address + i * 4, page, 4)) return;
            printf("PROBE_PROPPAGE index=%u handle=%08x\n", i, page[0]);
            continue;
        }
        if (!read(page_address, page, 4) || page[0] < 40 ||
            page[0] > sizeof page || (page[0] & 3) ||
            !read(page_address, page, page[0])) return;
        printf("PROBE_PROPPAGE index=%u address=%08x data=", i, page_address);
        for (unsigned n = 0; n < page[0] / 4; ++n) printf("%08x,", page[n]);
        putchar('\n');
        page_address += page[0];
    }
}
static void snapshot_delay_failure(X86::Application &app)
{
    if (!app.cpu || app.Status() != X86::Application::Failed ||
        (!strstr(app.Error(), "RaiseException") && !strstr(app.Error(), "Unhandled exception")))
        return;
    // Inspect the stopped guest without calling Application::Read, whose
    // failure path changes runtime state. DelayLoadInfo is nine x86 DWORDs.
    unsigned sp = 0, stack[5] = {}, address = 0, info[9] = {}, code = 0, at = 0, count = 0;
    auto read = [&](unsigned p, void *out, unsigned bytes) {
        return app.cpu->Read(p, out, bytes) == X86::CpuError::Ok;
    };
    // Two stops carry the same structure: a RaiseException call that is still
    // on the guest stack, and an exception the guest never handled, whose first
    // parameter the runtime names in its failure message.
    if (sscanf(app.Error(), "Unhandled exception %x at %x parameters %u first %x",
               &code, &at, &count, &address) != 4 || count != 1 || !address) {
        address = 0;
        if (app.cpu->Get(X86::Reg::Esp, sp) != X86::CpuError::Ok ||
            !read(sp, stack, sizeof stack) || stack[3] != 1 ||
            !read(stack[4], &address, 4))
            return;
        code = stack[1];
    }
    if (!read(address, info, sizeof info) ||
        info[0] != sizeof info || info[4] > 1 || (info[8] != 126 && info[8] != 127))
        return;
    char dll[260] = {};
    unsigned i = 0;
    for (; i < sizeof dll; ++i)
        if (!read(info[3] + i, dll + i, 1)) return;
        else if (!dll[i]) break;
    if (i == sizeof dll) return;
    printf("PROBE_DELAY exception=%08x thunk=%08x by_name=%u import=%08x last_error=%u dll=",
           code, info[2], info[4], info[5], info[8]);
    hex_text(dll);
    putchar('\n');
    if (info[4]) {
        char name[260] = {};
        for (unsigned n = 0; n < sizeof name; ++n) {
            if (!read(info[5] + n, name + n, 1)) return;
            if (!name[n]) {
                printf("PROBE_DELAY_NAME name=");
                hex_text(name);
                putchar('\n');
                break;
            }
        }
    }
}
// A scripted sequence of real input packets and captures.
// Every step drives the same entries an emulated USB mouse drives in the Pi
// kernel: Application::MouseButtons and Application::RawKey. Nothing here
// writes guest memory, enters a callback or edits a window; `window` and
// `text` only read the compositor's window list to find where to click, the
// way tools/test_explorer_ui.py reads the serial trace for the same purpose.
namespace script {
struct Step
{
    enum Kind { Point, Window, Text, WaitWindow, Wait, Shot, Key, KeyDown, KeyUp, Move, Dialog, Press, Release, WaitIdle, Mark, ClickChanged, Stop } kind;
    char text[128] = {};
    int x = 0, y = 0, dx = 0, dy = 0;
    unsigned ms = 0;
};
static std::vector<Step> steps;
static std::vector<unsigned char> marked;
static unsigned at = 0;
static auto since = std::chrono::steady_clock::time_point{};
static bool started = false;
static unsigned deadline_ms = 8000;
// The buttons a press step left held. `move` keeps them down, so a script can
// express a drag: press, one or more moves, release.
static unsigned held = 0;
static bool stop_requested = false;
// Reads the class name or window text of every window the compositor knows and
// returns the first visible match. This is the same read-only inspection
// snapshot_windows performs after the run.
static unsigned find(const char *want, bool by_text, Gdi::RECT &bounds)
{
    unsigned nodes[192], count = 0;
    if (!User::WindowSnapshot(User::GetDesktopWindow(), nodes, 192, count))
        return 0;
    for (unsigned i = 0; i < count; ++i) {
        if (!User::IsVisible(nodes[i]))
            continue;
        char name[128] = {};
        if (by_text) User::GetWindowText(nodes[i], name, sizeof name);
        else User::GetClassName(nodes[i], name, sizeof name);
        if (strcmp(name, want))
            continue;
        if (!User::GetWindowRect(nodes[i], &bounds))
            continue;
        if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
            continue;
        return nodes[i];
    }
    return 0;
}
static void click(X86::Application &app, int x, int y)
{
    held = 0;
    app.MouseButtons(x, y, 0);
    app.MouseButtons(x, y, 1);
    app.MouseButtons(x, y, 0);
}
// One step per pump. Returns false when the script is finished.
static bool advance(X86::Application &app)
{
    if (at >= steps.size())
        return false;
    const auto now = std::chrono::steady_clock::now();
    if (!started) { started = true; since = now; }
    auto &step = steps[at];
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(now - since).count();
    auto done = [&](const char *what, int x, int y) {
        printf("PROBE_SCRIPT index=%u step=%s x=%d y=%d waited=%lld calls=%u windows=%u target=",
               at, what, x, y, (long long)waited, app.ApiCalls(), User::LiveWindows());
        hex_text(step.text);
        putchar('\n');
        fflush(stdout);
        ++at;
        started = false;
    };
    switch (step.kind) {
    case Step::Wait:
        if (waited < (long long)step.ms) return true;
        done("wait", 0, 0);
        return true;
    case Step::Stop:
        stop_requested = true;
        done("stop", 0, 0);
        return true;
    case Step::Shot:
        if (!screenshot(step.text)) { printf("PROBE_SCRIPT_FAILED index=%u reason=screenshot\n", at); ++at; return true; }
        done("shot", 0, 0);
        return true;
    case Step::Point:
        click(app, step.x, step.y);
        done("point", step.x, step.y);
        return true;
    case Step::Move:
        app.MouseButtons(step.x, step.y, held);
        done("move", step.x, step.y);
        return true;
    case Step::Mark: {
        // "mark": keep the composed desktop as it is now.
        const auto *pixels = User::Pixels();
        marked.assign(pixels, pixels + (pixels ? 800u * 600u * 4u : 0u));
        done("mark", 0, 0);
        return true;
    }
    case Step::ClickChanged: {
        // "click-changed N [X0 Y0 X1 Y1]": the regions of the desktop that
        // differ from the mark, on a grid of four pixels and joined where they
        // touch; the N largest are clicked at their centres, in reading order,
        // each held for 100 ms. A game that highlights a hint (Shanghai II,
        // "Find a pair") is played this way whatever its deal is.
        const auto *pixels = User::Pixels();
        if (!pixels || marked.size() != 800u * 600u * 4u) {
            printf("PROBE_SCRIPT_FAILED index=%u reason=no-mark\n", at); ++at; return true;
        }
        const int x0 = step.dx ? step.dx : 0, y0 = step.dy ? step.dy : 0;
        const int x1 = step.text[0] ? atoi(step.text) : 800, y1 = step.ms ? int(step.ms) : 600;
        const int w = 200, h = 150;
        std::vector<int> label(w * h, 0);
        auto changed = [&](int gx, int gy) {
            const int x = gx * 4, y = gy * 4;
            if (x < x0 || y < y0 || x >= x1 || y >= y1) return false;
            const unsigned i = unsigned(y * 800 + x) * 4;
            return memcmp(pixels + i, marked.data() + i, 3) != 0;
        };
        struct Region { int count = 0, left = 800, top = 600, right = 0, bottom = 0; };
        std::vector<Region> regions;
        std::vector<int> stack;
        for (int gy = 0; gy < h; ++gy)
            for (int gx = 0; gx < w; ++gx) {
                if (label[gy * w + gx] || !changed(gx, gy)) continue;
                Region region;
                const int id = int(regions.size()) + 1;
                stack.assign(1, gy * w + gx);
                label[gy * w + gx] = id;
                while (!stack.empty()) {
                    const int cell = stack.back(); stack.pop_back();
                    const int cx = cell % w, cy = cell / w;
                    ++region.count;
                    region.left = std::min(region.left, cx * 4); region.right = std::max(region.right, cx * 4);
                    region.top = std::min(region.top, cy * 4); region.bottom = std::max(region.bottom, cy * 4);
                    for (int ny = cy - 1; ny <= cy + 1; ++ny)
                        for (int nx = cx - 1; nx <= cx + 1; ++nx)
                            if (nx >= 0 && ny >= 0 && nx < w && ny < h && !label[ny * w + nx] && changed(nx, ny)) {
                                label[ny * w + nx] = id; stack.push_back(ny * w + nx);
                            }
                }
                regions.push_back(region);
            }
        std::sort(regions.begin(), regions.end(), [](const Region &a, const Region &b) { return a.count > b.count; });
        const unsigned wanted = std::min<unsigned>((unsigned)step.x, (unsigned)regions.size());
        regions.resize(wanted);
        std::sort(regions.begin(), regions.end(), [](const Region &a, const Region &b) {
            return a.top != b.top ? a.top < b.top : a.left < b.left; });
        std::vector<Step> clicks;
        for (const auto &region : regions) {
            const int cx = (region.left + region.right) / 2, cy = (region.top + region.bottom) / 2;
            printf("PROBE_CHANGED x=%d y=%d w=%d h=%d cells=%d\n", region.left, region.top,
                   region.right - region.left + 4, region.bottom - region.top + 4, region.count);
            Step press{}; press.kind = Step::Press; press.x = cx; press.y = cy; press.dx = 1;
            Step hold{}; hold.kind = Step::Wait; hold.ms = 100;
            Step release = press; release.kind = Step::Release;
            Step pause{}; pause.kind = Step::Wait; pause.ms = 400;
            clicks.insert(clicks.end(), {press, hold, release, pause});
        }
        done("click-changed", (int)wanted, 0);
        steps.insert(steps.begin() + at, clicks.begin(), clicks.end());
        return true;
    }
    case Step::Press:
        held |= (unsigned)step.dx;
        app.MouseButtons(step.x, step.y, held);
        done("press", step.x, step.y);
        return true;
    case Step::Release:
        held &= ~(unsigned)step.dx;
        app.MouseButtons(step.x, step.y, held);
        done("release", step.x, step.y);
        return true;
    case Step::Key:
        app.RawKey((unsigned)step.x, (unsigned)step.dy, true,step.y,step.dx!=0);
        app.RawKey((unsigned)step.x, 0, false,step.y,step.dx!=0);
        done("key", step.x, step.dy);
        return true;
    case Step::KeyDown:
    case Step::KeyUp:
        app.RawKey((unsigned)step.x, step.kind == Step::KeyDown ? (unsigned)step.dy : 0u,
                   step.kind == Step::KeyDown,step.y,step.dx!=0);
        done(step.kind == Step::KeyDown ? "keydown" : "keyup", step.x, step.dy);
        return true;
    case Step::WaitIdle:
        if (!app.WaitingForInput()) {
            if (waited < (long long)deadline_ms) return true;
            printf("PROBE_SCRIPT_FAILED index=%u reason=not-idle\n", at);
            fflush(stdout);at=steps.size();return false;
        }
        done("wait-idle",0,0);return true;
    case Step::WaitWindow: {
        Gdi::RECT bounds{};
        if (!find(step.text, false, bounds)) {
            if (waited < (long long)deadline_ms) return true;
            printf("PROBE_SCRIPT_FAILED index=%u reason=no-window target=", at);
            hex_text(step.text); putchar('\n'); fflush(stdout);
            at = steps.size(); return false;
        }
        done("wait-window", 0, 0);
        return true;
    }
    case Step::Dialog: {
        // The dialog's own default button, the same choice the
        // --accept-dialog-caption path makes: IDOK first, then IDCANCEL,
        // because a finished MB_OK box carries IDCANCEL on its only button.
        const unsigned dialog = visible_dialog(step.text);
        Gdi::RECT bounds{};
        unsigned button = dialog ? User::GetDlgItem(dialog, 1) : 0;
        if (dialog && !button) button = User::GetDlgItem(dialog, 2);
        if (!dialog || !button || !User::IsVisible(button) || !User::IsEnabled(button) ||
            !User::GetWindowRect(button, &bounds)) {
            if (waited < (long long)deadline_ms) return true;
            printf("PROBE_SCRIPT_FAILED index=%u reason=no-dialog calls=%u target=", at, app.ApiCalls());
            hex_text(step.text);
            putchar('\n');
            fflush(stdout);
            at = steps.size();
            return false;
        }
        const int x = (bounds.left + bounds.right) / 2, y = (bounds.top + bounds.bottom) / 2;
        printf("PROBE_SCRIPT_TARGET index=%u hwnd=%u left=%d top=%d right=%d bottom=%d\n", at, button,
               bounds.left, bounds.top, bounds.right, bounds.bottom);
        click(app, x, y);
        done("dialog", x, y);
        return true;
    }
    case Step::Window:
    case Step::Text: {
        Gdi::RECT bounds{};
        const unsigned hwnd = find(step.text, step.kind == Step::Text, bounds);
        if (!hwnd) {
            if (waited < (long long)deadline_ms) return true;
            printf("PROBE_SCRIPT_FAILED index=%u reason=not-found calls=%u target=", at, app.ApiCalls());
            hex_text(step.text);
            putchar('\n');
            fflush(stdout);
            at = steps.size();
            return false;
        }
        const int x = (bounds.left + bounds.right) / 2 + step.dx;
        const int y = (bounds.top + bounds.bottom) / 2 + step.dy;
        printf("PROBE_SCRIPT_TARGET index=%u hwnd=%u left=%d top=%d right=%d bottom=%d\n", at, hwnd,
               bounds.left, bounds.top, bounds.right, bounds.bottom);
        click(app, x, y);
        done(step.kind == Step::Text ? "text" : "window", x, y);
        return true;
    }
    }
    return true;
}
static bool load(const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file) return false;
    char line[256];
    while (fgets(line, sizeof line, file)) {
        char verb[32] = {}, rest[200] = {};
        if (sscanf(line, "%31s %199[^\n]", verb, rest) < 1) continue;
        if (verb[0] == '#') continue;
        Step step{};
        if (!strcmp(verb, "point") || !strcmp(verb, "move")) {
            if (sscanf(rest, "%d %d", &step.x, &step.y) != 2) { fclose(file); return false; }
            step.kind = verb[0] == 'p' ? Step::Point : Step::Move;
        } else if (!strcmp(verb, "press") || !strcmp(verb, "release")) {
            // "press X Y [BUTTON]"; the button mask follows MouseButtons:
            // 1 left, 2 right, 4 middle. A press leaves the button held so that
            // the following move steps form a drag.
            step.dx = 1;
            if (sscanf(rest, "%d %d %d", &step.x, &step.y, &step.dx) < 2) { fclose(file); return false; }
            step.kind = verb[0] == 'p' ? Step::Press : Step::Release;
        } else if (!strcmp(verb, "wait-idle")) {
            step.kind = Step::WaitIdle;
        } else if (!strcmp(verb, "wait-window")) {
            step.kind = Step::WaitWindow; snprintf(step.text, sizeof step.text, "%s", rest);
        } else if (!strcmp(verb, "wait")) {
            step.kind = Step::Wait; step.ms = (unsigned)strtoul(rest, nullptr, 10);
        } else if (!strcmp(verb, "key")) {
            // "key VK [SCAN] [EXTENDED] [CHARACTER]". TranslateMessage makes
            // WM_CHAR from the virtual key and the key state through the
            // layout; the character only reaches a console process.
            step.kind = Step::Key; sscanf(rest,"%i %i %i %i",&step.x,&step.y,&step.dx,&step.dy);
        } else if (!strcmp(verb, "keydown") || !strcmp(verb, "keyup")) {
            step.kind = !strcmp(verb, "keydown") ? Step::KeyDown : Step::KeyUp;
            sscanf(rest,"%i %i %i %i",&step.x,&step.y,&step.dx,&step.dy);
        } else if (!strcmp(verb, "stop")) {
            // Ends the run at this point of the script, reason "script". A
            // session whose root is the shell never ends on its own.
            step.kind = Step::Stop;
        } else if (!strcmp(verb, "mark")) {
            step.kind = Step::Mark;
        } else if (!strcmp(verb, "click-changed")) {
            step.kind = Step::ClickChanged;
            int count = 0, left = 0, top = 0, right = 0, bottom = 0;
            if (sscanf(rest, "%d %d %d %d %d", &count, &left, &top, &right, &bottom) < 1) { fclose(file); return false; }
            step.x = count; step.dx = left; step.dy = top; step.ms = (unsigned)bottom;
            if (right) snprintf(step.text, sizeof step.text, "%d", right);
        } else if (!strcmp(verb, "shot")) {
            step.kind = Step::Shot; snprintf(step.text, sizeof step.text, "%s", rest);
        } else if (!strcmp(verb, "dialog")) {
            step.kind = Step::Dialog; snprintf(step.text, sizeof step.text, "%s", rest);
        } else if (!strcmp(verb, "window") || !strcmp(verb, "text")) {
            step.kind = verb[0] == 'w' ? Step::Window : Step::Text;
            // "<name>" or "<name> @ dx dy": the offset picks a point inside a
            // window whose centre is not the item we want, such as a menu row.
            char *split = strstr(rest, " @ ");
            if (split) { *split = 0; sscanf(split + 3, "%d %d", &step.dx, &step.dy); }
            snprintf(step.text, sizeof step.text, "%s", rest);
        } else { fclose(file); return false; }
        steps.push_back(step);
    }
    fclose(file);
    printf("PROBE_SCRIPT_LOADED steps=%u\n", (unsigned)steps.size());
    return true;
}
}
// Diagnostic export-stub reporting. Nothing printed here is a ported export.
// The counters of our own Unicorn build (tools/build_unicorn_host.py). They say
// how often the guest left the fast store path, what that cost in mapping
// lookups, and how full the translation cache got. The symbol is weak, so a
// runner linked against a library without the patches simply prints nothing.
extern "C" void uc_neo98_counters(unsigned long long *out, unsigned long count) __attribute__((weak));
static void unicorn_counters()
{
    if (!uc_neo98_counters)
        return;
    unsigned long long c[8] = {};
    uc_neo98_counters(c, 8);
    printf("PROBE_UNICORN store_slow=%llu notdirty=%llu map_lookup=%llu map_hit=%llu "
           "tb_flush=%llu code_bytes=%llu tb_invalidate=%llu\n", c[0], c[1], c[2], c[3], c[4], c[5], c[6]);
}
static void stub_report(X86::Application &app)
{
    if (!app.ExportStubsEnabled())
        return;
    static const char *const kinds[] = {"none", "import", "dynamic", "original_error"};
    printf("PROBE_STUB_SUMMARY records=%u calls=%u\n", app.StubRecordCount(), app.StubCalls());
    for (unsigned i = 0; i < app.StubRecordCount(); ++i)
    {
        const auto &r = app.StubRecordAt(i);
        printf("PROBE_STUB kind=%s dll=", kinds[r.kind < 4 ? r.kind : 0]);
        hex_text(r.dll);
        printf(" name=");
        hex_text(r.name);
        printf(" calls=%u first_call=%u last_call=%u gate=%08x return=%08x pop=%u pop_known=%u args=",
               r.calls, r.first_call, r.last_call, r.first_eip, r.first_return, r.pop_bytes, r.pop_known);
        for (unsigned n = 0; n < 8; ++n)
            printf("%08x%s", r.first_args[n], n == 7 ? "" : ",");
        putchar('\n');
    }
    for (unsigned i = 0; i < 32; ++i)
    {
        const auto &m = app.modules[i];
        if (!m.kind)
            continue;
        printf("PROBE_MODULE index=%u kind=%u handle=%08x path=", i, m.kind, m.handle);
        hex_text(m.path);
        putchar('\n');
    }
}
// The measuring mode: every import this process bound,
// whether statically or through GetProcAddress, with its class and the number
// of calls it took. tools/import_census.py reads these lines out of the log.
static void census_report(X86::Application &app)
{
    if (!X86::Application::export_census)
        return;
    printf("PROBE_CENSUS_SUMMARY imports=%u calls=%u\n", app.import_count, app.ApiCalls());
    for (unsigned i = 0; i < app.import_count; ++i)
    {
        const auto &item = app.imports[i];
        // A record that exists only because a native provider's export
        // directory names it is not an import of this program.
        if (!item.dll[0] || !strcmp(item.dll, "NEO98") || item.header_only)
            continue;
        // The two sentinels first: 0xffffffff carries every bit.
        const char *kind =
            (item.argc == 0xffffffffu || item.argc == 0xfffffffeu) ? "unbound"
            : (item.argc == X86::Application::FlatThunkGate)       ? "thunk"
            : (item.argc & X86::Application::MissingExportGate)    ? "missing"
            : (item.argc & 0x20000000u)                            ? "original"
                                                                   : "connected";
        printf("PROBE_CENSUS_IMPORT dll=");
        hex_text(item.dll);
        printf(" name=");
        hex_text(item.name);
        printf(" kind=%s calls=%u\n", kind, item.calls);
    }
}
// Seed a whole folder tree into the RAM volume from a manifest that an importer
// wrote: one guest path per line, then a tab, then '-' for a directory or the
// host file whose bytes the guest file gets. tools/import_start_menu.py writes
// the Start Menu tree this way, so the set seeded here is the set it recorded.
static bool seed_manifest(const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return false;
    char line[1024];
    unsigned directories = 0, files = 0, bytes = 0;
    while (fgets(line, sizeof line, file)) {
        char *tab = strchr(line, '\t');
        if (!tab)
            continue;
        *tab = 0;
        char *source = tab + 1;
        for (char *p = source; *p; ++p)
            if (*p == '\n' || *p == '\r') { *p = 0; break; }
        if (!line[0] || !source[0])
            continue;
        if (!strcmp(source, "-")) {
            const unsigned error = Files::System().MakeDirectory(line);
            if (error && error != 183) {
                fprintf(stderr, "seed manifest: cannot create %s, error %u\n", line, error);
                fclose(file);
                return false;
            }
            ++directories;
            continue;
        }
        auto data = load(source);
        const unsigned error = data.empty() ? 2u : Files::System().Seed(line, data.data(), data.size());
        if (error) {
            fprintf(stderr, "seed manifest: cannot seed %s from %s, error %u\n", line, source, error);
            fclose(file);
            return false;
        }
        ++files;
        bytes += (unsigned)data.size();
    }
    fclose(file);
    printf("PROBE_SEED_TREE manifest=");
    hex_text(path);
    printf(" directories=%u files=%u bytes=%u\n", directories, files, bytes);
    return true;
}
// Read-only inventory of the RAM volume after the run: which nodes exist, what
// each one's parent is and how big it is. It shows what a seeded tree looks
// like once the guest has been over it, including anything the guest created.
static void file_report()
{
    auto &store = Files::System();
    for (unsigned i = 0; i < 256; ++i) {
        const auto *node = store.Get(i);
        if (!node)
            continue;
        printf("PROBE_FILE node=%u parent=%u attributes=%u bytes=%u name=", i, node->parent,
               node->attributes, node->size);
        hex_text(node->name);
        putchar('\n');
    }
}
// --no-idle-stop: a program that animates from a WM_TIMER (the OpenGL screen
// savers) sits in GetMessage between frames; without the flag that wait ends
// the run as waiting-for-input.
static bool no_idle_stop = false;
static bool session_idle_stop = false;
static int desktop_depth = 0;
static bool load_stub_arguments(X86::Application &app, const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return false;
    char dll[64], name[128];
    unsigned bytes = 0, accepted = 0;
    while (fscanf(file, "%63s %127s %u", dll, name, &bytes) == 3)
        accepted += app.AddStubArgumentBytes(dll, name, bytes);
    fclose(file);
    printf("PROBE_STUB_ARGUMENTS accepted=%u\n", accepted);
    return true;
}
int main(int argc, char **argv)
{
    if (argc < 3)
        return 2;
    const char *command = nullptr, *accept_caption = nullptr, *dialog_screenshot = nullptr;
    const char *final_screenshot = nullptr, *cancel_caption = nullptr, *cancel_screenshot = nullptr;
    const char *audio_capture = nullptr;
    // Up to four --answer-dialog pairs, each answered once per dialog window.
    const char *answer_captions[4] = {};
    unsigned answer_ids[4] = {}, answer_count = 0, answered = 0;
    static short capture_frames[44100 * 2 * 120];
    const char *stub_arguments = nullptr, *click_script = nullptr, *current_directory = nullptr;
    std::vector<const char *> seed_manifests;
    std::vector<const char *> native_modules;
    bool stub_missing = false, trace_calls = false, count_instructions = false, api_counts = false;
    unsigned trace_from = 0;
    unsigned call_budget = 0, time_limit = 20, stall_limit = 0;
    // --frame-rate prints each second how often the screen changed
    // (User::FrameSerial) and how many pictures the display driver presented
    // (flips and blits into the primary surface): a game's frame rate.
    bool frame_rate = false;
    int pointer_proof_x = -1, pointer_proof_y = -1;
    unsigned cancelling = 0, candidate = 0, accepting = 0;
    auto candidate_since = std::chrono::steady_clock::time_point{};
    auto accepting_since = std::chrono::steady_clock::time_point{};
    // The Win98 tree is mounted before anything touches the RAM volume: it
    // raises the volume limits, and that only works before its first use.
    const auto tree_options = SystemTreeHost::Scan(argc, argv);
    if (tree_options.tree) {
        if (!SystemTreeHost::Mount(tree_options.root)) {
            fprintf(stderr, "no Win98 folder; run python3 neo98.py setup --iso ...\n");
            return 11;
        }
        printf("PROBE_SYSTEM_TREE source=");
        hex_text(SystemTree::Mounted()->Description());
        putchar('\n');
    }
    // The probe is the runner every automated test uses, so it keeps the
    // volatile RAM volume unless a run asks for a directory of its own with
    // --drive-c. That keeps those tests independent of each other.
    //
    {
        auto drive_options = HostVolume::Scan(argc, argv);
        drive_options.ram = drive_options.drive_c == nullptr;
        if (!HostVolume::Mount(drive_options)) {
            fprintf(stderr, "cannot mount the host drives\n");
            return 13;
        }
        if (!drive_options.ram) {
            printf("PROBE_DRIVE letter=C path=");
            hex_text(drive_options.drive_c);
            putchar('\n');
        }
        // Registry persistence is separate from file persistence: it needs its
        // own --registry, so a test can have one without the other.
        if (RegistryFile::Attach(argc, argv, nullptr)) {
            printf("PROBE_REGISTRY path=");
            hex_text(static_cast<RegistryFile::File *>(Registry::Attached())->Path());
            putchar('\n');
        }
        // --wininit: start the machine the way neo98-host does, with Win98's
        // WININIT.EXE step before the first process. Tests opt in,
        // so a pending WININIT.INI of the tree does not change other runs.
        for (int i = 3; i < argc; ++i)
            if (!strcmp(argv[i], "--wininit")) {
                const auto done = Win98Wininit::Run(Files::System(), logger);
                printf("PROBE_WININIT found=%d lines=%u renamed=%u deleted=%u skipped=%u failed=%u\n",
                       done.found ? 1 : 0, done.lines, done.renamed, done.deleted, done.skipped, done.failed);
            }
    }
    for (int i = 3; i < argc; i++)
    {
        if (!strcmp(argv[i], "--system-tree")) {
            if (i + 1 < argc && argv[i + 1][0] != '-') ++i;
            continue;
        }
        if (!strcmp(argv[i], "--exe")) { if (++i == argc) return 2; continue; }
        if (!strcmp(argv[i], "--ram-drive")) continue;
        if (!strcmp(argv[i], "--drive-c") || !strcmp(argv[i], "--drive") || !strcmp(argv[i], "--drive-readonly") ||
            !strcmp(argv[i], "--cdrom") ||
            !strcmp(argv[i], "--registry")) {
            if (++i == argc) return 2;
            continue;
        }
        if (!strcmp(argv[i], "--no-registry-file")) continue;
        if (!strcmp(argv[i], "--no-idle-stop")) { no_idle_stop = true; continue; }
        if (!strcmp(argv[i], "--wininit")) continue;
        if (!strcmp(argv[i], "--native-module")) {
            if (++i == argc) return 2;
            native_modules.push_back(argv[i]);
            continue;
        }
        if (!strcmp(argv[i], "--crash-report-dir")) {
            // Bare flag keeps the documented default directory.
            if (i + 1 < argc && argv[i + 1][0] != '-') crash_report_dir = argv[++i];
            else crash_report_dir = "build/crash-reports";
            continue;
        }
        if (!strcmp(argv[i], "--trace-calls")) { trace_calls = true; continue; }
        if (!strcmp(argv[i], "--api-counts")) { api_counts = true; continue; }
        // --host-clock starts the guest clock at the Mac's local time, as the
        // SDL host does, instead of the fixed 1998-06-25 12:00.
        // --printer-output DIR: the far end of LPT1 writes every print job
        // there (tools/printer_port_host.h).
        if (!strcmp(argv[i], "--printer-output")) {
            if (++i == argc) return 2;
            static PrinterPortHost::Directory printer(argv[i]);
            PrinterPort::Install(&printer);
            printer_sink = &printer;
            continue;
        }
        // --printer-jobs N: the run is not over before N jobs reached LPT1.
        // The spooler is a service process, which a finished session does
        // not wait for.
        if (!strcmp(argv[i], "--printer-jobs")) {
            if (++i == argc) return 2;
            printer_jobs = (unsigned)strtoul(argv[i], nullptr, 10);
            continue;
        }
        // --frozen-clock: the guest time of day stays at its start (tests
        // whose program seeds its random numbers from the clock).
        if (!strcmp(argv[i], "--desktop-depth")) { if (++i == argc) return 2; desktop_depth = atoi(argv[i]); continue; }
        if (!strcmp(argv[i], "--frozen-clock")) { X86::Application::guest_clock_frozen = true; continue; }
        if (!strcmp(argv[i], "--host-clock")) {
            const time_t now = time(nullptr);
            struct tm parts = {};
            localtime_r(&now, &parts);
            const unsigned short fields[8] = {
                (unsigned short)(parts.tm_year + 1900), (unsigned short)(parts.tm_mon + 1),
                (unsigned short)parts.tm_wday, (unsigned short)parts.tm_mday, (unsigned short)parts.tm_hour,
                (unsigned short)parts.tm_min, (unsigned short)(parts.tm_sec > 59 ? 59 : parts.tm_sec), 0};
            X86::Application::StartGuestClock(fields);
            continue;
        }
        if (!strcmp(argv[i], "--trace-from")) { if (++i == argc) return 2; trace_from = (unsigned)strtoul(argv[i], nullptr, 10); trace_calls = true; continue; }
        if (!strcmp(argv[i], "--seed-manifest")) { if (++i == argc) return 2; seed_manifests.push_back(argv[i]); continue; }
        if (!strcmp(argv[i], "--session-idle-stop")) { session_idle_stop = true; continue; }
        if (!strcmp(argv[i], "--click-script")) { if (++i == argc) return 2; click_script = argv[i]; continue; }
        if (!strcmp(argv[i], "--pointer-proof")) {
            if (i + 2 >= argc) return 2;
            pointer_proof_x = atoi(argv[++i]); pointer_proof_y = atoi(argv[++i]);
            continue;
        }
        // The current directory of the guest process. Win98 starts a program in
        // its own folder, and a game that opens "DATA\\LEVEL1.DAT" needs that.
        if (!strcmp(argv[i], "--current-directory")) { if (++i == argc) return 2; current_directory = argv[i]; continue; }
        if (!strcmp(argv[i], "--click-deadline")) { if (++i == argc) return 2; script::deadline_ms = (unsigned)strtoul(argv[i], nullptr, 10); continue; }
        if (!strcmp(argv[i], "--sample-eip")) {
            sampling::enabled = true;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
                sampling::stride = (unsigned)strtoul(argv[++i], nullptr, 10);
            if (!sampling::stride) sampling::stride = 1;
            continue;
        }
        if (!strcmp(argv[i], "--stub-missing-exports")) { stub_missing = true; continue; }
        // MEASURING MODE, never an ordinary run: a call of an export this port
        // does not implement writes a line and returns the answer of an
        // unimplemented Win98 export instead of stopping the run, so one start
        // shows every such call.
        if (!strcmp(argv[i], "--census")) { X86::Application::export_census = true; continue; }
        // Gives every readable guest page the execute right at once, the way
        // the port worked before For A/B measurement
        // with one binary.
        if (!strcmp(argv[i], "--execute-everything")) { X86::Application::execute_on_demand = false; continue; }
        // Sends every API call through the ordinary stop of uc_emu_start, the
        // way the port worked before fast-gate dispatch.
        if (!strcmp(argv[i], "--no-fast-gates")) { X86::Application::fast_gates = false; continue; }
        if (!strcmp(argv[i], "--count-instructions")) { count_instructions = true; continue; }
        if (!strcmp(argv[i], "--trace-eip")) {
            if (++i == argc) return 2;
            EipWatch watch;
            watch.address = (unsigned)strtoul(argv[i], nullptr, 16);
            const char *colon = strchr(argv[i], ':');
            if (colon) watch.limit = (unsigned)strtoul(colon + 1, nullptr, 10);
            eip_watches.push_back(watch);
            continue;
        }
        if (!strcmp(argv[i], "--dump-memory")) {
            if (++i == argc) return 2;
            MemoryDump dump;
            dump.address = (unsigned)strtoul(argv[i], nullptr, 16);
            const char *colon = strchr(argv[i], ':');
            if (!colon) return 2;
            dump.size = (unsigned)strtoul(colon + 1, nullptr, 16);
            const char *second = strchr(colon + 1, ':');
            if (!second) return 2;
            dump.path = second + 1;
            memory_dumps.push_back(dump);
            continue;
        }
        if (!strcmp(argv[i], "--trace-memory")) {
            if (++i == argc) return 2;
            memory_watches.push_back((unsigned)strtoul(argv[i], nullptr, 16));
            continue;
        }
        if (!strcmp(argv[i], "--stub-arguments")) { if (++i == argc) return 2; stub_arguments = argv[i]; continue; }
        if (!strcmp(argv[i], "--call-budget")) { if (++i == argc) return 2; call_budget = (unsigned)strtoul(argv[i], nullptr, 10); continue; }
        if (!strcmp(argv[i], "--frame-rate")) { frame_rate = true; continue; }
        if (!strcmp(argv[i], "--time-limit")) { if (++i == argc) return 2; time_limit = (unsigned)strtoul(argv[i], nullptr, 10); continue; }
        if (!strcmp(argv[i], "--stall-limit")) { if (++i == argc) return 2; stall_limit = (unsigned)strtoul(argv[i], nullptr, 10); continue; }
        if (!strcmp(argv[i], "--cancel-dialog-caption") || !strcmp(argv[i], "--cancel-dialog-screenshot")) {
            const bool caption = !strcmp(argv[i], "--cancel-dialog-caption");
            if (++i == argc) return 2;
            (caption ? cancel_caption : cancel_screenshot) = argv[i];
            continue;
        }
        // --answer-dialog CAPTION ID: whenever a dialog with this caption is
        // visible, click its button with this control ID, as a user answering
        // it would (7 is IDNO of a MessageBox).
        if (!strcmp(argv[i], "--answer-dialog")) {
            if (i + 2 >= argc || answer_count == 4) return 2;
            answer_captions[answer_count] = argv[++i];
            answer_ids[answer_count++] = (unsigned)strtoul(argv[++i], nullptr, 10);
            continue;
        }
        // --audio-capture PATH: the mixer writes every frame it mixes into a
        // buffer, and the frames go to PATH (16-bit stereo, 44100 Hz) at the end.
        if (!strcmp(argv[i], "--audio-capture")) {
            if (++i == argc) return 2;
            audio_capture = argv[i];
            continue;
        }
        if (!strcmp(argv[i], "--screenshot")) {
            if (++i == argc) return 2;
            final_screenshot = argv[i];
            continue;
        }
        if (!strcmp(argv[i], "--accept-dialog-caption") || !strcmp(argv[i], "--dialog-screenshot")) {
            const bool accept = !strcmp(argv[i], "--accept-dialog-caption");
            if (++i == argc) return 2;
            (accept ? accept_caption : dialog_screenshot) = argv[i];
            continue;
        }
        if (!strcmp(argv[i], "--command"))
        {
            if (++i == argc)
                return 2;
            command = argv[i];
            continue;
        }
        auto bytes = load(argv[i]);
        // Programs live in C:\WINDOWS beside EXPLORER.EXE, as on the
        // installation and in the SDL host; libraries and applets in SYSTEM.
        const char *name = leaf(argv[i]);
        const bool program = strlen(name) > 4 && !strcasecmp(name + strlen(name) - 4, ".EXE");
        char guest[260];
        snprintf(guest, sizeof guest, program ? "C:\\WINDOWS\\%s" : "C:\\WINDOWS\\SYSTEM\\%s", name);
        if (bytes.empty() || Files::System().Seed(guest, bytes.data(), bytes.size()))
            return 3;
    }
    for (const auto *path : seed_manifests)
        if (!seed_manifest(path)) return 9;
    auto font = load(argv[2]);
    std::vector<unsigned char> exe;
    // --exe starts any program without a profile. A guest path names a file of
    // the mounted tree and stays the program's path, so the loader searches its
    // own directory first; a host path is seeded into C:\WINDOWS as before.
    char program[260] = {};
    const char *source = tree_options.exe ? tree_options.exe : argv[1];
    if (tree_options.exe && SystemTreeHost::GuestPath(tree_options.exe)) {
        if (!SystemTreeHost::ReadGuest(tree_options.exe, exe)) {
            fprintf(stderr, "cannot read %s from the Win98 tree\n", tree_options.exe);
            return 12;
        }
        snprintf(program, sizeof program, "%s", tree_options.exe);
    } else {
        exe = load(source);
        snprintf(program, sizeof program, "%s", leaf(source));
        // The program also lives in C:\WINDOWS under its own name: the shell
        // resolves "explorer.exe,0" icon locations through SearchPath.
        if (!exe.empty()) {
            char guest[260];
            snprintf(guest, sizeof guest, "C:\\WINDOWS\\%s", leaf(source));
            Files::System().Seed(guest, exe.data(), exe.size());
        }
    }
    if (exe.empty() || font.empty()) {
        fprintf(stderr, "cannot read the program or the font\n");
        return 3;
    }
    auto *a = new X86::Application(logger);
    observed_app = a;
    probe_argc = argc;
    probe_argv = argv;
    probe_start = std::chrono::steady_clock::now();
    if (crash_report_dir) X86::Application::SetCrashHook(nullptr, on_crash);
    if (stub_missing)
        a->EnableExportStubs();
    a->trace_calls = trace_calls;
    a->trace_from = trace_from;
    for (const auto *name : native_modules)
        if (!a->AddNativeModule(name)) return 6;
        else { printf("PROBE_NATIVE_MODULE name="); hex_text(name); putchar('\n'); }
    if (stub_arguments && !load_stub_arguments(*a, stub_arguments))
        return 5;
    if (click_script && !script::load(click_script))
        return 8;
    if (current_directory)
    {
        const unsigned error = a->files.SetDirectory(current_directory);
        printf("PROBE_CURRENT_DIRECTORY error=%u path=", error);
        hex_text(current_directory);
        putchar('\n');
        if (error)
        {
            fprintf(stderr, "cannot enter %s, error %u\n", current_directory, error);
            return 14;
        }
    }
    const char *stop = "exited";
    // The rasterizer behind the Direct3D HAL of the display driver; without
    // it DirectDraw takes its emulation path.
    X86::Application::SetGpuBackend(HostGpu::Install());
    if (audio_capture) Sound::SetCapture(capture_frames, sizeof capture_frames / (2 * sizeof(short)));
    // The sockets of the host under the native WSOCK2.VXD.
    static HostNet::PosixSockets net;
    NetHost::Install(&net);
    // Without --command the program starts as Explorer starts it: the open
    // verb of exefile is "%1" %*, so a path with spaces arrives quoted. The
    // Flash projector read the unquoted rest of its own path as the URL of
    // its movie.
    std::string quoted;
    if (!command && strchr(program, ' ')) { quoted = std::string("\"") + program + "\""; command = quoted.c_str(); }
    const bool started = a->Start(exe.data(), exe.size(), font.data(), font.size(), program, command);
    // --desktop-depth N: the desktop runs 800x600 with N bits, as after a
    // restart with that mode in the registry.
    if (started && desktop_depth && !(User::SetDesktopMode(800, 600, desktop_depth) &&
                                      User::SetScreenMode(800, 600, desktop_depth)))
        return 2;
    // The CPU only exists after Start, so the observing hook is installed here.
    if (started && sampling::enabled && !sampling::install(*a))
        return 7;
    // Watches go into every process of the session, not only the root.
    X86::Application::child_started = [](X86::Application &child) {
        for (auto &watch : eip_watches)
            child.cpu->AddTrace(X86::TraceKind::Instruction, watch.address, watch.address, eip_watch_hook, &watch);
    };
    if (started) for (auto &watch : eip_watches) {
        if (a->cpu->AddTrace(X86::TraceKind::Instruction, watch.address, watch.address, eip_watch_hook, &watch) !=
            X86::CpuError::Ok) return 7;
    }
    if (started && count_instructions) {
        if (a->cpu->AddTrace(X86::TraceKind::Instruction, 1, 0, count_instruction, nullptr) != X86::CpuError::Ok) return 7;
    }
    const auto pump_start = std::chrono::steady_clock::now();
    const auto deadline = pump_start + std::chrono::seconds(time_limit);
    auto progress_deadline = pump_start + std::chrono::seconds(stall_limit);
    unsigned progress_calls = a->ApiCalls(), progress_callbacks = a->Callbacks();
    // A program that only starts another one and exits is a normal Win98
    // shape: PBRUSH.EXE finds MSPAINT.EXE, hands it the command line and
    // leaves, and the window belongs to the child. The
    // child keeps its own message loop, so the run is over when the last
    // process is gone, not when the first one is. Kernel::PumpApplication in
    // src/app_ui.cpp ends the SDL session by the same rule.
    const auto finished = [&] {
        const auto state = a->Status();
        if (printer_jobs && (!printer_sink || printer_sink->Completed() < printer_jobs) &&
            state != X86::Application::Failed)
            return false;
        return (state == X86::Application::Exited || state == X86::Application::Failed) &&
               !X86::Application::LiveChildren();
    };
    if (started)
        while (!finished() && std::chrono::steady_clock::now() < deadline)
        {
            if (call_budget && a->ApiCalls() >= call_budget) { stop = "call-budget"; break; }
            a->Pump();
            if (frame_rate) {
                static auto next = pump_start + std::chrono::seconds(1);
                static unsigned serial = User::FrameSerial(), presents = a->HalPresents(), second = 0;
                if (std::chrono::steady_clock::now() >= next) {
                    printf("PROBE_FRAMES second=%u frames=%u presents=%u calls=%u\n", ++second, User::FrameSerial() - serial,
                           a->HalPresents() - presents, a->ApiCalls());
                    fflush(stdout);
                    serial = User::FrameSerial();
                    presents = a->HalPresents();
                    next += std::chrono::seconds(1);
                }
            }
            // A wall-clock deadline alone makes a progressing guest fail when
            // other tests consume the host. A caller may also set a shorter
            // no-progress deadline. API calls and callbacks are guest work;
            // neither advances while a task is stuck in one instruction loop.
            if (stall_limit && (a->ApiCalls() != progress_calls || a->Callbacks() != progress_callbacks)) {
                progress_calls = a->ApiCalls();
                progress_callbacks = a->Callbacks();
                progress_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(stall_limit);
            } else if (stall_limit && std::chrono::steady_clock::now() >= progress_deadline) {
                stop = "stall-limit";
                break;
            }
            if (sampling::enabled) {
                // Restrict the histogram to the phase in which the guest runs
                // but dispatches nothing: two seconds without a new API call.
                static unsigned last_calls = 0;
                static auto since = std::chrono::steady_clock::now();
                if (a->ApiCalls() != last_calls) { last_calls = a->ApiCalls(); since = std::chrono::steady_clock::now(); }
                else if (std::chrono::steady_clock::now() - since >= std::chrono::seconds(2)) {
                    sampling::restart(last_calls);
                    static auto dumped = std::chrono::steady_clock::time_point{};
                    if (std::chrono::steady_clock::now() - dumped >= std::chrono::seconds(1)) {
                        dumped = std::chrono::steady_clock::now();
                        sampling::stall_dump(*a);
                    }
                }
            }
            if (const auto dialog = visible_dialog(accept_caption)) {
                // Click OK, the same gesture tools/test_explorer_ui.py sends
                // through the emulated USB mouse in the Pi kernel, so both
                // runtimes drive the dialog the same way. Creation and the
                // first paint need to finish first; this is observation
                // scheduling, not a callback or a guest-state edit. A dialog
                // without a visible IDOK still takes the default-button key.
                if (accepting != dialog) { accepting = dialog; accepting_since = std::chrono::steady_clock::now(); }
                if (std::chrono::steady_clock::now() - accepting_since < std::chrono::milliseconds(200)) continue;
                if (!screenshot(dialog_screenshot)) return 4;
                // IDOK first, then IDCANCEL. A finished MB_OK box answers only
                // the second one: USER renumbers its single button during
                // WM_INITDIALOG so that Escape and the close command have a
                // target, and USER repairs the public result back to IDOK.
                // It is the same button the Pi test clicks.
                auto button = User::GetDlgItem(dialog, 1);
                if (!button) button = User::GetDlgItem(dialog, 2);
                Gdi::RECT bounds{};
                if (button && User::IsVisible(button) && User::IsEnabled(button) &&
                    User::GetWindowRect(button, &bounds)) {
                    const int x = (bounds.left + bounds.right) / 2, y = (bounds.top + bounds.bottom) / 2;
                    printf("PROBE_ACCEPT hwnd=%u button=%u x=%d y=%d caption=", dialog, button, x, y);
                    hex_text(accept_caption); putchar('\n');
                    a->MouseButtons(x, y, 0);
                    a->MouseButtons(x, y, 1);
                    a->MouseButtons(x, y, 0);
                } else {
                    printf("PROBE_ACCEPT hwnd=%u button=0 x=0 y=0 caption=", dialog);
                    hex_text(accept_caption); putchar('\n');
                    a->RawKey(13, 0, true);
                    a->RawKey(13, 0, false);
                }
                accept_caption = nullptr; // One explicitly selected dialog per observation.
                continue;
            }
            if (cancelling && !User::IsWindow(cancelling)) {
                printf("PROBE_CANCEL_CLOSED hwnd=%u calls=%u children=%u\n", cancelling, a->ApiCalls(), a->LiveChildren());
                cancelling = 0;
            }
            bool answered_now = false;
            for (unsigned k = 0; k < answer_count && !answered_now; ++k)
            if (const auto dialog = visible_dialog(answer_captions[k])) {
                const unsigned answer_id = answer_ids[k];
                const auto button = User::GetDlgItem(dialog, answer_id);
                Gdi::RECT bounds{};
                if (dialog != answered && button && User::IsVisible(button) && User::IsEnabled(button) &&
                    User::GetWindowRect(button, &bounds)) {
                    const int x = (bounds.left + bounds.right) / 2, y = (bounds.top + bounds.bottom) / 2;
                    printf("PROBE_ANSWER hwnd=%u button=%u id=%u x=%d y=%d\n", dialog, button, answer_id, x, y);
                    a->MouseButtons(x, y, 0);
                    a->MouseButtons(x, y, 1);
                    a->MouseButtons(x, y, 0);
                    answered = dialog;
                    answered_now = true;
                }
            }
            if (answered_now)
                continue;
            if (const auto dialog = visible_dialog(cancel_caption)) {
                // Give creation and the first paint time to complete. This is
                // observation scheduling, not a callback or guest-state edit.
                if (candidate != dialog) { candidate = dialog; candidate_since = std::chrono::steady_clock::now(); }
                const auto button = User::GetDlgItem(dialog, 2);
                Gdi::RECT bounds{};
                if (std::chrono::steady_clock::now() - candidate_since >= std::chrono::milliseconds(200) &&
                    button && User::IsVisible(button) && User::IsEnabled(button) && User::GetWindowRect(button, &bounds)) {
                    if (!screenshot(cancel_screenshot)) return 4;
                    const int x = (bounds.left + bounds.right) / 2, y = (bounds.top + bounds.bottom) / 2;
                    printf("PROBE_CANCEL hwnd=%u button=%u x=%d y=%d caption=", dialog, button, x, y);
                    hex_text(cancel_caption); putchar('\n');
                    a->MouseButtons(x, y, 0);
                    a->MouseButtons(x, y, 1);
                    a->MouseButtons(x, y, 0);
                    cancelling = dialog;
                    cancel_caption = nullptr; // One explicit Cancel button click.
                    continue;
                }
            }
            // A scripted input sequence keeps the observation going for the
            // same reason a live child does: the run is not over while there
            // are packets left to send.
            if (script::stop_requested) { stop = "script"; break; }
            if (script::advance(*a))
                continue;
            // A live child process keeps the observation going: the shell
            // waits on it in MsgWaitForMultipleObjects, so its own idle is not
            // the end of the run. Without this the outcome depends on which
            // poll notices the closed dialog first.
            // With a live child the root's own wait says nothing: the shell
            // parks while its children run, and a launcher has already left.
            // The run is parked when every process that is still there is.
            // --session-idle-stop: the root is the shell and stays, and its Run
            // key left resident programs (SYSTRAY, TASKMON); the session is
            // parked when the shell and every one of them wait for input.
            const bool parked = X86::Application::LiveChildren()
                ? ((a->Status() == X86::Application::Exited ||
                    (session_idle_stop && a->WaitingForInput())) &&
                   X86::Application::SessionWaitingForInput())
                : a->WaitingForInput();
            const bool printing = printer_jobs && (!printer_sink || printer_sink->Completed() < printer_jobs);
            if (parked && !cancel_caption && !cancelling && !no_idle_stop && !printing)
            {
                stop = "waiting-for-input";
                break;
            }
            if (a->Status() == X86::Application::Idle)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    const auto pump_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - pump_start).count();
    if (started && a->Status() == X86::Application::Failed) stop = "failed";
    else if (started && std::chrono::steady_clock::now() >= deadline && !finished())
        stop = "time-limit";
    // A run that hits the time limit is treated as a hang:
    // it gets the same report as the reboot button, because that report is what
    // says which thread waits on what. A Fail already wrote one through the
    // crash hook, so this only covers the run that never stopped.
    if (started && crash_report_dir && !strcmp(stop, "time-limit"))
        write_crash_report(*a, "zeitueberschreitung");
    if (audio_capture) {
        const unsigned frames = Sound::Captured();
        FILE *file = fopen(audio_capture, "wb");
        const bool written = file && fwrite(capture_frames, 4, frames, file) == frames;
        if (file) fclose(file);
        printf("PROBE_AUDIO frames=%u written=%u\n", frames, written ? 1u : 0u);
    }
    printf("PROBE started=%u state=%u code=%u calls=%u callbacks=%u windows=%u error=%s\n", started,
           a->Status(), a->ExitCode(), a->ApiCalls(), a->Callbacks(), User::LiveWindows(), a->Error());
    printf("PROBE_STOP reason=%s calls=%u\n", started ? stop : "not-started", a->ApiCalls());
    // Children that ended in Fail: the shell only writes a box for them, so
    // the probe names them.
    for (unsigned i = 0; const char *failed = X86::Application::FailedChild(i); ++i)
        printf("PROBE_CHILD_FAILED %s\n", failed);
    printf("PROBE_TIME pump_us=%lld instructions=%llu counted=%u\n", (long long)pump_us, counted_instructions, count_instructions ? 1u : 0u);
    printf("PROBE_GATES fast=%llu slow=%llu withdrawn=%llu guest=%llu\n", a->fast_gate_calls, a->slow_gate_calls,
           a->fast_gate_withdrawn, a->guest_gate_calls);
    if (api_counts)
    {
        std::vector<unsigned> entries;
        for (unsigned i = 0; i < a->import_count; ++i) if (a->imports[i].calls) entries.push_back(i);
        std::sort(entries.begin(), entries.end(), [&](unsigned left, unsigned right)
                  { return a->imports[left].calls > a->imports[right].calls; });
        for (unsigned i = 0; i < entries.size() && i < 32; ++i)
        {
            const auto &entry = a->imports[entries[i]];
            printf("PROBE_API_COUNT host_calls=%u api=%s!%s\n", entry.calls, entry.dll, entry.name);
        }
    }
    unicorn_counters();
    if (started) write_memory_dumps(*a);
    if (started) sampling::stall_dump(*a); // per-thread block state at the end of every run
    sampling::report(*a);
    stub_report(*a);
    census_report(*a);
    for (unsigned i = 0; i < a->ThunkConnectionCount(); ++i) {
        const auto &c = a->ThunkConnectionAt(i);
        printf("PROBE_FLATTHUNK dll16="); hex_text(c.dll16);
        printf(" dll32="); hex_text(c.dll32);
        printf(" name="); hex_text(c.name);
        printf(" block=%08x signature=%08x checksum=%08x instance=%08x trap=%08x connected=%u\n",
               c.block, c.signature, c.checksum, c.instance, c.trap, c.connected);
    }
    printf("PROBE_FLATTHUNK_TRANSITIONS count=%u\n", a->ThunkTransitions());
    // The text of a console program's screen buffer, rows up to the last one
    // that holds anything, hex encoded like the window texts.
    {
        char row_text[512];
        unsigned last = 0;
        for (unsigned row = 0; a->ConsoleText(row, row_text, sizeof row_text); ++row)
            if (row_text[0]) last = row + 1;
        for (unsigned row = 0; row < last && a->ConsoleText(row, row_text, sizeof row_text); ++row) {
            printf("PROBE_CONSOLE row=%u text=", row); hex_text(row_text); putchar('\n');
        }
    }
    snapshot_delay_failure(*a);
    if (started)
        snapshot_windows(); // Native read-only inspection; no window callbacks are entered.
    file_report();
    if (pointer_proof_x >= 0 && pointer_proof_y >= 0)
        pointer_overlay_proof(*a, pointer_proof_x, pointer_proof_y);
    if (!screenshot(final_screenshot)) return 4;
    X86::Application::SetCrashHook(nullptr, nullptr);
    observed_app = nullptr;
    delete a;
    return 0;
}
