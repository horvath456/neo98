// SPDX-License-Identifier: GPL-2.0-or-later
// The test switches of the host for saved states of a machine that runs
// Windows programs (src/x86_state.h), and the
// table of the fields of this build. No menu and no folder of states: that
// is tools/host_windows_state.h, which reaches the
// same machine code (MachineState::CanSave, Save, Load).
// --state-save FILE where the step wstate=save of NEO98_HOST_INPUT writes
// --state-load FILE start the machine from a state instead of a program
// (--load-state is the game machine's: the id of one
// of its states)
// Steps of NEO98_HOST_INPUT:
// wstate=save pause, wait for a moment the machine can be saved
// in (at most two seconds of running on), write the
// state, go on
// wstate=scan write the fields into memory and throw them away:
// every pointer the writer refuses, with its field,
// and every number among the written bytes that is
// the start of a block or symbol of this process
// wstate=scan-all the same, and the numbers that point into the
// middle of something
// wstate=can print whether the machine could be saved right now
// wstate=repaint every window draws itself anew (after a load)
// wstate=containers fill, or check, the containers of the test
// NEO98_STATE_PROBE=1 asks the supplier after every turn of the host loop and
// prints at the end how often the answer was yes and the reasons for no.
// NEO98_STATE_TRACE=TEXT prints the way of the writer through every field
// whose path holds TEXT. NEO98_STATE_EXIT_TRACE=1 prints where the host
// faults after its main loop.
#pragma once
#include "../src/x86_app.h"
#include "../src/x86_state.h"
#include "../src/x86_state_fields.h"
#include <execinfo.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <signal.h>
#include <vector>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <unistd.h>

namespace HostState
{
inline std::string save_path, load_path;
// The machine came out of --load-state and stands paused.
inline bool loaded = false;

// The table of the fields of this build, from the file beside the program
// (tools/state_layout.py). Without it the host runs as ever and has no
// states of Windows programs. Read once, when a state is first asked about.
inline bool Layout()
{
    static bool tried = false;
    if (tried) return MachineState::HaveLayout();
    tried = true;
    char path[4096];
#ifdef __APPLE__
    uint32_t size = sizeof path;
    if (_NSGetExecutablePath(path, &size)) return false;
#else
    const ssize_t count = readlink("/proc/self/exe", path, sizeof path - 1);
    if (count <= 0) return false;
    path[count] = 0;
#endif
    char real[4096];
#ifdef __APPLE__
    if (!realpath(path, real)) return false;
#else
    // procfs already gives an absolute resolved path. An unlinked executable
    // remains mapped, with this suffix, and still needs its own field table.
    std::string image_path = path;
    const std::string deleted = " (deleted)";
    if (image_path.size() >= deleted.size() &&
        image_path.compare(image_path.size() - deleted.size(), deleted.size(), deleted) == 0)
        image_path.resize(image_path.size() - deleted.size());
    snprintf(real, sizeof real, "%s", image_path.c_str());
#endif
    // Beside the program, or in the Resources of its bundle (tools/mac_app.py).
    std::string name = std::string(real) + ".layout";
    // A rebuild can replace the pathname before this process first saves.
    // Prefer the immutable table for the image actually mapped in this process.
    unsigned char identity[16];
    FILE *file = nullptr;
    if (MachineState::BuildIdentity(identity)) {
        char hex[33];
        for (unsigned i = 0; i < 16; ++i) snprintf(hex + i * 2, 3, "%02x", identity[i]);
        const std::string path_text = real;
        name = path_text.substr(0, path_text.rfind('/')) + "/.state-layouts/" + hex + ".layout";
        file = fopen(name.c_str(), "rb");
    }
    if (!file) {
        name = std::string(real) + ".layout";
        file = fopen(name.c_str(), "rb");
    }
    if (!file) {
        const std::string path_text = real;
        const size_t slash = path_text.rfind('/');
        name = path_text.substr(0, slash) + "/../Resources/" + path_text.substr(slash + 1) + ".layout";
        file = fopen(name.c_str(), "rb");
    }
    if (!file) {
        // The build left it out because a field has no rule yet (tools/native_app.py).
        const std::string problems = std::string(real) + ".layout-problems";
        if (access(problems.c_str(), R_OK) == 0)
            fprintf(stderr, "NEO98 HOST state: this build saves no Windows programs; see %s\n", problems.c_str());
        return false;
    }
    std::vector<unsigned char> bytes;
    unsigned char buffer[65536];
    for (size_t n; (n = fread(buffer, 1, sizeof buffer, file)) > 0;) bytes.insert(bytes.end(), buffer, buffer + n);
    fclose(file);
    char error[256] = {};
    if (!MachineState::SetLayout(bytes.data(), bytes.size(), error, sizeof error))
        fprintf(stderr, "NEO98 HOST state: %s: %s\n", name.c_str(), error);
    return MachineState::HaveLayout();
}

// NEO98_STATE_EXIT_TRACE=1: from the end of the main loop on a fault prints
// where it happened. For the one who looks for what a loaded machine leaves
// behind when the host ends; the engines use the same signals while they run.
inline void ExitTrace()
{
    if (!getenv("NEO98_STATE_EXIT_TRACE")) return;
    struct sigaction action = {};
    action.sa_handler = [](int number) {
        void *frames[48];
        const int count = backtrace(frames, 48);
        fprintf(stderr, "NEO98 HOST state: signal %d at the end of the host\n", number);
        backtrace_symbols_fd(frames, count, 2);
        _exit(70);
    };
    sigaction(SIGBUS, &action, nullptr);
    sigaction(SIGSEGV, &action, nullptr);
    sigaction(SIGABRT, &action, nullptr);
    sigaction(SIGILL, &action, nullptr);
    sigaction(SIGTRAP, &action, nullptr);
}

// One option of the command line. True when it was one of these.
inline bool Option(int argc, char **argv, int &i)
{
    // --load-state is the game machine's: the id of one of its states (tools/neo98_host.cpp, StartStates).
    const bool save = !strcmp(argv[i], "--state-save");
    if (!save && strcmp(argv[i], "--state-load")) return false;
    if (i + 1 < argc) (save ? save_path : load_path) = argv[++i];
    return true;
}

struct Probe
{
    unsigned long long asked = 0, yes = 0;
    std::map<std::string, unsigned long long> reasons;
    struct Frame {
        std::chrono::steady_clock::time_point since;
        unsigned long long seen = 0;
        bool reported = false;
    };
    std::map<std::string, Frame> frames;
};
inline Probe probe;
inline void ProbeTurn(X86::Application &first)
{
    static const bool on = getenv("NEO98_STATE_PROBE") != nullptr && Layout();
    if (!on) return;
    const auto verdict = MachineState::CanSave(first);
    ++probe.asked;
    if (verdict.possible) ++probe.yes;
    else {
        ++probe.reasons[verdict.reason];
        // Function-by-function, for every blocked thread. A frame that
        // disappears starts its timer anew if it is seen in another call.
        MachineState::BlockedFrames(first, [](void *, const char *program, unsigned thread, const char *function) {
            const auto now = std::chrono::steady_clock::now();
            const std::string key = std::string(program) + " thread " + std::to_string(thread) + " function=" + function;
            auto [at, fresh] = probe.frames.try_emplace(key);
            auto &frame = at->second;
            if (fresh) frame.since = now;
            frame.seen = probe.asked;
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - frame.since).count();
            if (ms >= 200 && !frame.reported) {
                frame.reported = true;
                printf("NEO98 HOST state long-frame ms=%lld %s\n", (long long)ms, key.c_str());
            }
        }, nullptr);
    }
    for (auto at = probe.frames.begin(); at != probe.frames.end();)
        if (at->second.seen != probe.asked) at = probe.frames.erase(at);
        else ++at;
}
inline void ProbeReport()
{
    if (!probe.asked) return;
    printf("NEO98 HOST state probe asked=%llu yes=%llu (%.1f%%)\n", probe.asked, probe.yes,
           100.0 * probe.yes / probe.asked);
    for (const auto &[reason, count] : probe.reasons)
        printf("NEO98 HOST state probe no=%llu (%.1f%%) %s\n", count, 100.0 * count / probe.asked, reason.c_str());
    fflush(stdout);
}

// Brings the machine to a pause it can be saved in. A thread in the middle
// of a call that cannot be taken up again: the machine runs on, one turn at
// a time and with a save point asked for, until every thread stands well,
// and is stopped at that very turn; at most `patience` long. True with the
// machine paused; false with the reason, and the machine as it was. pause
// and paused are the host's switch and its state; pump runs the machine one
// turn. This is what a menu item "save state" does before MachineState::Save.
template <typename Pause, typename Pump>
bool Reach(X86::Application &first, Pause &&pause, bool paused, Pump &&pump, MachineState::Verdict &verdict,
           unsigned &turns, std::chrono::milliseconds patience = std::chrono::milliseconds(2000))
{
    const auto began = std::chrono::steady_clock::now();
    Layout();
    verdict = MachineState::CanSave(first);
    turns = 0;
    if (!verdict.possible && verdict.transient) {
        if (paused) pause(false, "state: not yet");
        MachineState::WantSavePoint(true);
        while (!verdict.possible && verdict.transient && std::chrono::steady_clock::now() - began < patience) {
            pump();
            ++turns;
            verdict = MachineState::CanSave(first);
        }
        MachineState::WantSavePoint(false);
        if (verdict.possible || paused) pause(true, "state");
    } else if (verdict.possible && !paused) pause(true, "state");
    return verdict.possible;
}

// The step state=... of the script. pause and paused are the host's switch
// and its state; pump runs the machine one turn.
template <typename Pause, typename Pump>
void Step(X86::Application &first, const std::string &what, Pause &&pause, bool paused, Pump &&pump)
{
    Layout();
    if (what == "scan" || what == "scan-all") {
        const unsigned outside = MachineState::ScanPointers(first, stdout, what == "scan-all");
        printf("NEO98 HOST state scan outside=%u\n", outside);
        fflush(stdout);
        return;
    }
    if (what == "repaint") {
        // Every window of the screen draws itself anew, frame and all: after
        // a load that uses every font, brush and DC the state brought along,
        // also those a standing picture never touches again.
        User::RedrawWindow(User::GetDesktopWindow(), nullptr, 0, 0x585);
        printf("NEO98 HOST state repaint asked\n");
        fflush(stdout);
        return;
    }
    if (what == "chains") {
        // Which host functions every waiting thread stands in.
        MachineState::Chains(first, stdout);
        fflush(stdout);
        return;
    }
    if (what == "containers") {
        // For the test of the containers of the standard library in a state.
        const int answer = MachineState::TestContainers();
        printf("NEO98 HOST state containers %s\n", answer == 0 ? "filled" : answer == 1 ? "same and usable" : "differ");
        fflush(stdout);
        return;
    }
    if (what == "can") {
        const auto verdict = MachineState::CanSave(first);
        printf("NEO98 HOST state can-save=%d transient=%d %s\n", verdict.possible ? 1 : 0, verdict.transient ? 1 : 0,
               verdict.reason);
        fflush(stdout);
        return;
    }
    if (what != "save") {
        printf("NEO98 HOST state: unknown step %s\n", what.c_str());
        return;
    }
    if (save_path.empty()) {
        printf("NEO98 HOST state save refused: no --state-save FILE\n");
        fflush(stdout);
        return;
    }
    const bool was_paused = paused;
    const auto began = std::chrono::steady_clock::now();
    MachineState::Verdict verdict;
    unsigned turns = 0;
    Reach(first, pause, paused, pump, verdict, turns);
    if (!verdict.possible) {
        printf("NEO98 HOST state save refused after %u turns: %s\n", turns, verdict.reason);
        fflush(stdout);
        return;
    }
    char error[1024] = {};
    auto *sink = MachineState::FileSink(save_path.c_str());
    const bool okay = sink && MachineState::Save(first, *sink, error, sizeof error);
    const unsigned long long bytes = sink ? MachineState::Written(sink) : 0;
    if (sink) MachineState::CloseSink(sink);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    const auto parts = MachineState::LastSave();
    if (okay) printf("NEO98 HOST state saved %s bytes=%llu ms=%.1f turns=%u fields=%llu guest=%llu processes=%u threads=%u\n",
                     save_path.c_str(), bytes, ms, turns, parts.field_bytes, parts.guest_bytes, parts.processes,
                     parts.threads);
    else printf("NEO98 HOST state save failed: %s\n", sink ? error : "cannot write the file");
    fflush(stdout);
    if (!was_paused) pause(false, "state");
}

// The Mac's local time as Application::StartGuestClock takes it.
inline void WallClock(unsigned short fields[8])
{
    const time_t now = time(nullptr);
    struct tm parts = {};
    localtime_r(&now, &parts);
    const unsigned short values[8] = {
        (unsigned short)(parts.tm_year + 1900), (unsigned short)(parts.tm_mon + 1), (unsigned short)parts.tm_wday,
        (unsigned short)parts.tm_mday, (unsigned short)parts.tm_hour, (unsigned short)parts.tm_min,
        (unsigned short)(parts.tm_sec > 59 ? 59 : parts.tm_sec), 0};
    memcpy(fields, values, sizeof values);
}

// Where the host would start the first Application: the machine of
// --state-load instead, in the process the host has made and not started.
// False when none was asked for. A failure ends the process, because the
// state of the port is then neither one nor the other.
inline bool LoadMachine(X86::Application &first)
{
    if (load_path.empty()) return false;
    const char *why = nullptr;
    Layout();
    if (!MachineState::Available(&why)) {
        fprintf(stderr, "NEO98 HOST state load refused: %s\n", why);
        exit(21);
    }
    const auto began = std::chrono::steady_clock::now();
    auto *source = MachineState::FileSource(load_path.c_str());
    if (!source) {
        fprintf(stderr, "NEO98 HOST state load refused: cannot read %s\n", load_path.c_str());
        exit(21);
    }
    char error[512] = {};
    MachineState::Host host;
    host.first = &first;
    // The wall clock of the guest starts at the Mac's local time again.
    unsigned short fields[8];
    WallClock(fields);
    host.wall_clock = fields;
    const bool okay = MachineState::Load(*source, host, error, sizeof error) != nullptr;
    MachineState::CloseSource(source);
    if (!okay) {
        fprintf(stderr, "NEO98 HOST state load failed: %s\n", error);
        // The state of the port is half the old one and half the new one:
        // no destructor may see it.
        fflush(stdout);
        _exit(22);
    }
    // The state was saved in a pause, and the machine stays in it: the host
    // takes the pause over once its switch exists (Loaded).
    loaded = true;
    printf("NEO98 HOST state loaded %s ms=%.1f end-pointers=%u\n", load_path.c_str(),
           std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count(),
           MachineState::EndPointers());
    fflush(stdout);
    return true;
}
} // namespace HostState
