// SPDX-License-Identifier: GPL-2.0-or-later
// Runs one ARM64 program (or any other) to its end without a screen and
// prints what a test compares: the rows of its console, its exit code or the
// failure, and the counters of the run. tools/test_arm64_apps_native.py.
// docs/ARM64-APPS.md.
// The rows of the console are read through a private accessor, as the other
// guest runners read private state.
#define private public
#include <csignal>
#include <chrono>
#include <unistd.h>
#include "../src/x86_app.h"
#undef private
#include "system_tree_host.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static std::vector<unsigned char> Load(const char *path)
{
    std::vector<unsigned char> bytes;
    FILE *file = fopen(path, "rb");
    if (!file) return bytes;
    for (int c; (c = fgetc(file)) != EOF;) bytes.push_back((unsigned char)c);
    fclose(file);
    return bytes;
}

static volatile sig_atomic_t asked_to_end = 0;
static bool verbose = false;
static void Log(const char *text)
{
    if (verbose) fprintf(stderr, "LOG %s\n", text);
    else if (!strncmp(text, "ARM64", 5) || !strncmp(text, "MISSING", 7) || !strncmp(text, "EXCEPTION", 9) ||
             !strncmp(text, "arm64 ", 6))
        printf("LOG %s\n", text);
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: test-arm64-apps program.exe font.fnt GUEST.EXE [command line]\n");
        return 2;
    }
    verbose = getenv("NEO98_ARM64_VERBOSE") != nullptr;
    auto exe = Load(argv[1]), font = Load(argv[2]);
    if (exe.empty() || font.empty()) return 2;
    if (!SystemTreeHost::Mount()) return 2;
    X86::Application app(Log);
    const char *command = argc > 4 ? argv[4] : nullptr;
    if (!app.Start(exe.data(), (unsigned)exe.size(), font.data(), (unsigned)font.size(), argv[3], command))
    {
        printf("START FAILED %s\n", app.Error());
        return 1;
    }
    app.trace_calls = getenv("NEO98_ARM64_TRACE_CALLS") != nullptr;
    // Until the program ends, bounded by time: a program that waits (for a
    // timer, for a wave block to play) leaves the pump idle, and an idle pump
    // must not use up the patience of the runner.
    const char *limit = getenv("NEO98_ARM64_SECONDS");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(limit ? atoi(limit) : 120);
    // SIGTERM ends the run in order: the process leaves through main, so the
    // machine of the ARM64 program is taken down before it goes.
    signal(SIGTERM, [](int) { asked_to_end = 1; });
    while (app.Status() != X86::Application::Exited && app.Status() != X86::Application::Failed && !asked_to_end &&
           std::chrono::steady_clock::now() < deadline)
    {
        const unsigned before = app.ApiCalls();
        app.Pump(32);
        if (app.ApiCalls() == before) usleep(200);
    }
    for (unsigned row = 0; row < 50; ++row)
    {
        char text[256] = {};
        if (!app.ConsoleText(row, text, sizeof text)) break;
        size_t length = strlen(text);
        while (length && text[length - 1] == ' ') text[--length] = 0;
        if (length) printf("CONSOLE %u %s\n", row, text);
    }
    if (app.Status() == X86::Application::Exited)
        printf("EXITED code=%u calls=%u callbacks=%u\n", app.ExitCode(), app.ApiCalls(), app.Callbacks());
    else if (app.Status() == X86::Application::Failed)
        printf("FAILED %s\n", app.Error());
    else
        printf("RUNNING status=%u calls=%u\n", (unsigned)app.Status(), app.ApiCalls());
    return 0;
}
