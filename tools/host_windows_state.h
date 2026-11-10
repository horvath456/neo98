// SPDX-License-Identifier: GPL-2.0-or-later
// The supplier of the machine state for a game machine with a Windows game:
// the runtime of src/ as bytes and back (src/x86_state.h).
// The bytes are the stream of MachineState::Save, packed with zlib. They do
// not hang on the build: the stream names its fields, and a host reads it by
// the table of its own build (tools/state_layout.py). What they do hang on
// is the number of the format, which is the engine of zustand.ini.
// A state is taken where every thread of the machine can be taken up again
// from its registers (MachineState::CanSave). Save asks for it, and Poll,
// called every turn of the host's main loop, takes it at the first turn the
// machine stands like that; until then the threads are asked to stop at the
// next such place.
// Load is different from the DOS machine's: the program of the game machine
// is not started and replaced, the machine is built from the bytes alone, in
// the first process the host has made and not started (LoadNow).
#pragma once
#include "host_state.h"
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <zlib.h>

namespace HostWindowsState
{
// What the host hands over once its first process and its pause exist.
inline X86::Application *first = nullptr;
inline std::function<void(bool, const char *)> pause;
inline std::function<bool()> paused;
inline bool asked = false, loaded_now = false;
inline std::vector<unsigned char> pending;
inline std::string load_error;
// The packing of the bytes: "A98Z", the length of the stream, the stream deflated.
constexpr unsigned Packing = 1;

inline std::string Engine()
{
    const char *test = getenv("NEO98_WINDOWS_STATE_ENGINE");
    if (test && *test) return test;
    return "windows-" + std::to_string(MachineState::FormatVersion) +
#ifdef __linux__
        "-linux-native"
#endif
        "-z" + std::to_string(Packing);
}

struct Bytes final : MachineState::Sink, MachineState::Source
{
    std::vector<unsigned char> data;
    size_t at = 0;
    bool Write(const void *bytes, size_t count) override
    {
        const auto *b = static_cast<const unsigned char *>(bytes);
        data.insert(data.end(), b, b + count);
        return true;
    }
    bool Read(void *bytes, size_t count) override
    {
        if (count > data.size() - at) return false;
        memcpy(bytes, data.data() + at, count);
        at += count;
        return true;
    }
};
inline bool Pack(const std::vector<unsigned char> &raw, std::vector<unsigned char> &packed)
{
    uLongf room = compressBound(raw.size());
    packed.resize(12 + room);
    memcpy(packed.data(), "A98Z", 4);
    const unsigned long long length = raw.size();
    memcpy(packed.data() + 4, &length, 8);
    // Level 1: a state is mostly tables with long runs, and the person waits.
    if (compress2(packed.data() + 12, &room, raw.data(), raw.size(), 1) != Z_OK) return false;
    packed.resize(12 + room);
    return true;
}
inline bool Unpack(const std::vector<unsigned char> &packed, std::vector<unsigned char> &raw)
{
    unsigned long long length = 0;
    if (packed.size() < 12 || memcmp(packed.data(), "A98Z", 4)) return false;
    memcpy(&length, packed.data() + 4, 8);
    if (length > (1ull << 34)) return false;
    raw.resize(length);
    uLongf got = length;
    return uncompress(raw.data(), &got, packed.data() + 12, packed.size() - 12) == Z_OK && got == length;
}

// The reason of a refusal in the words of the person.
inline std::string Reason(const MachineState::Verdict &verdict)
{
    using V = MachineState::Verdict;
    switch (verdict.why) {
    case V::NoTable: return "This Neo98 build cannot save Windows game states: the runtime field layout is missing.";
    case V::DosProgram: return "Cannot save a state while DOS and Windows programs are running together.";
    case V::Arm64Program: return "Saving ARM64 program states is not yet supported.";
    case V::Engine: return "The selected CPU backend cannot save this program's state.";
    case V::Ended: return "The program has exited or crashed.";
    case V::Dialog: return "Close the open game dialog before saving a state.";
    case V::Menu: return "Close the open game menu before saving a state.";
    default: return "The game is busy and cannot save a state right now. Try again shortly.";
    }
}
inline bool Ready(std::string &reason, std::vector<std::pair<std::string, std::string>> *machine)
{
    if (!first) { reason = "The machine is still starting."; return false; }
    if (!pending.empty() || loaded_now) { reason = "A state is being loaded."; return false; }
    HostState::Layout();
    const auto verdict = MachineState::CanSave(*first);
    // A thread in the middle of a call is no reason against asking: Poll
    // waits for it. A dialog or a menu of the game does not pass by itself;
    // the person is told to close it, and the items are grey until then.
    if (!verdict.possible && (!verdict.transient || verdict.why == MachineState::Verdict::Dialog ||
                              verdict.why == MachineState::Verdict::Menu)) {
        reason = Reason(verdict);
        return false;
    }
    if (machine) machine->emplace_back("format", std::to_string(MachineState::FormatVersion));
    return true;
}
inline void Save()
{
    asked = true;
}
inline void Load(std::vector<unsigned char> bytes)
{
    pending = std::move(bytes);
}
// Where the host would start its first process: the machine of the state
// instead. False when no state is to be loaded. A failure leaves the port
// half one machine and half the other; `error` says why, for the person, and
// the host must start over.
inline bool LoadNow(X86::Application &process, const unsigned short *wall_clock, bool &failed, std::string &error)
{
    failed = false;
    if (pending.empty()) return false;
    const auto began = std::chrono::steady_clock::now();
    Bytes stream;
    failed = true;
    const bool unpacked = Unpack(pending, stream.data);
    const size_t packed = pending.size();
    pending.clear();
    pending.shrink_to_fit();
    if (!unpacked) {
        error = "The state file is damaged.";
        return true;
    }
    HostState::Layout();
    MachineState::Host host;
    host.first = &process;
    host.wall_clock = wall_clock;
    char text[512] = {};
    if (!MachineState::Load(stream, host, text, sizeof text)) {
        printf("NEO98 HOST state load: the machine was not built: %s\n", text);
        fflush(stdout);
        error = std::string("Could not restore the machine from the state (") + text + ").";
        return true;
    }
    failed = false;
    loaded_now = true;
    printf("NEO98 HOST state machine built from %zu bytes (%zu packed) in %.1f ms end-pointers=%u\n", stream.data.size(), packed,
           std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count(), MachineState::EndPointers());
    fflush(stdout);
    return true;
}
inline int Poll(std::vector<unsigned char> *bytes, std::string &error)
{
    // After a load: the machine was built before the first turn and stands.
    if (!bytes) {
        if (!loaded_now) { error = load_error.empty() ? "Could not load the state." : load_error; return -1; }
        return 1;
    }
    if (!asked || !first) { error = "No state was requested."; return -1; }
    const auto verdict = MachineState::CanSave(*first);
    if (!verdict.possible) {
        if (verdict.transient && verdict.why != MachineState::Verdict::Dialog && verdict.why != MachineState::Verdict::Menu) {
            // Every thread stops at the next place it can be taken up from.
            MachineState::WantSavePoint(true);
            return 0;
        }
        MachineState::WantSavePoint(false);
        asked = false;
        error = Reason(verdict);
        return -1;
    }
    MachineState::WantSavePoint(false);
    asked = false;
    const auto began = std::chrono::steady_clock::now();
    if (paused && !paused() && pause) pause(true, "a state is taken");
    Bytes stream;
    char text[512] = {};
    if (!MachineState::Save(*first, stream, text, sizeof text)) {
        printf("NEO98 HOST state save: the machine was not written: %s\n", text);
        fflush(stdout);
        error = "Could not capture the game state. See the log for details.";
        return -1;
    }
    const auto written = std::chrono::steady_clock::now();
    if (!Pack(stream.data, *bytes)) {
        error = "Could not compress the state.";
        return -1;
    }
    const auto parts = MachineState::LastSave();
    printf("NEO98 HOST state machine written %zu bytes (fields=%llu guest=%llu processes=%u threads=%u) in %.1f ms, packed %zu in %.1f ms\n",
           stream.data.size(), parts.field_bytes, parts.guest_bytes, parts.processes, parts.threads,
           std::chrono::duration<double, std::milli>(written - began).count(), bytes->size(),
           std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - written).count());
    fflush(stdout);
    return 1;
}
inline void Release()
{
    asked = false;
    loaded_now = false;
    MachineState::WantSavePoint(false);
}
} // namespace HostWindowsState
