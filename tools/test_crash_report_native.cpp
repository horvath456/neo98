// SPDX-License-Identifier: GPL-2.0-or-later
// Drives tools/crash_report_fixture.c into one of its three stops and writes
// the report through exactly the code path the SDL host uses: the same
// Application::CollectCrashReport, the same CrashFiles::Write, the same
// CRASHREPORT line on stderr. No SDL is involved.
#include "../src/crash_unwind.h"
#include "../src/x86_app.h"
#include "crash_report_files.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

static void log(const char *text) { puts(text); }
static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    std::vector<unsigned char> bytes(n);
    assert(fread(bytes.data(), 1, n, f) == size_t(n));
    fclose(f);
    return bytes;
}
static const char *report_dir = nullptr;
static int saved_argc = 0;
static char **saved_argv = nullptr;
static unsigned reports_written = 0;
static char last_report[1024] = {};

static void write_report(X86::Application &app, const char *reason, bool hang, unsigned hang_ms)
{
    if (!report_dir) return;
    auto *report = new CrashReport::Report();
    CrashFiles::Describe(*report, reason, "test-crash-report", saved_argc, saved_argv);
    report->hang = hang ? 1 : 0;
    report->hang_ms = hang_ms;
    if (hang)
        snprintf(report->hang_text, sizeof report->hang_text,
                 "No API call for %u ms.", hang_ms);
    app.CollectCrashReport(*report);
    CrashFiles::Write(*report, report_dir, User::Pixels(), 800, 600, last_report,
                      sizeof last_report);
    ++reports_written;
    delete report;
}
static void on_crash(void *, X86::Application &app, const char *reason)
{
    write_report(app, reason, false, 0);
}

// The report says when it cut the process list: the markdown carries
// an INCOMPLETE line, the JSON the count, and the collector has room for
// the root and every child slot.
static void omission_wording()
{
    static_assert(CrashReport::Report::ProcessCapacity >= 17, "room for the root and 16 children");
    struct Text final : CrashReport::Sink {
        std::vector<char> bytes;
        bool Write(const void *data, unsigned size) override {
            bytes.insert(bytes.end(), (const char *)data, (const char *)data + size); return true; }
    } text;
    auto *report = new CrashReport::Report();
    report->processes_omitted = 3;
    assert(CrashReport::FormatMarkdown(*report, text));
    text.bytes.push_back(0);
    assert(strstr(text.bytes.data(), "INCOMPLETE: 3 additional processes"));
    Text json;
    assert(CrashReport::FormatJson(*report, json));
    json.bytes.push_back(0);
    assert(strstr(json.bytes.data(), "\"processes_omitted\": 3"));
    report->processes_omitted = 0;
    Text clean;
    assert(CrashReport::FormatMarkdown(*report, clean));
    clean.bytes.push_back(0);
    assert(!strstr(clean.bytes.data(), "INCOMPLETE"));
    delete report;
}
// The rule behind the call chain of a report: a word on the stack is
// only a return address when a call instruction ends directly in front of it.
// The two negative cases are the ones the Moorhuhn report named as frames, a
// function start (8bff 55 8bec) and a 16-byte data record.
static void call_detection()
{
    struct Case { unsigned char window[7]; unsigned available, length; const char *what; };
    static const Case cases[] = {
        {{0x90, 0x90, 0xe8, 0x11, 0x22, 0x33, 0x44}, 7, 5, "E8 rel32"},
        {{0x90, 0x2e, 0xe8, 0x11, 0x22, 0x33, 0x44}, 7, 5, "2E E8 rel32, without the prefix"},
        {{0x9a, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, 7, 7, "9A ptr16:32"},
        {{0x90, 0xff, 0x15, 0x78, 0x56, 0x34, 0x12}, 7, 6, "FF 15 abs32"},
        {{0x90, 0x90, 0x90, 0x90, 0x90, 0xff, 0xd0}, 7, 2, "FF D0 call eax"},
        {{0x90, 0x90, 0x90, 0x90, 0xff, 0x50, 0x08}, 7, 3, "FF 50 disp8"},
        {{0x90, 0xff, 0x90, 0x44, 0x33, 0x22, 0x11}, 7, 6, "FF 90 disp32"},
        {{0x90, 0x90, 0x90, 0xff, 0x54, 0x24, 0x04}, 7, 4, "FF 54 SIB disp8"},
        {{0xff, 0x94, 0x24, 0x04, 0x03, 0x02, 0x01}, 7, 7, "FF 94 SIB disp32"},
        {{0x90, 0x90, 0x90, 0x90, 0xff, 0x1d, 0x00}, 7, 0, "FF 1D is a far call, other length"},
        {{0x90, 0x90, 0x8b, 0xff, 0x55, 0x8b, 0xec}, 7, 0, "function start, no call"},
        {{0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00}, 7, 0, "data record"},
        {{0x90, 0x90, 0x90, 0x90, 0x90, 0xff, 0xe0}, 7, 0, "FF E0 is a jump"},
        {{0x90, 0x90, 0x90, 0x90, 0x90, 0xff, 0x00}, 7, 0, "FF /0 is an increment"},
        {{0x00, 0x00, 0x00, 0xe8, 0x11, 0x22, 0x33}, 7, 0, "E8 too far from the end"},
        {{0x00, 0x00, 0x00, 0xe8, 0x11, 0x22, 0x33}, 4, 0, "too few bytes to decide"},
        {{0x9a, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, 6, 0, "9A needs all seven bytes"},
    };
    for (const auto &c : cases)
    {
        const unsigned length = CrashUnwind::CallLength(c.window, c.available);
        if (length == c.length) continue;
        printf("CALLCHECK %s: expected %u, got %u\n", c.what, c.length, length);
        assert(false);
    }
    // FF 15 with three bytes of the address readable must not pass as FF D0.
    assert(CrashUnwind::CallLength(cases[3].window, 3) == 0);
}
int main(int argc, char **argv)
{
    omission_wording();
    call_detection();
    // mode exe font [report-directory]
    assert(argc >= 4);
    saved_argc = argc;
    saved_argv = argv;
    const char *mode = argv[1];
    if (argc > 4 && argv[4][0]) report_dir = argv[4];
    auto exe = load(argv[2]), font = load(argv[3]);
    assert(!Files::System().Seed("C:\\WINDOWS\\CRASHRPT.EXE", exe.data(), exe.size()));
    auto *a = new X86::Application(log);
    if (report_dir) X86::Application::SetCrashHook(nullptr, on_crash);
    const char *command = !strcmp(mode, "spin") ? "spin"
                          : !strcmp(mode, "wait") ? "wait"
                          : !strcmp(mode, "frames") ? "frames" : "missing";
    assert(a->Start(exe.data(), exe.size(), font.data(), font.size(), "CRASHRPT.EXE", command));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    unsigned last_calls = a->ApiCalls(), quiet = 0;
    const char *stop = "deadline";
    while (a->Status() != X86::Application::Exited && a->Status() != X86::Application::Failed &&
           std::chrono::steady_clock::now() < deadline)
    {
        a->Pump(8);
        if (a->ApiCalls() != last_calls) { last_calls = a->ApiCalls(); quiet = 0; continue; }
        // The host's hang test in one sentence: no API call for a while. Here
        // the measure is pumps rather than seconds, so the test is not timed.
        if (++quiet < 400) continue;
        stop = "hang";
        break;
    }
    if (a->Status() == X86::Application::Failed) stop = "failed";
    else if (a->Status() == X86::Application::Exited) stop = "exited";
    if (!strcmp(stop, "hang")) write_report(*a, "hang", true, 3000);
    printf("CRASHTEST mode=%s stop=%s state=%u calls=%u reports=%u error=%s path=%s\n", mode, stop,
           (unsigned)a->Status(), a->ApiCalls(), reports_written, a->Error(), last_report);
    fflush(stdout);
    X86::Application::SetCrashHook(nullptr, nullptr);
    delete a;
    return 0;
}
