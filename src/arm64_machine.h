// SPDX-License-Identifier: GPL-2.0-or-later
// The machine an ARM64 process runs in: its 4 GiB address space and the
// execution of its code on EL0 until the next event. src/arm64_cpu.cpp builds
// the processor of an ARM64 process (X86::CpuBackend::Arm64) on it; a host
// provides the machine. On macOS that is Hypervisor.framework
// (tools/host_arm64_cpu.cpp); a kernel that runs on EL1 itself would supply
// page tables for EL0 and its own exception vector. docs/ARM64-APPS.md.
#pragma once
namespace Arm64
{
struct Vector
{
    unsigned long long low = 0, high = 0;
};
// The state of one thread of the program.
struct Registers
{
    unsigned long long x[31] = {};
    unsigned long long sp = 0, pc = 0;
    // NZCV and the other bits of PSTATE a program on EL0 can
    unsigned long long pstate = 0;
    unsigned long long tpidr = 0;
    Vector v[32];
    unsigned long long fpcr = 0, fpsr = 0;
};
enum class Event
{
    // The program executed `svc`; pc stands behind it.
    Call,
    // A memory access the page rights refuse. `kind` is an X86::CpuFault.
    Fault,
    // An instruction the processor refuses, or `brk`.
    Illegal,
    // RequestStop from another thread.
    Stopped,
    // The time Slice allowed is used up; the program stands between two
    // instructions.
    Slice,
    // The machine itself failed; `error` is an X86::CpuError.
    Failed,
};
struct Exit
{
    Event event = Event::Failed;
    unsigned kind = 0, size = 0, error = 0;
    unsigned long long address = 0;
    // ESR_EL1 of the exception, for a report.
    unsigned syndrome = 0;
};
class Machine
{
  public:
    virtual ~Machine() = default;
    // Guest memory, with the meaning and the result codes of X86::Cpu.
    virtual unsigned Map(unsigned address, unsigned size, unsigned prot) = 0;
    virtual unsigned MapHost(unsigned address, unsigned size, unsigned prot, void *host) = 0;
    virtual unsigned Unmap(unsigned address, unsigned size) = 0;
    virtual unsigned Protect(unsigned address, unsigned size, unsigned prot) = 0;
    virtual unsigned Read(unsigned address, void *out, unsigned size) = 0;
    virtual unsigned Write(unsigned address, const void *data, unsigned size) = 0;
    virtual void *HostPointer(unsigned address, unsigned size) = 0;
    // The rights of one page (X86::Prot), and whether it is mapped at all.
    virtual bool Rights(unsigned address, unsigned &prot) = 0;
    // The host wrote instructions the program may already have executed.
    virtual void InvalidateCode(unsigned address, unsigned size) = 0;
    // The three pointers of X86::Cpu for access in place.
    virtual unsigned char *Memory() = 0;
    virtual const unsigned char *PageRead() = 0;
    virtual const unsigned char *PageWrite() = 0;
    // Runs the program from the state in registers until the next event and
    // leaves the state of that moment in registers. The vector registers
    // may stay in the machine between two runs of the same Registers;
    // SyncVectors fetches them before the caller copies or inspects them,
    // and VectorsChanged says the caller wrote them.
    virtual void Run(Registers &registers, Exit &exit) = 0;
    // A call materializes x0..x7 and x30. The other integer registers may
    // remain in the machine until context capture, a callback or inspection.
    virtual void SyncGeneral(Registers &) {}
    virtual void GeneralChanged(Registers &) {}
    void SyncRegisters(Registers &registers) { SyncGeneral(registers); SyncVectors(registers); }
    virtual void SyncVectors(Registers &registers) = 0;
    virtual void VectorsChanged(Registers &registers) = 0;
    // How long each following Run may execute the program before it ends
    // with Event::Slice; zero for no limit.
    virtual void Slice(unsigned long long nanoseconds) = 0;
    // From any thread: a Run in progress ends with Event::Stopped.
    virtual void RequestStop() = 0;
};
// A host provider registers the factory; without one no ARM64 process starts.
using MachineFactory = unsigned (*)(Machine *&machine);
// The provider's orderly end: afterwards no machine runs and none is made.
using MachineShutdown = void (*)();
void RegisterMachine(MachineFactory, MachineShutdown = nullptr);
// The microseconds the machine has stood paused in all. A
// program reads the counter of its processor without a call (CNTVCT_EL0), so
// the provider keeps that counter this far behind the host's.
void SetCounterPause(unsigned long long microseconds);
unsigned long long CounterPause();
bool MachineAvailable();
// A host calls this before its process ends or replaces itself (exit, execv).
// Without a provider it does nothing.
void ShutdownMachines();
} // namespace Arm64
