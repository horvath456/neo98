// SPDX-License-Identifier: GPL-2.0-or-later
// Formatting half of the crash report: Markdown, JSON and a PNG screenshot.
// No file access, no allocation, no exceptions; everything goes into a Sink.
#include "crash_report.h"
// C headers, not their <c...> forms: the Circle build has newlib, which has no
// C++ standard library headers.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace CrashReport
{
namespace
{
// Buffered printer over a Sink. One 4 KiB staging buffer, flushed when full.
class Printer
{
  public:
    explicit Printer(Sink &s) : sink(s) {}
    ~Printer() { Flush(); }
    bool Failed() const { return failed; }
    bool Flush()
    {
        if (!used) return !failed;
        if (!sink.Write(buffer, used)) failed = true;
        used = 0;
        return !failed;
    }
    void Raw(const char *text, unsigned length)
    {
        while (length)
        {
            if (used == sizeof buffer && !Flush()) return;
            unsigned room = (unsigned)sizeof buffer - used, take = length < room ? length : room;
            memcpy(buffer + used, text, take);
            used += take;
            text += take;
            length -= take;
        }
    }
    void Raw(const char *text) { Raw(text, (unsigned)strlen(text)); }
    __attribute__((format(printf, 2, 3))) void Print(const char *format, ...)
    {
        char line[2048];
        va_list args;
        va_start(args, format);
        const int n = vsnprintf(line, sizeof line, format, args);
        va_end(args);
        if (n > 0) Raw(line, (unsigned)n < sizeof line ? (unsigned)n : (unsigned)sizeof line - 1);
    }
    // JSON string body with the six escapes the standard requires.
    void Json(const char *text)
    {
        Raw("\"");
        for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; ++p)
        {
            switch (*p)
            {
            case '"': Raw("\\\""); break;
            case '\\': Raw("\\\\"); break;
            case '\n': Raw("\\n"); break;
            case '\r': Raw("\\r"); break;
            case '\t': Raw("\\t"); break;
            default:
                if (*p < 0x20 || *p > 0x7e) Print("\\u%04x", *p);
                else Raw((const char *)p, 1);
            }
        }
        Raw("\"");
    }

  private:
    Sink &sink;
    char buffer[4096];
    unsigned used = 0;
    bool failed = false;
};

// The history the report shows for one thread: the newest HistoryDepth entries
// of the process ring that belong to it, oldest first.
unsigned Collect(const ApiRing *ring, unsigned thread, const ApiCall **out, unsigned capacity)
{
    if (!ring) return 0;
    unsigned count = 0;
    for (unsigned back = 0; back < ApiRing::Capacity && count < capacity; ++back)
    {
        const ApiCall *call = ring->Recent(back);
        if (!call) break;
        if (call->thread != thread) continue;
        out[count++] = call;
    }
    for (unsigned i = 0, n = count; i + 1 < n; ++i, --n) // reverse in place
    {
        const ApiCall *swap = out[i];
        out[i] = out[n - 1];
        out[n - 1] = swap;
    }
    return count;
}
void PrintCall(Printer &p, const ApiCall &c, bool json)
{
    if (json)
    {
        p.Print("{\"sequence\":%u,\"thread\":%u,\"dll\":", c.sequence, c.thread);
        p.Json(c.dll ? c.dll : "");
        p.Raw(",\"name\":");
        p.Json(c.name ? c.name : "");
        p.Print(",\"return_address\":%u,\"args\":[", c.ret);
        for (unsigned i = 0; i < 4; ++i) p.Print("%s%u", i ? "," : "", c.args[i]);
        p.Print("],\"argc\":%u,\"returned\":%s,\"value\":%u}", c.argc,
                c.returned ? "true" : "false", c.value);
        return;
    }
    p.Print("%6u  %-14s %-32s ret=%08x args=%08x,%08x,%08x,%08x", c.sequence, c.dll ? c.dll : "?",
            c.name ? c.name : "?", c.ret, c.args[0], c.args[1], c.args[2], c.args[3]);
    if (c.returned) p.Print(" -> %08x\n", c.value);
    else p.Raw(" -> (no result, call still active)\n");
}
} // namespace

void ReasonSlug(const char *reason, char *out, unsigned capacity)
{
    if (!capacity) return;
    unsigned n = 0;
    bool dash = false;
    for (const char *p = reason ? reason : ""; *p && n + 1 < capacity && n < 48; ++p)
    {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        const bool word = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!word)
        {
            dash = n != 0;
            continue;
        }
        if (dash && n + 2 < capacity) out[n++] = '-';
        dash = false;
        out[n++] = c;
    }
    if (!n && capacity > 1) out[n++] = 'x';
    out[n] = 0;
}

bool FormatMarkdown(const Report &r, Sink &sink)
{
    Printer p(sink);
    p.Print("# Neo98 crash report\n\nReason: %s\n", r.reason);
    p.Print("Time: %s\nRevision: %s\nHost: %s\nUptime: %llu ms\n", r.timestamp, r.revision,
            r.host, r.uptime_ms);
    p.Print("Build: %s\nCommand line: %s\n", r.build, r.command);
    if (r.hang)
        p.Print("\nUnresponsive: yes, for %u ms. %s\n", r.hang_ms, r.hang_text);
    else
        p.Raw("\nUnresponsive: no (or not measured).\n");

    const ProcessInfo *suspect_process = r.suspect_process < r.process_count
                                             ? &r.processes[r.suspect_process] : nullptr;
    const ThreadInfo *suspect = suspect_process && r.suspect_thread < suspect_process->thread_count
                                    ? &suspect_process->threads[r.suspect_thread] : nullptr;
    p.Raw("\n## Suspected cause\n\n");
    if (!suspect)
        p.Raw("No suspect thread found. All processes are listed below.\n");
    else
    {
        p.Print("Process %u (%s), thread %u (index %u).\n", suspect_process->id,
                suspect_process->name, suspect->id, suspect->index);
        p.Print("Reason: %s\n", r.suspect_text);
        p.Print("EIP %08x = %s\n", suspect->eip, suspect->eip_address.text);
        if (suspect->gate.value)
            p.Print("The thread is at the entry to %s; the call has not "
                    "returned yet.\n", suspect->gate.text);
        p.Print("Wait state: %s (block code %u). %s\n", suspect->block_text, suspect->block,
                suspect->wait_text);
        if (suspect->return_count)
        {
            p.Raw("Call chain from the stack (top first; each entry follows a call instruction "
                  "unless marked uncertain):\n");
            for (unsigned i = 0; i < suspect->return_count; ++i)
                p.Print("  %08x  %s%s\n", suspect->returns[i].value, suspect->returns[i].text,
                        suspect->returns[i].certain ? "" : "  (uncertain, preceding bytes unreadable)");
        }
        const ApiCall *history[Report::HistoryDepth];
        const unsigned count = Collect(suspect_process->ring, suspect->id, history,
                                       Report::HistoryDepth);
        p.Print("\n### Last %u API calls for this thread (oldest first)\n\n```\n", count);
        for (unsigned i = 0; i < count; ++i) PrintCall(p, *history[i], false);
        if (!count) p.Raw("(none)\n");
        p.Raw("```\n");
    }

    p.Raw("\n## Processes\n");
    if (r.processes_omitted)
        p.Print("\nINCOMPLETE: %u additional processes did not fit in this report. A missing window or "
                "recipient may belong to one of them.\n", r.processes_omitted);
    for (unsigned i = 0; i < r.process_count; ++i)
    {
        const auto &proc = r.processes[i];
        p.Print("\n### %s, process %u%s\n\n", proc.name, proc.id, proc.child ? " (child)" : "");
        p.Print("State: %s\n", proc.state_text);
        if (proc.error[0]) p.Print("Error: %s\n", proc.error);
        p.Print("Command line: %s\n", proc.command);
        p.Print("API calls: %u, callbacks: %u, imports: %u, exit code: %u, threads: %u\n",
                proc.api_calls, proc.callbacks, proc.import_count, proc.exit_code,
                proc.live_threads);
        p.Print("Image: base %08x, size %08x\n", proc.image_base, proc.image_size);
        p.Raw("\nModules:\n\n```\n");
        for (unsigned m = 0; m < proc.module_count; ++m)
        {
            const auto &mod = proc.modules[m];
            p.Print("%-9s base=%08x size=%08x handle=%08x %s\n",
                    mod.original ? "original" : "native", mod.base, mod.size, mod.handle, mod.path);
        }
        if (!proc.module_count) p.Raw("(none)\n");
        p.Raw("```\n");
        for (unsigned t = 0; t < proc.thread_count; ++t)
        {
            const auto &th = proc.threads[t];
            p.Print("\n#### Thread %u (Index %u)%s\n\n", th.id, th.index,
                    th.current ? ", last running" : "");
            p.Print("Block state: %s (code %u), runnable=%u, done=%u, suspended=%u, "
                    "waiting=%u, alertable=%u\n",
                    th.block_text, th.block, th.runnable, th.done, th.suspended, th.waiting,
                    th.alertable);
            p.Print("Waiting for: %s\n", th.wait_text);
            p.Print("EIP %08x = %s\n", th.eip, th.eip_address.text);
            if (th.gate.value) p.Print("API-Gate: %s\n", th.gate.text);
            p.Print("eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x\n", th.eax, th.ebx,
                    th.ecx, th.edx, th.esi, th.edi);
            p.Print("esp=%08x ebp=%08x eflags=%08x\n", th.esp, th.ebp, th.eflags);
            p.Print("cs=%04x ds=%04x es=%04x ss=%04x fs=%04x gs=%04x teb=%08x\n", th.cs, th.ds,
                    th.es, th.ss, th.fs, th.gs, th.teb);
            p.Print("Depth %u, LastError %u, message pending=%u, %u APCs\n", th.depth,
                    th.last_error, th.queue_pending, th.apc_count);
            if (th.stack_count)
            {
                p.Print("\nStack from esp=%08x:\n\n```\n", th.esp);
                for (unsigned s = 0; s < th.stack_count; ++s)
                    p.Print("%08x  %08x  %s\n", th.esp + s * 4, th.stack[s].value,
                            th.stack[s].text);
                p.Raw("```\n");
            }
            const ApiCall *history[Report::HistoryDepth];
            const unsigned count = Collect(proc.ring, th.id, history, Report::HistoryDepth);
            p.Print("\nLast %u API calls (oldest first):\n\n```\n", count);
            for (unsigned c = 0; c < count; ++c) PrintCall(p, *history[c], false);
            if (!count) p.Raw("(none)\n");
            p.Raw("```\n");
        }
    }

    p.Raw("\n## Windows in Z order\n\n```\n");
    for (unsigned i = 0; i < r.window_count; ++i)
    {
        const auto &w = r.windows[i];
        p.Print("hwnd=%-5u parent=%-5u owner=%-5u pid=%-5u tid=%-5u style=%08x ex=%08x "
                "rect=%d,%d,%d,%d visible=%u enabled=%u class=%s text=%s\n",
                w.hwnd, w.parent, w.owner, w.process, w.thread, w.style, w.extended, w.left, w.top,
                w.right, w.bottom, w.visible, w.enabled, w.klass, w.text);
    }
    if (!r.window_count) p.Raw("(none)\n");
    p.Print("```\n\nFocus %u, active %u, foreground %u, mouse captured by %u, live windows %u, "
            "GDI objects %u.\n",
            r.focus, r.active, r.foreground, r.capture, r.live_windows, r.gdi_objects);
    p.Raw("\nAddresses refer to this run. Uncertain stack entries may not be return addresses.\n");
    return p.Flush();
}

bool FormatJson(const Report &r, Sink &sink)
{
    Printer p(sink);
    p.Raw("{\n  \"reason\": ");
    p.Json(r.reason);
    p.Raw(",\n  \"timestamp\": ");
    p.Json(r.timestamp);
    p.Raw(",\n  \"revision\": ");
    p.Json(r.revision);
    p.Raw(",\n  \"host\": ");
    p.Json(r.host);
    p.Raw(",\n  \"build\": ");
    p.Json(r.build);
    p.Raw(",\n  \"command\": ");
    p.Json(r.command);
    p.Print(",\n  \"uptime_ms\": %llu", r.uptime_ms);
    p.Print(",\n  \"hang\": %s,\n  \"hang_ms\": %u,\n  \"hang_text\": ", r.hang ? "true" : "false",
            r.hang_ms);
    p.Json(r.hang_text);
    p.Raw(",\n  \"suspect\": {");
    if (r.suspect_process < r.process_count)
    {
        const auto &proc = r.processes[r.suspect_process];
        p.Print("\"process_index\": %u, \"process_id\": %u, \"thread_index\": %u",
                r.suspect_process, proc.id, r.suspect_thread);
        if (r.suspect_thread < proc.thread_count)
        {
            const auto &th = proc.threads[r.suspect_thread];
            p.Print(", \"thread_id\": %u, \"block\": %u, \"block_text\": ", th.id, th.block);
            p.Json(th.block_text);
            p.Print(", \"eip\": %u, \"eip_text\": ", th.eip);
            p.Json(th.eip_address.text);
        }
        p.Raw(", \"why\": ");
        p.Json(r.suspect_text);
    }
    p.Print("},\n  \"focus\": %u,\n  \"active\": %u,\n  \"foreground\": %u,\n  \"capture\": %u,\n"
            "  \"live_windows\": %u,\n  \"gdi_objects\": %u,\n  \"process_capacity\": %u,\n"
            "  \"processes_omitted\": %u",
            r.focus, r.active, r.foreground, r.capture, r.live_windows, r.gdi_objects,
            Report::ProcessCapacity, r.processes_omitted);

    p.Raw(",\n  \"processes\": [");
    for (unsigned i = 0; i < r.process_count; ++i)
    {
        const auto &proc = r.processes[i];
        p.Print("%s\n    {\"id\": %u, \"child\": %s, \"name\": ", i ? "," : "", proc.id,
                proc.child ? "true" : "false");
        p.Json(proc.name);
        p.Raw(", \"command\": ");
        p.Json(proc.command);
        p.Print(", \"state\": %u, \"state_text\": ", proc.state);
        p.Json(proc.state_text);
        p.Raw(", \"error\": ");
        p.Json(proc.error);
        p.Print(", \"api_calls\": %u, \"callbacks\": %u, \"imports\": %u, \"exit_code\": %u,"
                " \"image_base\": %u, \"image_size\": %u, \"live_threads\": %u",
                proc.api_calls, proc.callbacks, proc.import_count, proc.exit_code, proc.image_base,
                proc.image_size, proc.live_threads);
        p.Raw(",\n     \"modules\": [");
        for (unsigned m = 0; m < proc.module_count; ++m)
        {
            const auto &mod = proc.modules[m];
            p.Print("%s{\"path\": ", m ? ", " : "");
            p.Json(mod.path);
            p.Print(", \"base\": %u, \"size\": %u, \"handle\": %u, \"original\": %s}", mod.base,
                    mod.size, mod.handle, mod.original ? "true" : "false");
        }
        p.Raw("],\n     \"threads\": [");
        for (unsigned t = 0; t < proc.thread_count; ++t)
        {
            const auto &th = proc.threads[t];
            p.Print("%s\n      {\"index\": %u, \"id\": %u, \"block\": %u, \"block_text\": ",
                    t ? "," : "", th.index, th.id, th.block);
            p.Json(th.block_text);
            p.Raw(", \"wait_text\": ");
            p.Json(th.wait_text);
            p.Print(", \"wait_handle\": %u, \"deadline\": %llu, \"done\": %s, \"suspended\": %u,"
                    " \"waiting\": %s, \"runnable\": %s, \"alertable\": %s, \"current\": %s",
                    th.wait_handle, th.deadline, th.done ? "true" : "false", th.suspended,
                    th.waiting ? "true" : "false", th.runnable ? "true" : "false",
                    th.alertable ? "true" : "false", th.current ? "true" : "false");
            p.Print(", \"eip\": %u, \"eip_text\": ", th.eip);
            p.Json(th.eip_address.text);
            p.Raw(", \"gate\": ");
            p.Json(th.gate.text);
            p.Print(", \"registers\": {\"eax\": %u, \"ebx\": %u, \"ecx\": %u, \"edx\": %u,"
                    " \"esi\": %u, \"edi\": %u, \"esp\": %u, \"ebp\": %u, \"eflags\": %u,"
                    " \"cs\": %u, \"ds\": %u, \"es\": %u, \"ss\": %u, \"fs\": %u, \"gs\": %u,"
                    " \"teb\": %u}",
                    th.eax, th.ebx, th.ecx, th.edx, th.esi, th.edi, th.esp, th.ebp, th.eflags,
                    th.cs, th.ds, th.es, th.ss, th.fs, th.gs, th.teb);
            p.Print(", \"depth\": %u, \"last_error\": %u, \"queue_pending\": %u, \"apcs\": %u",
                    th.depth, th.last_error, th.queue_pending, th.apc_count);
            p.Raw(", \"multiwait\": [");
            for (unsigned n = 0; n < th.multiwait_count && n < 8; ++n)
                p.Print("%s%u", n ? ", " : "", th.multiwait[n]);
            p.Raw("], \"stack\": [");
            for (unsigned s = 0; s < th.stack_count; ++s)
            {
                p.Print("%s{\"address\": %u, \"value\": %u, \"text\": ", s ? ", " : "",
                        th.esp + s * 4, th.stack[s].value);
                p.Json(th.stack[s].text);
                p.Raw("}");
            }
            p.Raw("], \"returns\": [");
            for (unsigned s = 0; s < th.return_count; ++s)
            {
                p.Print("%s{\"value\": %u, \"text\": ", s ? ", " : "", th.returns[s].value);
                p.Json(th.returns[s].text);
                p.Print(", \"certain\": %s}", th.returns[s].certain ? "true" : "false");
            }
            p.Raw("], \"api_calls\": [");
            const ApiCall *history[Report::HistoryDepth];
            const unsigned count = Collect(proc.ring, th.id, history, Report::HistoryDepth);
            for (unsigned c = 0; c < count; ++c)
            {
                if (c) p.Raw(", ");
                PrintCall(p, *history[c], true);
            }
            p.Raw("]}");
        }
        p.Raw("]}");
    }
    p.Raw("\n  ],\n  \"windows\": [");
    for (unsigned i = 0; i < r.window_count; ++i)
    {
        const auto &w = r.windows[i];
        p.Print("%s\n    {\"z\": %u, \"hwnd\": %u, \"parent\": %u, \"owner\": %u, \"process\": %u,"
                " \"thread\": %u, \"style\": %u, \"extended\": %u, \"visible\": %s,"
                " \"enabled\": %s, \"rect\": [%d, %d, %d, %d], \"class\": ",
                i ? "," : "", i, w.hwnd, w.parent, w.owner, w.process, w.thread, w.style,
                w.extended, w.visible ? "true" : "false", w.enabled ? "true" : "false", w.left,
                w.top, w.right, w.bottom);
        p.Json(w.klass);
        p.Raw(", \"text\": ");
        p.Json(w.text);
        p.Raw("}");
    }
    p.Raw("\n  ]\n}\n");
    return p.Flush();
}

// -- PNG ---------------------------------------------------------------------
namespace
{
unsigned Crc32(unsigned crc, const unsigned char *data, unsigned length)
{
    static unsigned table[256];
    static bool ready = false;
    if (!ready)
    {
        for (unsigned i = 0; i < 256; ++i)
        {
            unsigned c = i;
            for (unsigned n = 0; n < 8; ++n) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    for (unsigned i = 0; i < length; ++i) crc = table[(crc ^ data[i]) & 255] ^ (crc >> 8);
    return crc;
}
void Be32(unsigned char *out, unsigned value)
{
    out[0] = (unsigned char)(value >> 24);
    out[1] = (unsigned char)(value >> 16);
    out[2] = (unsigned char)(value >> 8);
    out[3] = (unsigned char)value;
}
// Chunk with its own CRC. The data is handed in one piece; the image chunk is
// written through PngStream below instead, so nothing large is buffered.
bool Chunk(Sink &sink, const char *type, const unsigned char *data, unsigned length)
{
    unsigned char header[8];
    Be32(header, length);
    memcpy(header + 4, type, 4);
    if (!sink.Write(header, 8)) return false;
    if (length && !sink.Write(data, length)) return false;
    unsigned crc = Crc32(0xffffffffu, header + 4, 4);
    crc = Crc32(crc, data, length);
    unsigned char tail[4];
    Be32(tail, crc ^ 0xffffffffu);
    return sink.Write(tail, 4);
}
} // namespace

bool WritePng(Sink &sink, const unsigned char *bgra, unsigned width, unsigned height)
{
    // The scanline buffer is on the stack; 4.096 pixels cover every surface the
    // compositor produces and keep the writer allocation free.
    if (!bgra || !width || !height || width > 4096 || height > 1u << 15) return false;
    static const unsigned char signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!sink.Write(signature, 8)) return false;
    unsigned char ihdr[13];
    Be32(ihdr, width);
    Be32(ihdr + 4, height);
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 2;  // truecolour
    ihdr[10] = 0; // deflate
    ihdr[11] = 0; // no filter beyond the per-scanline byte
    ihdr[12] = 0; // no interlace
    if (!Chunk(sink, "IHDR", ihdr, 13)) return false;

    // One zlib stream of stored deflate blocks. The raw bytes are the scanlines
    // with filter type 0 in front of each, so nothing is compressed and no
    // library is needed; a 800x600 report costs about 1,4 MiB.
    const unsigned stride = 1 + width * 3;
    const unsigned long long raw = (unsigned long long)stride * height;
    if (raw > 0xf0000000ull) return false;
    unsigned blocks = (unsigned)((raw + 65534) / 65535);
    if (!blocks) blocks = 1;
    const unsigned data_length = 2 + (unsigned)raw + blocks * 5 + 4;

    unsigned char header[8];
    Be32(header, data_length);
    memcpy(header + 4, "IDAT", 4);
    if (!sink.Write(header, 8)) return false;
    unsigned crc = Crc32(0xffffffffu, header + 4, 4);

    auto emit = [&](const unsigned char *bytes, unsigned length) {
        crc = Crc32(crc, bytes, length);
        return sink.Write(bytes, length);
    };
    const unsigned char zlib[] = {0x78, 0x01};
    if (!emit(zlib, 2)) return false;

    unsigned char line[1 + 3 * 4096];
    unsigned adler_a = 1, adler_b = 0;
    unsigned long long written = 0;
    unsigned row = 0, row_offset = 0;
    while (written < raw)
    {
        const unsigned size = (unsigned)(raw - written > 65535 ? 65535 : raw - written);
        unsigned char block[5];
        block[0] = (unsigned char)(written + size >= raw ? 1 : 0);
        block[1] = (unsigned char)(size & 255);
        block[2] = (unsigned char)(size >> 8);
        block[3] = (unsigned char)(~size & 255);
        block[4] = (unsigned char)((~size >> 8) & 255);
        if (!emit(block, 5)) return false;
        unsigned left = size;
        while (left)
        {
            if (!row_offset)
            {
                line[0] = 0;
                for (unsigned x = 0; x < width; ++x)
                {
                    const unsigned char *pixel = bgra + ((unsigned long long)row * width + x) * 4;
                    line[1 + x * 3] = pixel[2];
                    line[2 + x * 3] = pixel[1];
                    line[3 + x * 3] = pixel[0];
                }
            }
            const unsigned take = stride - row_offset < left ? stride - row_offset : left;
            if (!emit(line + row_offset, take)) return false;
            for (unsigned i = 0; i < take; ++i)
            {
                adler_a = (adler_a + line[row_offset + i]) % 65521;
                adler_b = (adler_b + adler_a) % 65521;
            }
            row_offset += take;
            left -= take;
            written += take;
            if (row_offset == stride) { row_offset = 0; ++row; }
        }
    }
    unsigned char adler[4];
    Be32(adler, (adler_b << 16) | adler_a);
    if (!emit(adler, 4)) return false;
    unsigned char tail[4];
    Be32(tail, crc ^ 0xffffffffu);
    if (!sink.Write(tail, 4)) return false;
    return Chunk(sink, "IEND", nullptr, 0);
}
} // namespace CrashReport
