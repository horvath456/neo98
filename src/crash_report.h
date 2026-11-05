// SPDX-License-Identifier: GPL-2.0-or-later
// Agent-readable crash report: data model, the always-on API ring buffer and
// the two formatters. Nothing here touches a host file system, an exception or
// an iostream; the caller supplies a Sink and the report core writes bytes into
// it. tools/crash_report_files.h is the file sink the host and the probe use.
//
#pragma once
namespace CrashReport
{
// One dispatched guest API call. The name pointers are the Import record's own
// storage, which never moves (x86_app.h, ImportTable), so recording a call
// costs one struct store and no allocation.
struct ApiCall
{
    const char *dll = nullptr, *name = nullptr;
    unsigned sequence = 0, thread = 0, ret = 0, value = 0;
    unsigned args[4] = {};
    unsigned char argc = 0, returned = 0;
};
// A fixed per-process ring. It runs always, also without --trace-calls: the
// report of a hang is only useful with the calls that led into it.
class ApiRing
{
  public:
    static constexpr unsigned Capacity = 1024; // power of two
    // Records the entry of a call and returns its token for Finish.
    // Padded callers provide four readable words, with zeros past count.
    template<bool Padded = false>
    unsigned Begin(const char *dll, const char *name, unsigned thread, unsigned ret,
                   const unsigned *args, unsigned count)
    {
        const unsigned token = next++;
        ApiCall &c = entries[token & (Capacity - 1)];
        c.dll = dll;
        c.name = name;
        c.sequence = token;
        c.thread = thread;
        c.ret = ret;
        c.value = 0;
        c.returned = 0;
        c.argc = (unsigned char)(count < 4 ? count : 4);
        for (unsigned i = 0; i < 4; ++i) c.args[i] = Padded || i < count ? args[i] : 0;
        return token;
    }
    // Ignored when the ring wrapped past the entry; a nested run of 1.024 calls
    // inside one handler is the only way that happens.
    void Finish(unsigned token, unsigned value)
    {
        ApiCall &c = entries[token & (Capacity - 1)];
        if (c.sequence != token) return;
        c.value = value;
        c.returned = 1;
    }
    unsigned Recorded() const { return next; }
    // Newest first: Recent(0) is the last call recorded.
    const ApiCall *Recent(unsigned back) const
    {
        if (back >= Capacity || back >= next) return nullptr;
        const ApiCall &c = entries[(next - 1 - back) & (Capacity - 1)];
        return c.name ? &c : nullptr;
    }

  private:
    ApiCall entries[Capacity];
    unsigned next = 0;
};

// Byte sink. The core never opens a file; a host implements this over stdio.
class Sink
{
  public:
    virtual bool Write(const void *data, unsigned length) = 0;

  protected:
    ~Sink() = default;
};

struct ModuleInfo
{
    char path[260] = {};
    unsigned base = 0, size = 0, handle = 0;
    unsigned char original = 0; // 1: original x86 PE under Unicorn, 0: native provider
};
// A guest address with the module it falls into, already resolved.
struct Address
{
    unsigned value = 0;
    char text[96] = {}; // "MODULE+0x1234 (Export+0x8)" or "-"
    unsigned char code = 0;
    // Only used for the call chain: 1 when a call instruction ends exactly in
    // front of the address, 0 when the bytes before it could not be read and
    // the link stays a guess.
    unsigned char certain = 0;
};
struct ThreadInfo
{
    unsigned index = 0, id = 0;
    unsigned eip = 0, esp = 0, ebp = 0, eax = 0, ebx = 0, ecx = 0, edx = 0, esi = 0, edi = 0;
    unsigned eflags = 0, cs = 0, ds = 0, es = 0, fs = 0, gs = 0, ss = 0, teb = 0;
    unsigned block = 0, wait_handle = 0, depth = 0, last_error = 0, pending = 0;
    unsigned message_window = 0, message_minimum = 0, message_maximum = 0;
    unsigned long long deadline = 0;
    unsigned multiwait_count = 0, multiwait[8] = {}, queue_pending = 0, apc_count = 0;
    unsigned char done = 0, suspended = 0, waiting = 0, runnable = 0, alertable = 0, current = 0;
    char block_text[32] = {};   // "message-wait", "object-wait",...
    char wait_text[256] = {};   // what it blocks on, with object state
    Address eip_address;
    Address gate;               // set when EIP rests in an API gate
    unsigned stack_count = 0;
    Address stack[64];
    unsigned return_count = 0;
    Address returns[12];
};
struct ProcessInfo
{
    unsigned id = 0, state = 0;
    char name[128] = {}, command[512] = {}, state_text[16] = {}, error[256] = {};
    unsigned api_calls = 0, callbacks = 0, exit_code = 0, depth = 0, current_thread = 0;
    unsigned image_base = 0, image_size = 0, import_count = 0, live_threads = 0;
    unsigned char child = 0;
    unsigned module_count = 0;
    // The loader table of a process is larger; the report keeps the first 64,
    // which covers every graph seen so far including the shell with its web view.
    static constexpr unsigned ModuleCapacity = 64;
    ModuleInfo modules[ModuleCapacity];
    unsigned thread_count = 0;
    // The first 32 threads of a process (Application::ThreadCapacity is larger).
    static constexpr unsigned ThreadCapacity = 32;
    ThreadInfo threads[ThreadCapacity];
    const ApiRing *ring = nullptr; // read, never copied: the report is written synchronously
};
struct WindowInfo
{
    unsigned hwnd = 0, parent = 0, owner = 0, style = 0, extended = 0;
    int left = 0, top = 0, right = 0, bottom = 0;
    unsigned process = 0, thread = 0;
    unsigned char visible = 0, enabled = 0;
    char klass[64] = {};
    char text[128] = {};
};
struct Report
{
    // One slot for the root and one for every child slot of Application
    // (ChildApplication takes 16). Four slots hid a living process from the
    // report of 21 September 2026 and sent a triage the wrong way.
    static constexpr unsigned ProcessCapacity = 17;
    static constexpr unsigned WindowCapacity = 192;
    static constexpr unsigned HistoryDepth = 200; // API calls per reported thread
    char reason[192] = {};
    char timestamp[32] = {};
    char revision[64] = {};
    char build[160] = {};
    char host[64] = {};
    char command[1024] = {};
    unsigned long long uptime_ms = 0;
    // Hang observation, filled by the host; the core never measures time.
    unsigned char hang = 0;
    unsigned hang_ms = 0;
    char hang_text[256] = {};
    // The thread the report points at first, as indices into processes.
    unsigned suspect_process = 0xffffffffu, suspect_thread = 0xffffffffu;
    char suspect_text[384] = {};
    unsigned focus = 0, active = 0, capture = 0, foreground = 0, live_windows = 0;
    unsigned gdi_objects = 0;
    unsigned window_count = 0;
    WindowInfo windows[WindowCapacity];
    unsigned process_count = 0;
    // Processes the collector could not fit; the report says so when non-zero.
    unsigned processes_omitted = 0;
    ProcessInfo processes[ProcessCapacity];
};

// report.md, most useful facts first.
bool FormatMarkdown(const Report &, Sink &);
// report.json, the same data.
bool FormatJson(const Report &, Sink &);
// screenshot.png from the 32-bit BGRA compositor surface. Deflate "stored"
// blocks only, so no compression library is needed and an agent can open the
// file with any reader.
bool WritePng(Sink &, const unsigned char *bgra, unsigned width, unsigned height);
// Directory leaf for a reason, lower case, non-alphanumerics folded to '-'.
void ReasonSlug(const char *reason, char *out, unsigned capacity);
} // namespace CrashReport
