// SPDX-License-Identifier: GPL-2.0-or-later
// The processor of an ARM64 process behind X86::Cpu. docs/ARM64-APPS.md.
#pragma once
#include "x86_cpu.h"
namespace Arm64
{
// What the process tells its processor once, before the first Run.
struct Layout
{
    // The gate region of the imports and its two marks.
    unsigned gates = 0, gate_bytes = 0, exit_gate = 0, callback_gate = 0;
    // A region of the process for the processor: the resume address at its
    // start, the entries of the callback table behind it. Mapped read and
    // execute by the process.
    unsigned area = 0, area_bytes = 0;
    // A second region, readable and writable: the lasting 32-bit forms of
    // structs the system holds on to between calls (a prepared WAVEHDR).
    unsigned shadows = 0, shadow_bytes = 0;
    // The descriptor page (GDT); the FS selector of a thread names its TEB.
    unsigned descriptors = 0;
    void *context = nullptr;
    // DLL and export name behind a gate; false for a slot without an import.
    bool (*import_name)(void *context, unsigned gate, const char *&dll, const char *&name) = nullptr;
    // Whether a window is a dialog (DWLP_DLGPROC and DWLP_USER then name the
    // dialog's own fields).
    bool (*is_dialog)(void *context, unsigned hwnd) = nullptr;
    // The type of a GDI object as GetObjectType names it; zero for no object.
    unsigned (*object_type)(void *context, unsigned handle) = nullptr;
    // The base of the image (EXE or DLL) of the process that holds this
    // address; false for an address outside every image.
    bool (*image_of)(void *context, unsigned address, unsigned &base) = nullptr;
};
// The address a finished API call returns to: the first word of the area.
inline unsigned ResumeAddress(const Layout &layout) { return layout.area; }
// The address the frame registers name while a thread stands interrupted
// between two instructions of the program: to go on there is to go on where
// its own registers stand. The second word of the area.
inline unsigned ContinueAddress(const Layout &layout) { return layout.area + 8; }
// The 16 bytes an ARM64 process has in every gate: svc #0; ret; and padding.
void FillGates(unsigned char *bytes, unsigned size);
unsigned CreateCpu(X86::Cpu *&cpu);
void Configure(X86::Cpu &cpu, const Layout &layout);
// Text for a Run that ended with an error the result code does not explain
// (a function without a signature); null otherwise.
const char *ErrorDetail(X86::Cpu &cpu);
// The ARM64 state at the last fault, for the failure message and the report.
bool FaultState(X86::Cpu &cpu, unsigned long long &pc, unsigned long long &sp, unsigned long long &lr,
                unsigned &syndrome);
// Whether the signature table describes this export, and the 32-bit export
// that provides it (GetWindowLongPtrA is GetWindowLongA).
bool Described(const char *dll, const char *name, const char *&provider);
// The address the import table gets for an export that exists under Win64
// only and leads to another export here: an entry of the area that carries
// the signature of `name` and calls the gate of the provider. Zero when the
// area is full.
unsigned AliasGate(X86::Cpu &cpu, const char *dll, const char *name, unsigned provider_gate);
// A procedure of an ARM64 program reaches the system as the address of an
// entry of its process (target and kind). Something that outlives the
// process boundary, a hook for the whole session, needs both sides: the
// procedure and kind behind an entry, and the entry of this process for a
// procedure of a module loaded here. For any other processor the values pass
// unchanged.
unsigned CallbackTarget(X86::Cpu &cpu, unsigned entry, int &kind);
unsigned CallbackEntry(X86::Cpu &cpu, unsigned target, int kind);

// Exceptions (src/arm64_seh.inc, docs/ARM64-APPS.md "Exceptions"). The
// processor answers the exports Windows on ARM64 has for them itself and
// walks the frames of the program by its.pdata.
// The address the import table gets for such an export (RaiseException,
// RtlUnwindEx,...); zero for a function a provider answers.
unsigned CoreGate(X86::Cpu &cpu, const char *dll, const char *name);
// A fault of the processor as an exception of the program, from the state
// the last Run ended with. True: a handler of the program runs with the next
// Run. False: no frame has a handler, nothing changed, the fault is what it
// was. run_error, trap and fault_kind come back with an unhandled exception.
bool DeliverFault(X86::Cpu &cpu, unsigned code, unsigned parameters, const unsigned long long *information,
                  unsigned run_error, unsigned trap, unsigned fault_kind);
// An exception the handlers of the program were asked about and none took, or
// one the program raised itself that nobody caught: Run ended with run_error
// of the fault, or with CpuError::Exception, and the registers are the ones
// of the exception again.
struct UnhandledException
{
    bool valid = false;
    unsigned code = 0, flags = 0, parameters = 0;
    unsigned long long address = 0, information[15] = {};
    unsigned run_error = 0, trap = 0, fault_kind = 0;
    // The filter of the process answered EXCEPTION_EXECUTE_HANDLER: the
    // process ends without the error box.
    bool quiet = false;
};
bool TakeUnhandled(X86::Cpu &cpu, UnhandledException &exception);
} // namespace Arm64
