// SPDX-License-Identifier: GPL-2.0-or-later
// The x86 processor of one guest process, independent of the engine that
// emulates it. The interface below defines the processor contract.
#pragma once
namespace X86
{
// Register numbers. The eight general registers carry their encoding in the
// x86 instruction set (EAX 0.. EDI 7), so the number is also their index in
// Cpu::gpr. Registers up to Gs and the x87 and debug words are read and
// written as 32-bit values; Fp0..Fp7, Xmm0..Xmm7, Gdtr and Ldtr only through
// Cpu::GetWide and Cpu::SetWide.
struct Reg
{
    enum : int
    {
        Eax = 0, Ecx, Edx, Ebx, Esp, Ebp, Esi, Edi,
        Eip, Eflags,
        Es, Cs, Ss, Ds, Fs, Gs,
        Cr0,
        Fpcw, Fpsw, Fptag, Fip, Fcs, Fdp, Fds, Fop, Mxcsr,
        Dr0, Dr1, Dr2, Dr3, Dr6, Dr7,
        // Physical x87 registers, not ST(i): FPSW.TOP selects ST(0). Ten bytes,
        // the 64-bit mantissa followed by sign and exponent, in a buffer of
        // sixteen.
        Fp0, Fp1, Fp2, Fp3, Fp4, Fp5, Fp6, Fp7,
        // Sixteen bytes each.
        Xmm0, Xmm1, Xmm2, Xmm3, Xmm4, Xmm5, Xmm6, Xmm7,
        // A DescriptorTable each.
        Gdtr, Ldtr,
        Count
    };
};
// Page rights. The values are the ones the loader and VirtualProtect code of
// this port already pass around (read 1, write 2, execute 4).
struct Prot
{
    enum : unsigned { None = 0, Read = 1, Write = 2, Exec = 4, All = 7 };
};
// Result codes of every operation. Zero is success. The numbers and the texts
// of CpuErrorText are the ones Unicorn used, so failure messages, crash reports
// and the tests that read them stay the same whichever engine runs.
struct CpuError
{
    enum : unsigned
    {
        Ok = 0, NoMemory, Arch, Handle, Mode, Version,
        ReadUnmapped, WriteUnmapped, FetchUnmapped,
        Hook, InvalidInstruction, Map,
        WriteProtected, ReadProtected, FetchProtected,
        Argument, ReadUnaligned, WriteUnaligned, FetchUnaligned,
        HookExists, Resource, Exception, Overflow
    };
};
const char *CpuErrorText(unsigned error);
// True for the six errors of a guest memory access.
inline bool CpuMemoryError(unsigned error)
{
    return (error >= CpuError::ReadUnmapped && error <= CpuError::FetchUnmapped) ||
           (error >= CpuError::WriteProtected && error <= CpuError::FetchProtected);
}
// The kind of a faulting guest access, as the fault hook reports it.
struct CpuFault
{
    enum : unsigned
    {
        ReadUnmapped = 1, WriteUnmapped, FetchUnmapped,
        ReadProtected, WriteProtected, FetchProtected
    };
};
// GDTR and LDTR. The selector and the access flags matter for LDTR only.
struct DescriptorTable
{
    unsigned base = 0, limit = 0, selector = 0, flags = 0;
};
// A saved register state. What it holds belongs to the engine; the caller
// only allocates, saves, restores and frees it.
struct CpuContext;
enum class CpuBackend
{
    Unicorn,
    // FEXCore, macOS host only (tools/host_fex_cpu.cpp).
    Fex,
    // No processor: the guest memory of a process that runs no x86 code, the
    // DOS process until it loads an x86 module (src/x86_address_space.h).
    None,
    // No x86 processor at all: the process is an ARM64 program whose code
    // runs natively in a machine of the host (src/arm64_cpu.cpp,
    // docs/ARM64-APPS.md). The general registers of this interface are its
    // call frame towards the providers.
    Arm64,
    // Linux x86_64: direct compatibility-mode execution in the host process,
    // or ptrace execution for comparison and sanitizer builds.
    Native,
};
class Cpu;
// Hooks. Each one runs on the thread that called Run, inside Run, while the
// guest stands at the event. A hook may read and write registers and memory and
// may call Stop; returning lets the guest go on.
// The gate hook runs before every instruction in the gate range. Without a
// Stop the guest executes that instruction next.
using GateHook = void (*)(Cpu &, unsigned address, void *context);
// A guest access the engine refused. Returning true means the hook repaired
// the mapping and the access is tried again; false ends Run with the error.
using FaultHook = bool (*)(Cpu &, unsigned kind, unsigned address, unsigned size, void *context);
// INT n, INT3, INTO and the single-step trap of the guest.
using InterruptHook = void (*)(Cpu &, unsigned number, void *context);
using PortInputHook = unsigned (*)(Cpu &, unsigned port, unsigned size, void *context);
using PortOutputHook = void (*)(Cpu &, unsigned port, unsigned size, unsigned value, void *context);
// The block budget of Budget is used up, and the guest stands before the
// block at address. An engine without a budget calls this before every block
// instead; SetBudgetHook says which of the two it does.
using BudgetHook = void (*)(Cpu &, unsigned address, void *context);
// Instrumentation for measuring runners: every guest write, every instruction
// or every block inside a range. They cost what they say and are not part of
// an ordinary run; an engine may refuse them with CpuError::Hook.
using WriteHook = void (*)(Cpu &, unsigned address, unsigned size, void *context);
using TraceHook = void (*)(Cpu &, unsigned address, unsigned size, void *context);
enum class TraceKind
{
    Instruction,
    Block,
};
class Cpu
{
  public:
    virtual ~Cpu();
    virtual CpuBackend Backend() const = 0;
    // Whether the engine runs code in 16-bit segments of the LDT. FEXCore
    // decodes every code segment as a flat 32-bit one; the pair of
    // src/x86_cpu_dual.cpp runs such code on its second engine.
    virtual bool Runs16Bit() const { return Backend() != CpuBackend::Fex; }

    // Registers. gpr points at the eight general registers in place, each in
    // a 64-bit slot whose low half is the 32-bit value; a write has to leave
    // the upper half zero. eip points at EIP the same way and may only be
    // read through it. Both are null when the engine cannot hand them out, and
    // then Get and Set are the only way.
    unsigned long long *gpr = nullptr;
    const unsigned long long *eip = nullptr;
    // Guest memory the host may touch in place, for an engine that keeps it
    // in host memory of its own (FEXCore). Where page_read[address >> 12] is
    // not zero the guest may read that 4 KiB page and its bytes lie at
    // memory + address. page_write says the same for a write that needs
    // nothing else done: the guest may write the page, no translated code
    // has to go and no write has to be reported. For every other page, and
    // where the pointers are null, Read and Write are the way.
    unsigned char *memory = nullptr;
    const unsigned char *page_read = nullptr, *page_write = nullptr;
    virtual unsigned Get(int reg, unsigned &value) = 0;
    virtual unsigned Set(int reg, unsigned value) = 0;
    virtual unsigned GetWide(int reg, void *value) = 0;
    virtual unsigned SetWide(int reg, const void *value) = 0;

    // Contexts: the whole processor state of a thread, for fibers and for
    // callbacks that have to come back to the state before them.
    virtual unsigned NewContext(CpuContext *&context) = 0;
    virtual void FreeContext(CpuContext *context) = 0;
    virtual unsigned Save(CpuContext *context) = 0;
    virtual unsigned Restore(const CpuContext *context) = 0;
    // One 32-bit register of a saved context.
    virtual unsigned ContextGet(const CpuContext *context, int reg, unsigned &value) = 0;

    // Guest memory. Addresses and sizes are page aligned for the mapping
    // calls. MapHost maps host bytes the caller keeps alive, so two engines
    // can share them. Write is the host writing: it ignores the guest's page
    // rights, and translated code of the range stays until InvalidateCode.
    virtual unsigned Map(unsigned address, unsigned size, unsigned prot) = 0;
    virtual unsigned MapHost(unsigned address, unsigned size, unsigned prot, void *host) = 0;
    // Device aperture: callbacks handle guest accesses in byte order. One
    // aperture per engine; addresses passed to the callback are absolute.
    using DeviceAccess = void (*)(void *, unsigned address, unsigned char *, unsigned size, bool write);
    virtual unsigned MapDevice(unsigned, unsigned, DeviceAccess, void *) { return CpuError::Hook; }

    virtual unsigned Unmap(unsigned address, unsigned size) = 0;
    virtual unsigned Protect(unsigned address, unsigned size, unsigned prot) = 0;
    // Loader-only exception for a complete 16-KiB group of private PE data
    // pages. Bits in read_only identify the originally read-only 4-KiB pages;
    // the other pages are read/write. VirtualQuery keeps the loader's rights.
    virtual unsigned ImageDataPageSize() const { return 0; }
    unsigned RelaxImageData(unsigned address, unsigned read_only);
    // Native API output validation still uses the original guest rights.
    bool ImageDataReadOnly(unsigned address) const
    {
        const unsigned base = address & ~16383u, bit = 1u << ((address >> 12) & 3);
        for (const auto *group = image_data_groups; group; group = group->next)
            if (group->base == base) return (group->read_only & bit) != 0;
        return false;
    }
    // Any protection/backing change revokes the exception for the whole group.
    unsigned RevokeImageData(unsigned address, unsigned long long size);
    unsigned ImageDataWriteWatch(bool active);
    virtual unsigned Read(unsigned address, void *out, unsigned size) = 0;
    virtual unsigned Write(unsigned address, const void *data, unsigned size) = 0;
    virtual unsigned InvalidateCode(unsigned address, unsigned size) = 0;
    virtual unsigned FlushCode() = 0;
    // The host address of guest bytes the caller may read and write directly,
    // or null where the engine cannot say.
    virtual void *HostPointer(unsigned address, unsigned size) = 0;

    // Execution. Run starts at begin and returns when a hook calls Stop, when
    // the guest reaches until, or with the error of an access or instruction
    // it could not complete; EIP then stands at the instruction that failed.
    // instructions, when not zero, bounds the run for a measuring caller.
    virtual unsigned Run(unsigned begin, unsigned until, unsigned instructions = 0) = 0;
    // From a hook, on the running thread: the run ends before the instruction
    // or block the hook was called for.
    virtual void Stop() = 0;
    // From any thread. The run ends at the next point where the engine looks,
    // at the latest at the end of the budget. Cleared by the next Run.
    virtual void RequestStop() = 0;
    // Blocks the guest may run before the budget hook, less one. Only
    // meaningful when SetBudgetHook reported a counting engine.
    virtual void Budget(int blocks) = 0;

    // Hooks, one of each kind; installing one replaces the one before.
    // They must be in place before the first Run translates code.
    virtual unsigned SetGateHook(unsigned begin, unsigned end, GateHook, void *context) = 0;
    // Execute guest bodies in the gate range directly. The caller must leave
    // a trapping instruction at every entry that needs the gate hook.
    virtual unsigned SetGateCodeExecution(bool) { return CpuError::Hook; }
    // A directory of 16-byte API slots with RET/RET n or HLT entries. Serve
    // each entry and complete its RET without tracing a guest instruction.
    virtual unsigned SetReturnGateExecution(bool) { return CpuError::Hook; }
    virtual unsigned SetFaultHook(FaultHook, void *context) = 0;
    virtual unsigned SetInterruptHook(InterruptHook, void *context) = 0;
    virtual unsigned SetPortHooks(PortInputHook, PortOutputHook, void *context) = 0;
    // On the way in, counted says whether the caller can work with a counted
    // budget; on the way out, whether the engine counts it (true) or calls the
    // hook before every block (false).
    virtual unsigned SetBudgetHook(BudgetHook, void *context, bool &counted) = 0;
    // A native CPU can yield at a hardware timer trap instead of tracing
    // blocks. The hook sees the next guest instruction, and may call Stop.
    // An engine without this facility keeps the block-budget contract above.
    virtual unsigned SetTimeSliceHook(BudgetHook, void *) { return CpuError::Hook; }
    // An engine may report only the first write to a page after arming, and
    // may report a whole host page for it (FEXCore with 16 KiB pages); the
    // caller re-arms a range once it has forgotten its writes. Unicorn reports
    // every write and needs no re-arming.
    virtual unsigned WatchWrites(WriteHook, void *context) = 0;
    virtual void UnwatchWrites() = 0;
    virtual void RearmWrites(unsigned, unsigned) {}
    // The state of the engine that belongs to the host stack a Run is on, for
    // a fiber that yields while one of its Runs is still open (a gate served
    // from inside the engine, Application::DirectGate). DetachFrame takes it
    // away and leaves the engine idle for the next fiber; AttachFrame gives
    // it back when the fiber resumes. An engine that never keeps a Run open
    // across a yield has nothing to keep.
    virtual void *DetachFrame() { return nullptr; }
    virtual void AttachFrame(void *) {}
    virtual unsigned AddTrace(TraceKind, unsigned begin, unsigned end, TraceHook, void *context) = 0;

    // Saved states (src/x86_state.h). A state
    // carries of a processor its guest memory and its registers, both read
    // through this interface, and makes a new processor of the same kind
    // when it is loaded; translated code is not part of it.
    // Null when the engine can be saved as it stands, else the reason.
    virtual const char *StateRefusal() const { return "its engine cannot be saved"; }
    // A saved context may belong to a different engine from the live one.
    // Check it separately before writing its portable register state.
    virtual const char *ContextStateRefusal(const CpuContext *) const { return StateRefusal(); }
    // Every run of mapped guest pages with the same rights and the same
    // backing: host is null for pages of the engine's own and else the host
    // bytes MapHost was given for the first page of the run.
    using RegionVisitor = void (*)(void *context, unsigned address, unsigned bytes, unsigned prot, void *host);
    virtual bool Regions(RegionVisitor, void *context)
    {
        (void)context;
        return false;
    }
    // Saved memory also carries the provenance of relaxed read-only pages,
    // so a VirtualProtect after restore still revokes the whole exception.
    static constexpr unsigned SavedImageReadOnly = 0x100;
    bool SavedRegions(RegionVisitor, void *context);
    unsigned MapSaved(unsigned address, unsigned size, unsigned prot);
    // The bytes of a saved register state, for an engine whose registers
    // cannot be set one by one from outside (Unicorn at privilege three).
    // They mean something to a processor of the same engine and build only.
    // Zero bytes: the state travels register by register (Get, Set).
    virtual unsigned ContextBytes() { return 0; }
    // A pair may keep a flat context portably and a secondary context with
    // hidden segment caches. ContextBytes still gives the import envelope.
    virtual unsigned ContextStateBytes(const CpuContext *) { return ContextBytes(); }
    virtual bool ContextExport(const CpuContext *, void *out)
    {
        (void)out;
        return false;
    }
    virtual bool ContextImport(CpuContext *, const void *in)
    {
        (void)in;
        return false;
    }
  private:
    struct ImageDataGroup { unsigned base, read_only; ImageDataGroup *next; };
    ImageDataGroup *image_data_groups = nullptr;
    bool image_data_watched = false;
    void RememberImageData(unsigned address, unsigned read_only);
};
// The sum of the pauses of the machine in microseconds :
// what RDTSC and RDTSCP of every engine stay behind the host's counter, so the
// guest's counter stands while the machine does. Application::PauseMachine
// sets it when a pause ends; no guest code runs in between.
void SetCounterPause(unsigned long long microseconds);
unsigned long long CounterPause();
// A new processor in 32-bit protected mode with nothing mapped. purpose says
// what it is for; NEO98_TRACE_CPU=1 prints it with the engine on a host, so a
// run shows what still needed Unicorn.
unsigned CreateCpu(CpuBackend, Cpu *&cpu, const char *purpose = nullptr);
// A host provider adds an engine this way; CreateCpu answers CpuError::Arch
// for one that no provider registered.
using CpuFactory = unsigned (*)(Cpu *&cpu);
void RegisterCpuBackend(CpuBackend, CpuFactory);
// The engine of a new 32-bit Win32 process: FEXCore wherever a provider for it
// is linked (the macOS host), Unicorn otherwise. NEO98_CPU=unicorn chooses
// Unicorn anyway, NEO98_CPU=fex insists on FEXCore. Win16 always uses
// Unicorn; the DOS process starts with None and gets Unicorn with its first
// x86 module.
CpuBackend ProcessCpuBackend();
// The processor of a process whose main engine cannot run 16-bit code: the
// main engine keeps the memory and runs the 32-bit code, a second engine
// (Unicorn), made on the first 16-bit instruction, runs the code of the
// hosted 16-bit modules over the same bytes. Takes ownership of main.
// src/x86_cpu_dual.cpp.
Cpu *CreateDualCpu(Cpu *main);
} // namespace X86
