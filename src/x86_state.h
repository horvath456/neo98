// SPDX-License-Identifier: GPL-2.0-or-later
// The saved state of a machine that runs Windows programs: the supplier of
// the machine state for a game machine. The runtime serializes its own state.
// A state is written field by field and by name, so it does not hang on the
// build that wrote it:
// the fields every variable with static storage of src/ and what it
// owns, by the layout table the build reads out of the
// debug information (src/x86_state_fields.h)
// the regions the pages of the session arena
// the processes what belongs to the engine and is read through
// X86::Cpu: the guest memory with its page rights and
// the registers of every thread
// A pointer is never written as a number. What points at an object of the
// host has a rule that says so (src/state_rules/), and the build fails for a
// pointer it can neither follow nor find a rule for.
#pragma once
#include <stddef.h>
namespace X86
{
class Application;
}
namespace MachineState
{
// The number of the format. It changes when a state written before can no
// longer be read at all; a new field or a field less does not change it.
constexpr unsigned FormatVersion = 1;
// Identity of the loaded image, also used to find its immutable field table.
bool BuildIdentity(unsigned char out[16]);
// True when a state can be saved and loaded here at all: the host has
// handed over the layout table of its build (SetLayout, x86_state_fields.h).
bool Available(const char **why = nullptr);

// ---- Byte streams -----------------------------------------------------------
struct Sink
{
    virtual ~Sink() = default;
    virtual bool Write(const void *bytes, size_t count) = 0;
};
struct Source
{
    virtual ~Source() = default;
    virtual bool Read(void *bytes, size_t count) = 0;
};
// A file of the host as a stream, for the test switches of the host.
Sink *FileSink(const char *path);
Source *FileSource(const char *path);
unsigned long long Written(const Sink *);
void CloseSink(Sink *);
void CloseSource(Source *);

// ---- The supplier of the machine state --------------------------------------
// Whether the machine can be saved at this moment. The caller has paused it
// (X86::Application::PauseMachine) and calls from the loop that would call
// Pump next. A "no" names the reason; most reasons pass when the machine
// runs a little longer (a thread inside a call that cannot be taken up
// again), some do not (a 16-bit process, a DOS machine).
struct Verdict
{
    bool possible = false;
    // The reason is one that running on can end.
    bool transient = false;
    char reason[256] = {};
    // What kind of reason, for a host that tells a person in its own words.
    enum Why
    {
        Nothing, NoTable, DosProgram, Arm64Program, Glide, OpenGL, Engine, Ended,
        // A thread in the middle of something that passes by itself.
        Busy,
        // A thread inside a modal dialog or a menu: it passes when the person closes it.
        Dialog, Menu
    } why = Nothing;
};
Verdict CanSave(X86::Application &first);
Verdict CanSaveDosUi(X86::Application &first);
// While a "no" of CanSave is one that running on can end: ask every thread
// to stop at the next place it can be saved at, pump, ask CanSave again.
// Switch it off again afterwards; it costs a turn per call of the system.
void WantSavePoint(bool wanted);
// Writes the state of the paused machine. False with a text when it failed;
// the stream is then incomplete and must be thrown away.
bool Save(X86::Application &first, Sink &sink, char *error, size_t error_bytes);
bool SaveDosUi(X86::Application &first, Sink &sink, char *error, size_t error_bytes);
// What the last Save wrote: the fields of the port with the regions, and what
// was read out of the processors (guest memory, registers).
struct SaveNumbers
{
    unsigned long long field_bytes = 0, guest_bytes = 0;
    unsigned processes = 0, threads = 0;
};
SaveNumbers LastSave();
// What the loading host hands over: the objects of its own that the machine
// reaches. The loader binds them into the loaded state.
struct Host
{
    // The first process as the host has made it and not started: the
    // machine's first process becomes this very object.
    X86::Application *first = nullptr;
    // The Mac's local time as Application::StartGuestClock takes it: the
    // wall clock of the guest shows the present, like a notebook after
    // sleep. Null keeps the clock of the saved process.
    const unsigned short *wall_clock = nullptr;
};
// Replaces the state of the port in this process by the one of the stream
// and answers the first process of the machine (host.first), paused, or null
// with a text. The process must not have started a machine of its own: the
// host calls this where it would otherwise start the first Application.
// After a failure the process is of no use any more and must end.
X86::Application *Load(Source &source, const Host &host, char *error, size_t error_bytes);
X86::Application *LoadDosUi(Source &source, const Host &host, char *error, size_t error_bytes);
bool ValidateDosUi(Source &source, char *error, size_t error_bytes);

// ---- Numbers and checks -----------------------------------------------------
// Prints, for every thread of the machine that waits inside the host, the
// functions of the host it stands in, innermost first: "NEO98 STATE chain
// PROGRAM thread N depth D pending NAME: f < g < h". What a thread holds on
// its host stack is what a state cannot carry; this is the list of it.
void Chains(X86::Application &first, void *file);
// Visits the functions of threads whose host frames cannot be resumed.
// The host uses this to measure how long each function prevents a state.
using FrameVisitor = void (*)(void *, const char *program, unsigned thread, const char *function);
void BlockedFrames(X86::Application &first, FrameVisitor visitor, void *context);

// For tools/test_state_host.py: containers of the standard library in a
// variable of the port. The first call fills them and answers 0; a call in
// a process whose containers are filled (a loaded state) answers 1 when
// they hold what was filled in and can be used on, else -1.
int TestContainers();
// Writes the state into memory and throws it away; prints, to the stream,
// what it consists of and every number among its bytes that is an address of
// this process although no field typed it as a pointer. Returns how many.
unsigned ScanPointers(X86::Application &first, void *file, bool verbose);
} // namespace MachineState
