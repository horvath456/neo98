// SPDX-License-Identifier: GPL-2.0-or-later
// The processor of an ARM64 process (X86::CpuBackend::Arm64).
//
// An ARM64 program is built for the Win64 ABI; the providers of this port
// read a 32-bit stdcall frame from the stack of the guest. This engine stands
// between the two. It keeps the eight general "registers" of X86::Cpu as the
// call frame registers Application::Dispatch and Application::Call work with
// (ESP the frame, EAX the result, EIP where to go on), and converts by the
// table tools/arm64_sdk.py generates from sdk/api:
//
// call `svc` in the gate of an import: arguments from x0..x7, v0..v7
// and the stack into a 32-bit frame below the stack pointer of
// the program, structs that differ converted into the same frame;
// then the gate hook, as for an x86 process. When the provider
// has answered, EIP is the resume address: outputs go back, the
// result is widened into x0, the program continues.
// callback Application::Call puts arguments and a return mark on the stack
// and starts the processor at the procedure: arguments go into
// x0..x7, x30 is the mark, which is a gate; the `svc` there is
// the return.
//
// Nothing here knows macOS: the code of the program runs in an Arm64::Machine
// a host provides (src/arm64_machine.h). docs/ARM64-APPS.md.
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arm64_api.h"
#include "arm64_cpu.h"
#include "arm64_machine.h"
#include "arm64_unwind.h"
#include "arm64_critical_leaf.h"
#include "../build/generated/arm64_api_table.inc"

namespace Arm64
{
namespace
{
using X86::CpuError;
using X86::CpuFault;
using X86::Prot;
using X86::Reg;
using U64 = unsigned long long;

MachineFactory machine_factory = nullptr;
MachineShutdown machine_shutdown = nullptr;

bool SameName(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b)
    {
        unsigned x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return false;
    }
    return !*a && !*b;
}
int FindFunction(const char *dll, const char *name)
{
    for (unsigned i = 0; i < arm64_api_function_count; ++i)
        if (!strcmp(arm64_api_functions[i].name, name) && SameName(arm64_api_functions[i].dll, dll))
            return (int)i;
    return -1;
}
const Arm64Api::Message *FindMessage(unsigned message)
{
    for (unsigned i = 0; i < arm64_api_message_count; ++i)
        if (arm64_api_messages[i].message == message)
            return &arm64_api_messages[i];
    return nullptr;
}
int FindCallbackKind(const char *name)
{
    for (unsigned i = 0; i < arm64_api_callback_count; ++i)
        if (!strcmp(arm64_api_callbacks[i].name, name))
            return (int)i;
    return -1;
}

// The 64-bit part of a thread's TEB block, where x18 points. The 32-bit TEB
// of the runtime occupies the start of the same page.
constexpr unsigned Teb64Offset = 0x800;
constexpr unsigned TebSelf = 0x30, TebStackBase = 0x08, TebStackLimit = 0x10;
// The open call or callback of the thread: a guest address of a Record.
constexpr unsigned TebRecord = 0x100;
// Distance kept below the stack pointer of the program before this engine
// writes: the ABI has no red zone, the margin is for a leaf that uses one
// anyway.
constexpr unsigned StackMargin = 256;
constexpr unsigned MaximumFixups = 20, MaximumSlots = 32, VarargSlots = 14;
constexpr unsigned RecordMagic = 0x34364141; // "AA64"
// What a thread may compute before the scheduler is asked. A turn of a
// thread is eight slices (Application::Run); eight of these are the 20 ms
// quantum of Windows 98.
constexpr unsigned long long SliceNanoseconds = 2500000;

// One open transition, on the stack of the program.
struct Fixup
{
    unsigned structure;  // index of the struct
    unsigned direction;  // Arm64Api::Direction
    unsigned narrow;     // address of the 32-bit form
    unsigned wide;       // address of the 64-bit form
    unsigned count;      // elements
};
struct Record
{
    unsigned magic;
    unsigned callback;   // 0: a call of the program into the system, 1: a callback
    unsigned previous;   // the record below this one, a guest address
    int function;        // index of the function, or of the callback kind; -1 untyped
    U64 pc, sp;          // where the program continues; its stack pointer
    unsigned frame;      // ESP at the transition
    unsigned return_gate; // callback: the mark the procedure returns to
    unsigned bytes;      // callback: bytes of arguments in the 32-bit frame
    unsigned fixup_count;
    unsigned index;      // window long: the index the program asked for
    // Callback: the complete registers before entering guest code. The SEH
    // walker needs these even when the callback belongs to a system call.
    unsigned saved;
    unsigned interrupted; // saved PC is an instruction, rather than a system gate
    Fixup fixups[MaximumFixups];
};

// A struct the system holds on to (Arm64Api::Keep): its lasting 32-bit form
// in the shadow region of the process, and the struct of the program.
constexpr unsigned ShadowSlotBytes = 64;
struct Shadow
{
    bool used = false;
    unsigned short structure = 0;
    unsigned wide = 0;
    // What the 32-bit form held when it was last carried to the program.
    unsigned char seen[ShadowSlotBytes] = {};
};

// What an entry of the area is.
struct AreaEntry
{
    // CoreEntry: an export the processor answers itself (src/arm64_seh.inc);
    // ReturnEntry: where an exception handler returns to.
    enum Type : unsigned char { Free = 0, CallbackEntry, AliasEntry, CoreEntry, ReturnEntry };
    Type type = Free;
    int kind = -1;         // callback kind, function index of an alias, or core function
    unsigned target = 0;   // callback: the procedure; alias: the gate of the provider
};
} // namespace

struct Context
{
    Registers registers;
    U64 slots[8];
    U64 eip;
    unsigned other[Reg::Count];
    X86::DescriptorTable gdt, ldt;
    unsigned teb;
    bool entry, live;
};

class Cpu final : public X86::Cpu
{
  public:
    explicit Cpu(Machine *m) : machine(m)
    {
        gpr = slots;
        eip = &eip_slot;
        memory = machine->Memory();
        page_read = machine->PageRead();
        page_write = machine->PageWrite();
    }
    ~Cpu() override
    {
        delete machine;
        free(bindings);
        free(entries);
        free(shadows);
    }
    X86::CpuBackend Backend() const override { return X86::CpuBackend::Arm64; }
    bool Runs16Bit() const override { return false; }

    unsigned Get(int reg, unsigned &value) override
    {
        if (reg < 0 || reg >= Reg::Count) return CpuError::Argument;
        if (reg < 8) value = (unsigned)slots[reg];
        else if (reg == Reg::Eip) value = (unsigned)eip_slot;
        else value = other[reg];
        return CpuError::Ok;
    }
    unsigned Set(int reg, unsigned value) override
    {
        if (reg < 0 || reg >= Reg::Count) return CpuError::Argument;
        if (reg < 8) slots[reg] = value;
        else if (reg == Reg::Eip)
        {
            // The process names where the processor goes on: a procedure to
            // enter, a gate, or the resume address of a finished call.
            eip_slot = value;
            entry = true;
        }
        else
        {
            other[reg] = value;
            // The FS selector of a thread names its TEB through the GDT.
            if (reg == Reg::Fs) LoadTeb(value);
        }
        return CpuError::Ok;
    }
    unsigned GetWide(int reg, void *value) override
    {
        if (reg == Reg::Gdtr) { *static_cast<X86::DescriptorTable *>(value) = gdt; return CpuError::Ok; }
        if (reg == Reg::Ldtr) { *static_cast<X86::DescriptorTable *>(value) = ldt; return CpuError::Ok; }
        if (reg >= Reg::Fp0 && reg < Reg::Gdtr) { memset(value, 0, 16); return CpuError::Ok; }
        return CpuError::Argument;
    }
    unsigned SetWide(int reg, const void *value) override
    {
        if (reg == Reg::Gdtr) { gdt = *static_cast<const X86::DescriptorTable *>(value); return CpuError::Ok; }
        if (reg == Reg::Ldtr) { ldt = *static_cast<const X86::DescriptorTable *>(value); return CpuError::Ok; }
        // The x87 and XMM registers of the interface have no counterpart: a
        // provider that returns a floating-point value has no signature here.
        if (reg >= Reg::Fp0 && reg < Reg::Gdtr) return CpuError::Ok;
        return CpuError::Argument;
    }

    unsigned NewContext(X86::CpuContext *&context) override
    {
        auto *c = static_cast<Context *>(calloc(1, sizeof(Context)));
        context = reinterpret_cast<X86::CpuContext *>(c);
        return c ? (unsigned)CpuError::Ok : (unsigned)CpuError::NoMemory;
    }
    void FreeContext(X86::CpuContext *context) override { free(context); }
    unsigned Save(X86::CpuContext *context) override
    {
        auto &c = *reinterpret_cast<Context *>(context);
        machine->SyncRegisters(registers);
        c.registers = registers;
        memcpy(c.slots, slots, sizeof slots);
        c.eip = eip_slot;
        memcpy(c.other, other, sizeof other);
        c.gdt = gdt;
        c.ldt = ldt;
        c.teb = teb;
        c.entry = entry;
        c.live = live;
        return CpuError::Ok;
    }
    unsigned Restore(const X86::CpuContext *context) override
    {
        const auto &c = *reinterpret_cast<const Context *>(context);
        machine->GeneralChanged(registers);
        registers = c.registers;
        machine->VectorsChanged(registers);
        memcpy(slots, c.slots, sizeof slots);
        eip_slot = c.eip;
        memcpy(other, c.other, sizeof other);
        gdt = c.gdt;
        ldt = c.ldt;
        teb = c.teb;
        entry = c.entry;
        live = c.live;
        return CpuError::Ok;
    }
    unsigned ContextGet(const X86::CpuContext *context, int reg, unsigned &value) override
    {
        const auto &c = *reinterpret_cast<const Context *>(context);
        if (reg < 0 || reg >= Reg::Count) return CpuError::Argument;
        value = reg < 8 ? (unsigned)c.slots[reg] : reg == Reg::Eip ? (unsigned)c.eip : c.other[reg];
        return CpuError::Ok;
    }

    unsigned Map(unsigned address, unsigned size, unsigned prot) override { return machine->Map(address, size, prot); }
    unsigned MapHost(unsigned address, unsigned size, unsigned prot, void *host) override
    {
        return machine->MapHost(address, size, prot, host);
    }
    unsigned Unmap(unsigned address, unsigned size) override { return machine->Unmap(address, size); }
    unsigned Protect(unsigned address, unsigned size, unsigned prot) override { return machine->Protect(address, size, prot); }
    unsigned Read(unsigned address, void *out, unsigned size) override { return machine->Read(address, out, size); }
    unsigned Write(unsigned address, const void *data, unsigned size) override { return machine->Write(address, data, size); }
    unsigned InvalidateCode(unsigned address, unsigned size) override
    {
        machine->InvalidateCode(address, size);
        return CpuError::Ok;
    }
    unsigned FlushCode() override { return CpuError::Ok; }
    void *HostPointer(unsigned address, unsigned size) override { return machine->HostPointer(address, size); }

    unsigned Run(unsigned begin, unsigned until, unsigned instructions) override;
    void Stop() override { stop = true; }
    void RequestStop() override { machine->RequestStop(); }
    // The slice of an ARM64 thread is time, not blocks: arming the budget
    // arms the timer of the machine for the next Run.
    void Budget(int) override { slice_armed = true; }

    unsigned SetGateHook(unsigned begin, unsigned end, X86::GateHook hook, void *context) override
    {
        gate_begin = begin;
        gate_end = end;
        gate_hook = hook;
        gate_context = context;
        return CpuError::Ok;
    }
    unsigned SetFaultHook(X86::FaultHook hook, void *context) override
    {
        fault_hook = hook;
        fault_context = context;
        return CpuError::Ok;
    }
    // A program on EL0 has no software interrupts and no ports of the PC.
    unsigned SetInterruptHook(X86::InterruptHook, void *) override { return CpuError::Ok; }
    unsigned SetPortHooks(X86::PortInputHook, X86::PortOutputHook, void *) override { return CpuError::Ok; }
    // The hook is called when a thread has computed for a slice of time
    // without a call into the system; the machine interrupts it for that.
    unsigned SetBudgetHook(X86::BudgetHook hook, void *context, bool &counted) override
    {
        budget_hook = hook;
        budget_context = context;
        counted = true;
        return CpuError::Ok;
    }
    unsigned WatchWrites(X86::WriteHook, void *) override { return CpuError::Hook; }
    void UnwatchWrites() override {}
    unsigned AddTrace(X86::TraceKind, unsigned, unsigned, X86::TraceHook, void *) override { return CpuError::Hook; }

    void Configure(const Layout &l)
    {
        layout = l;
        entry_capacity = layout.area_bytes / 16;
        entries = static_cast<AreaEntry *>(calloc(entry_capacity, sizeof(AreaEntry)));
        // Entry 0 is the resume address; no code runs there.
        entry_count = 1;
        shadow_capacity = layout.shadow_bytes / ShadowSlotBytes;
        shadows = shadow_capacity ? static_cast<Shadow *>(calloc(shadow_capacity, sizeof(Shadow))) : nullptr;
        if (!shadows) shadow_capacity = 0;
    }
    const char *Detail() const { return detail[0] ? detail : nullptr; }
    unsigned CallbackTarget(unsigned value, int &kind) const
    {
        kind = -1;
        if (!InArea(value) || (value - layout.area) & 15) return value;
        const unsigned index = (value - layout.area) / 16;
        if (!index || index >= entry_count || entries[index].type != AreaEntry::CallbackEntry) return value;
        kind = entries[index].kind;
        return entries[index].target;
    }
    unsigned CallbackEntry(unsigned target, int kind) { return Wrap(target, kind); }
    bool Fault(U64 &pc, U64 &sp, U64 &lr, unsigned &syndrome) const
    {
        if (!fault_valid) return false;
        pc = fault_pc;
        sp = fault_sp;
        lr = fault_lr;
        syndrome = fault_syndrome;
        return true;
    }
    // The entry of the area that stands for an export the processor answers
    // itself; zero for any other function.
    unsigned CoreGate(int function)
    {
        if (!(arm64_api_functions[function].flags & Arm64Api::CoreFunction)) return 0;
        const int which = CoreIndex(arm64_api_functions[function].name);
        if (which < 0) return 0;
        return SystemEntry(AreaEntry::CoreEntry, which);
    }
    bool TakeUnhandled(UnhandledException &out)
    {
        if (!unhandled.valid) return false;
        out = unhandled;
        unhandled = {};
        return true;
    }
    bool DeliverFault(unsigned code, unsigned parameters, const U64 *information, unsigned run_error, unsigned trap,
                      unsigned fault_kind)
    {
        Unwind::ExceptionRecord record = {};
        record.code = code;
        record.parameters = parameters > 15 ? 15 : parameters;
        for (unsigned i = 0; i < record.parameters; ++i) record.information[i] = information[i];
        const U64 pc = fault_valid ? fault_pc : registers.pc;
        record.address = pc;
        bool taken = false;
        Raise(record, false, run_error ? run_error : (unsigned)CpuError::Exception, trap, fault_kind, fault_syndrome, taken);
        if (taken) fault_valid = false;
        return taken;
    }
    // Windows TLS slots and LastError are words of this thread's TEB.
    // These leaf bodies keep the SDK's normal Windows imports intact;
    // only NEO98's resolved gates use its TEB layout. Invalid TLS indices
    // still enter the provider at the aligned SVC in the third slot.
    unsigned TlsLeaf(int function, unsigned provider_gate)
    {
        if (!SameName(arm64_api_functions[function].dll, "KERNEL32.DLL") ||
            strcmp(arm64_api_functions[function].name, "TlsGetValue")) return 0;
        for (unsigned i = 1; i < entry_count; ++i)
            if (entries[i].type == AreaEntry::AliasEntry && entries[i].kind == function)
                return layout.area + i * 16;
        if (!entries || entry_capacity - entry_count < 3) return 0;
        static const unsigned code[12] = {
            0x7101401fu, // cmp w0,#80
            0x540000e2u, // b.hs slow
            0xd11de250u, // sub x16,x18,#0x778: TEB32+88h
            0xb81d821fu, // stur wzr,[x16,#-40]: LastError=0
            0xb8605a00u, // ldr w0,[x16,w0,uxtw #2]
            0xd65f03c0u, // ret
            0, 0,
            0xd4000001u, // slow: svc #0
            0xd65f03c0u, // ret
            0, 0
        };
        const unsigned at = layout.area + entry_count * 16;
        if (machine->Write(at, code, sizeof code) != CpuError::Ok) return 0;
        for (unsigned i = 0; i < 3; ++i) entries[entry_count + i] = {AreaEntry::AliasEntry, function, provider_gate};
        entry_count += 3;
        machine->InvalidateCode(at, sizeof code);
        return at;
    }
    unsigned CriticalLeaf(int function, unsigned provider_gate)
    {
        if (!SameName(arm64_api_functions[function].dll, "KERNEL32.DLL")) return 0;
        const char *name = arm64_api_functions[function].name;
        const unsigned which = !strcmp(name, "EnterCriticalSection") ? 0 :
                               !strcmp(name, "LeaveCriticalSection") ? 1 : 2;
        if (which == 2) return 0;
        if (critical_leaves[which]) return critical_leaves[which];
        constexpr unsigned slots = Arm64Critical::Bytes / 16;
        if (!entries || entry_capacity - entry_count < slots) return 0;
        const unsigned at = layout.area + entry_count * 16;
        if (machine->Write(at, Arm64Critical::Code + which * Arm64Critical::Bytes / 4,
                           Arm64Critical::Bytes) != CpuError::Ok) return 0;
        for (unsigned i = 0; i < slots; ++i)
            entries[entry_count + i] = {AreaEntry::AliasEntry, function, provider_gate};
        entry_count += slots;
        machine->InvalidateCode(at, Arm64Critical::Bytes);
        critical_leaves[which] = at;
        return at;
    }
    unsigned FastLeaf(int function, unsigned provider_gate)
    {
        if (const unsigned at = TlsLeaf(function, provider_gate)) return at;
        return CriticalLeaf(function, provider_gate);
    }
    void BindLeaf(unsigned gate, int function)
    {
        const unsigned target = FastLeaf(function, gate);
        if (!target) return;
        // Keep the old RET at gate+4: the call resolving this binding is
        // still in flight and resumes there. Later calls take the branch.
        const long long distance = (long long)target - gate;
        if (distance < -0x8000000ll || distance >= 0x8000000ll) return;
        const unsigned branch = 0x14000000u | ((unsigned)(distance / 4) & 0x03ffffffu);
        if (machine->Write(gate, &branch, sizeof branch) == CpuError::Ok)
            machine->InvalidateCode(gate, sizeof branch);
    }
    unsigned AliasGate(int function, unsigned provider_gate)
    {
        if (const unsigned leaf = FastLeaf(function, provider_gate)) return leaf;
        for (unsigned i = 1; i < entry_count; ++i)
            if (entries[i].type == AreaEntry::AliasEntry && entries[i].kind == function)
                return layout.area + i * 16;
        if (!entries || entry_count >= entry_capacity) return 0;
        const unsigned index = entry_count;
        unsigned char code[16];
        FillGates(code, sizeof code);
        const unsigned address = layout.area + index * 16;
        if (machine->Write(address, code, sizeof code) != CpuError::Ok) return 0;
        machine->InvalidateCode(address, 16);
        entries[index] = {AreaEntry::AliasEntry, function, provider_gate};
        ++entry_count;
        return address;
    }

  private:
    Machine *machine;
    Registers registers;
    U64 slots[8] = {};
    U64 eip_slot = 0;
    unsigned other[Reg::Count] = {};
    X86::DescriptorTable gdt, ldt;
    unsigned teb = 0;
    // EIP was set from outside since the last Run: begin names an entry, a
    // gate or the resume address. Otherwise Run continues where it stopped.
    bool entry = false;
    bool stop = false;
    unsigned gate_begin = 0, gate_end = 0;
    X86::GateHook gate_hook = nullptr;
    void *gate_context = nullptr;
    X86::FaultHook fault_hook = nullptr;
    void *fault_context = nullptr;
    X86::BudgetHook budget_hook = nullptr;
    void *budget_context = nullptr;
    bool slice_armed = false;
    // The registers hold a point between two instructions of the program
    // (it was interrupted), not the state at a call: a procedure entered now
    // must hand every register back.
    bool live = false;
    Layout layout;
    // Function index per gate slot: -2 not looked up, -1 no signature.
    int *bindings = nullptr;
    AreaEntry *entries = nullptr;
    unsigned entry_count = 0, entry_capacity = 0;
    unsigned critical_leaves[2] = {};
    Shadow *shadows = nullptr;
    unsigned shadow_capacity = 0, shadow_live = 0;
    char detail[200] = {};
    // The filter of the process (SetUnhandledExceptionFilter), a procedure
    // of the program, and an exception no frame and no filter took.
    U64 unhandled_filter = 0;
    unsigned handler_return = 0;
    UnhandledException unhandled;
    bool fault_valid = false;
    U64 fault_pc = 0, fault_sp = 0, fault_lr = 0;
    unsigned fault_syndrome = 0;
    // A conversion met memory the program may not use.
    struct Refused
    {
        bool valid = false;
        unsigned kind = 0, address = 0;
    } refused;

    bool InGates(U64 address) const { return address >= gate_begin && address <= gate_end; }
    bool InArea(U64 address) const { return address >= layout.area && address < (U64)layout.area + layout.area_bytes; }
    void LoadTeb(unsigned selector)
    {
        unsigned char descriptor[8] = {};
        const unsigned base = gdt.base ? gdt.base : layout.descriptors;
        if (!base || machine->Read(base + (selector & ~7u), descriptor, 8) != CpuError::Ok) return;
        teb = descriptor[2] | descriptor[3] << 8 | descriptor[4] << 16 | (unsigned)descriptor[7] << 24;
    }
    unsigned Teb64() const { return teb + Teb64Offset; }
    // An entry of the area with `svc; ret` that belongs to the processor.
    unsigned SystemEntry(AreaEntry::Type type, int kind)
    {
        for (unsigned i = 1; i < entry_count; ++i)
            if (entries[i].type == type && entries[i].kind == kind) return layout.area + i * 16;
        if (!entries || entry_count >= entry_capacity) return 0;
        const unsigned index = entry_count;
        unsigned char code[16];
        FillGates(code, sizeof code);
        const unsigned address = layout.area + index * 16;
        if (machine->Write(address, code, sizeof code) != CpuError::Ok) return 0;
        machine->InvalidateCode(address, 16);
        entries[index] = {type, kind, 0};
        ++entry_count;
        return address;
    }

    // Memory of the program, with its rights: a pointer the program handed
    // over that it may not read or write is its fault, at that address.
    bool Allowed(unsigned address, unsigned size, unsigned need)
    {
        if (!size) return true;
        const unsigned last = address + size - 1;
        if (last < address) { refused = {true, need == Prot::Write ? (unsigned)CpuFault::WriteUnmapped : (unsigned)CpuFault::ReadUnmapped, address}; return false; }
        for (unsigned page = address & ~4095u;; page += 4096)
        {
            unsigned prot = 0;
            const bool mapped = machine->Rights(page, prot);
            if (!mapped || !(prot & need))
            {
                const unsigned at = page > address ? page : address;
                refused = {true, need == Prot::Write ? (mapped ? (unsigned)CpuFault::WriteProtected : (unsigned)CpuFault::WriteUnmapped)
                                                     : (mapped ? (unsigned)CpuFault::ReadProtected : (unsigned)CpuFault::ReadUnmapped), at};
                return false;
            }
            if (page == (last & ~4095u)) break;
        }
        return true;
    }
    bool Peek(unsigned address, void *out, unsigned size)
    {
        return Allowed(address, size, Prot::Read) && machine->Read(address, out, size) == CpuError::Ok;
    }
    bool Poke(unsigned address, const void *data, unsigned size)
    {
        return Allowed(address, size, Prot::Write) && machine->Write(address, data, size) == CpuError::Ok;
    }
    bool Peek32(unsigned address, unsigned &value) { return Peek(address, &value, 4); }
    bool Peek64(unsigned address, U64 &value) { return Peek(address, &value, 8); }
    bool Poke32(unsigned address, unsigned value) { return Poke(address, &value, 4); }
    bool Poke64(unsigned address, U64 value) { return Poke(address, &value, 8); }

    // A procedure of the program handed to the system becomes the address of
    // an entry of the area, which names the procedure and its kind. An
    // address that is no code of the program (a window procedure token of a
    // system class, a gate) stays as it is.
    unsigned Wrap(unsigned target, int kind)
    {
        if (!target || kind < 0 || InGates(target) || InArea(target)) return target;
        unsigned prot = 0;
        if (!machine->Rights(target & ~4095u, prot) || !(prot & Prot::Exec)) return target;
        for (unsigned i = 1; i < entry_count; ++i)
            if (entries[i].type == AreaEntry::CallbackEntry && entries[i].target == target && entries[i].kind == kind)
                return layout.area + i * 16;
        if (!entries || entry_count >= entry_capacity) return target;
        // ldr x16, [pc, #8]; br x16;.quad target: the entry is code as well,
        // for a program that calls what GetWindowLongPtr gave it.
        const unsigned index = entry_count;
        const unsigned code[4] = {0x58000050u, 0xd61f0200u, target, 0};
        const unsigned address = layout.area + index * 16;
        if (machine->Write(address, code, sizeof code) != CpuError::Ok) return target;
        machine->InvalidateCode(address, 16);
        entries[index] = {AreaEntry::CallbackEntry, kind, target};
        ++entry_count;
        return address;
    }
    unsigned Unwrap(unsigned value) const
    {
        if (!InArea(value) || (value - layout.area) & 15) return value;
        const unsigned index = (value - layout.area) / 16;
        if (index && index < entry_count && entries[index].type == AreaEntry::CallbackEntry) return entries[index].target;
        return value;
    }

    // One struct between its two layouts. narrow_to_wide says the direction.
    bool Convert(const Arm64Api::Struct &s, unsigned narrow, unsigned wide, bool to_wide)
    {
        unsigned char buffer[512];
        for (unsigned i = 0; i < s.field_count; ++i)
        {
            const auto &f = arm64_api_fields[s.first_field + i];
            const unsigned a = narrow + f.offset32, b = wide + f.offset64;
            if (f.kind == Arm64Api::Copy)
            {
                for (unsigned done = 0; done < f.value;)
                {
                    const unsigned n = f.value - done < sizeof buffer ? f.value - done : (unsigned)sizeof buffer;
                    if (to_wide ? !Peek(a + done, buffer, n) || !Poke(b + done, buffer, n)
                                : !Peek(b + done, buffer, n) || !Poke(a + done, buffer, n))
                        return false;
                    done += n;
                }
                continue;
            }
            if (f.kind == Arm64Api::SizeField)
            {
                if (to_wide ? !Poke32(b, s.size64) : !Poke32(a, s.size32)) return false;
                continue;
            }
            // The struct behind such a pointer crosses on its own (Deepen);
            // each side keeps its own pointer.
            if (f.kind == Arm64Api::DeepField) continue;
            if (to_wide)
            {
                unsigned value = 0;
                if (!Peek32(a, value)) return false;
                U64 out = value;
                if (f.kind == Arm64Api::SignExtend) out = (U64)(long long)(int)value;
                else if (f.kind == Arm64Api::FunctionField) out = Unwrap(value);
                if (!Poke64(b, out)) return false;
            }
            else
            {
                U64 value = 0;
                if (!Peek64(b, value)) return false;
                unsigned out = (unsigned)value;
                if (f.kind == Arm64Api::FunctionField) out = Wrap(out, f.value);
                if (!Poke32(a, out)) return false;
            }
        }
        return true;
    }

    // --- shadows ---------------------------------------------------------
    unsigned ShadowAddress(unsigned index) const { return layout.shadows + index * ShadowSlotBytes; }
    int ShadowOfWide(unsigned wide, unsigned structure) const
    {
        for (unsigned i = 0; i < shadow_capacity; ++i)
            if (shadows[i].used && shadows[i].wide == wide && shadows[i].structure == structure) return (int)i;
        return -1;
    }
    int ShadowOfNarrow(unsigned narrow, unsigned structure) const
    {
        if (narrow < layout.shadows || (narrow - layout.shadows) % ShadowSlotBytes) return -1;
        const unsigned index = (narrow - layout.shadows) / ShadowSlotBytes;
        return index < shadow_capacity && shadows[index].used && shadows[index].structure == structure ? (int)index : -1;
    }
    // The shadow of the struct of the program at `wide`, a new one if it has
    // none; -1 when the region is full or the struct does not fit a slot.
    int KeepShadow(unsigned wide, unsigned structure)
    {
        int index = ShadowOfWide(wide, structure);
        if (index >= 0) return index;
        if (arm64_api_structs[structure].size32 > ShadowSlotBytes) return -1;
        for (unsigned i = 0; i < shadow_capacity; ++i)
            if (!shadows[i].used)
            {
                shadows[i] = {};
                shadows[i].used = true;
                shadows[i].structure = (unsigned short)structure;
                shadows[i].wide = wide;
                ++shadow_live;
                return (int)i;
            }
        return -1;
    }
    // What the system wrote into a shadow since the program last saw it goes
    // to the struct of the program. Called before the program runs.
    bool CarryShadow(unsigned index)
    {
        auto &shadow = shadows[index];
        const auto &s = arm64_api_structs[shadow.structure];
        unsigned char now[ShadowSlotBytes];
        if (machine->Read(ShadowAddress(index), now, s.size32) != CpuError::Ok) return true;
        if (!memcmp(now, shadow.seen, s.size32)) return true;
        memcpy(shadow.seen, now, s.size32);
        return Convert(s, ShadowAddress(index), shadow.wide, true);
    }
    bool CarryShadows()
    {
        for (unsigned i = 0, live = 0; i < shadow_capacity && live < shadow_live; ++i)
            if (shadows[i].used)
            {
                ++live;
                if (!CarryShadow(i)) return false;
            }
        return true;
    }
    void DropShadow(unsigned index)
    {
        if (!shadows[index].used) return;
        shadows[index].used = false;
        --shadow_live;
    }
    bool When(const Arm64Api::Param &p, const unsigned *narrow, const unsigned *slot_of) const
    {
        return p.when_parameter == 0xff || (narrow[slot_of[p.when_parameter]] & p.when_mask) == p.when_value;
    }

    // The structs a struct points at (Arm64Api::DeepField) cross with it:
    // each gets a copy in the layout of the other side below `cursor`, the
    // pointer of the copy of the outer struct leads there, and a fixup of its
    // own carries it back.
    bool Deepen(Record &record, const Arm64Api::Struct &s, unsigned narrow, unsigned wide, unsigned direction, bool to_wide,
                unsigned &cursor)
    {
        for (unsigned i = 0; i < s.field_count; ++i)
        {
            const auto &f = arm64_api_fields[s.first_field + i];
            if (f.kind != Arm64Api::DeepField) continue;
            const auto &inner = arm64_api_structs[f.value];
            if (to_wide)
            {
                unsigned from = 0;
                if (!Peek32(narrow + f.offset32, from)) return false;
                U64 to = 0;
                if (from && record.fixup_count < MaximumFixups)
                {
                    cursor = (cursor - inner.size64) & ~15u;
                    if (!Convert(inner, from, cursor, true)) return false;
                    record.fixups[record.fixup_count++] = {f.value, direction, from, cursor, 1};
                    to = cursor;
                }
                if (!Poke64(wide + f.offset64, to)) return false;
            }
            else
            {
                U64 from = 0;
                if (!Peek64(wide + f.offset64, from)) return false;
                unsigned to = 0;
                if ((unsigned)from && record.fixup_count < MaximumFixups)
                {
                    cursor = (cursor - inner.size32) & ~15u;
                    if (!Convert(inner, cursor, (unsigned)from, false)) return false;
                    record.fixups[record.fixup_count++] = {f.value, direction, cursor, (unsigned)from, 1};
                    to = cursor;
                }
                if (!Poke32(narrow + f.offset32, to)) return false;
            }
        }
        return true;
    }
    // A message in its struct (GetMessage hands one out, DispatchMessage
    // takes one): a parameter that is the address of a struct the system
    // keeps names the shadow on the side of the system and the struct of the
    // program on its own side (MM_WOM_DONE and its WAVEHDR).
    bool MessageStruct(unsigned narrow, unsigned wide, bool to_wide)
    {
        unsigned message = 0;
        if (!Peek32(to_wide ? narrow + 4 : wide + 8, message)) return false;
        const auto *m = FindMessage(message);
        if (!m || !(m->direction & Arm64Api::Keep)) return true;
        const unsigned narrow_at = narrow + (m->lparam ? 12 : 8), wide_at = wide + (m->lparam ? 24 : 16);
        if (to_wide)
        {
            unsigned value = 0;
            if (!Peek32(narrow_at, value)) return false;
            const int index = ShadowOfNarrow(value, m->structure);
            return index < 0 || Poke64(wide_at, shadows[index].wide);
        }
        U64 value = 0;
        if (!Peek64(wide_at, value)) return false;
        const int index = ShadowOfWide((unsigned)value, m->structure);
        return index < 0 || Poke32(narrow_at, ShadowAddress((unsigned)index));
    }
    static const Arm64Api::Code *FindCode(int kind, int value)
    {
        for (unsigned i = 0; i < arm64_api_code_count; ++i)
            if (arm64_api_codes[i].callback == kind && arm64_api_codes[i].value == value) return &arm64_api_codes[i];
        return nullptr;
    }

    int Binding(unsigned gate)
    {
        const unsigned slot = (gate - gate_begin) / 16, capacity = (gate_end - gate_begin + 1) / 16;
        if (slot >= capacity) return -1;
        if (!bindings)
        {
            bindings = static_cast<int *>(malloc(sizeof(int) * capacity));
            if (!bindings) return -1;
            for (unsigned i = 0; i < capacity; ++i) bindings[i] = -2;
        }
        if (bindings[slot] == -2)
        {
            const char *dll = nullptr, *name = nullptr;
            bindings[slot] = layout.import_name && layout.import_name(layout.context, gate, dll, name) && dll && name
                                 ? FindFunction(dll, name) : -1;
            if (bindings[slot] < 0)
                snprintf(detail, sizeof detail, "No ARM64 signature for %s!%s", dll ? dll : "?", name ? name : "?");
            else BindLeaf(gate, bindings[slot]);
        }
        return bindings[slot];
    }

    // The top of the 32-bit frame area for a new transition: below the stack
    // pointer of the program, or below the frame of the open transition.
    bool WidenResult(unsigned kind, U64 &x0) const
    {
        const unsigned eax = (unsigned)slots[Reg::Eax], edx = (unsigned)slots[Reg::Edx];
        switch (kind)
        {
        case Arm64Api::Void: return true;
        case Arm64Api::Handle:
        case Arm64Api::SignedPtr: x0 = (U64)(long long)(int)eax; return true;
        case Arm64Api::Int64: x0 = (U64)edx << 32 | eax; return true;
        default: x0 = eax; return true;
        }
    }

    unsigned RaiseRefused()
    {
        // The address the conversion could not use is the fault; the program
        // stands at its call.
        const unsigned kind = refused.kind, address = refused.address;
        refused = {};
        return RaiseFault(kind, address, 4, 0);
    }
    unsigned RaiseFault(unsigned kind, U64 address, unsigned size, unsigned syndrome)
    {
        fault_valid = true;
        fault_pc = registers.pc;
        fault_sp = registers.sp;
        fault_lr = registers.x[30];
        fault_syndrome = syndrome;
        eip_slot = (unsigned)registers.pc;
        if (address >> 32)
        {
            // An address outside the 4 GiB of the process. The hook reports
            // 32 bits; the detail carries the whole address.
            snprintf(detail, sizeof detail, "ARM64 access outside the address space at %016llx, pc=%016llx", address, registers.pc);
            if (fault_hook) fault_hook(*this, kind, 0xffffffffu, size, fault_context);
        }
        else if (fault_hook && fault_hook(*this, kind, (unsigned)address, size, fault_context))
        {
            fault_valid = false;
            return CpuError::Ok;
        }
        switch (kind)
        {
        case CpuFault::ReadUnmapped: return CpuError::ReadUnmapped;
        case CpuFault::WriteUnmapped: return CpuError::WriteUnmapped;
        case CpuFault::FetchUnmapped: return CpuError::FetchUnmapped;
        case CpuFault::ReadProtected: return CpuError::ReadProtected;
        case CpuFault::WriteProtected: return CpuError::WriteProtected;
        default: return CpuError::FetchProtected;
        }
    }

    unsigned EnterCall(unsigned gate, int function, unsigned provider_gate);
    unsigned FinishCall(Record &record, unsigned at);
    unsigned EnterProcedure(unsigned begin);
    unsigned FinishProcedure(Record &record, unsigned at);
    bool MessageFixup(Record &record, unsigned message, unsigned &wparam32, unsigned &lparam32, U64 *wparam64,
                      U64 *lparam64, bool to_wide, unsigned &cursor);
    bool TopRecord(Record &record, unsigned &at)
    {
        at = 0;
        if (!teb) return false;
        unsigned char raw[4];
        if (machine->Read(Teb64() + TebRecord, raw, 4) != CpuError::Ok) return false;
        memcpy(&at, raw, 4);
        if (!at || machine->Read(at, &record, sizeof record) != CpuError::Ok || record.magic != RecordMagic)
        {
            at = 0;
            return false;
        }
        return true;
    }
    bool SetTopRecord(unsigned at) { return machine->Write(Teb64() + TebRecord, &at, 4) == CpuError::Ok; }
    unsigned DispatchEntry();
#include "arm64_seh.inc"
};

// A struct pointer parameter of a message, in one of the two directions. For
// a call of the program (to_wide false) the 64-bit struct of the program gets
// a 32-bit copy; for a callback the 32-bit struct of the system gets a 64-bit
// copy. cursor is the allocation pointer of the frame area, moving down.
bool Cpu::MessageFixup(Record &record, unsigned message, unsigned &wparam32, unsigned &lparam32, U64 *wparam64,
                       U64 *lparam64, bool to_wide, unsigned &cursor)
{
    const auto *m = FindMessage(message);
    if (!m) return true;
    const auto &s = arm64_api_structs[m->structure];
    unsigned &narrow_value = m->lparam ? lparam32 : wparam32;
    U64 *wide_value = m->lparam ? lparam64 : wparam64;
    if (m->direction & Arm64Api::Keep)
    {
        // The parameter is a struct the system kept: its shadow on the side
        // of the system, the struct itself on the side of the program.
        if (to_wide)
        {
            const int index = ShadowOfNarrow(narrow_value, m->structure);
            if (index >= 0) *wide_value = shadows[index].wide;
        }
        else
        {
            const int index = ShadowOfWide((unsigned)*wide_value, m->structure);
            if (index >= 0) narrow_value = ShadowAddress((unsigned)index);
        }
        return true;
    }
    if (record.fixup_count >= MaximumFixups) return true;
    Fixup fixup = {m->structure, m->direction, 0, 0, 1};
    if (to_wide)
    {
        if (!narrow_value) return true;
        cursor = (cursor - s.size64) & ~15u;
        fixup.narrow = narrow_value;
        fixup.wide = cursor;
        // The procedure may read every field, whatever the direction says:
        // the system filled the struct before it sent the message.
        if (!Convert(s, fixup.narrow, fixup.wide, true)) return false;
        *wide_value = cursor;
    }
    else
    {
        const unsigned wide = (unsigned)*wide_value;
        if (!wide) return true;
        cursor = (cursor - s.size32) & ~15u;
        fixup.narrow = cursor;
        fixup.wide = wide;
        if (!Convert(s, fixup.narrow, fixup.wide, false)) return false;
        narrow_value = cursor;
    }
    record.fixups[record.fixup_count++] = fixup;
    return true;
}

// `svc` in the gate of an import, or in an alias entry of the area.
unsigned Cpu::EnterCall(unsigned gate, int function, unsigned provider_gate)
{
    const auto &f = arm64_api_functions[function];
    Record record = {};
    record.magic = RecordMagic;
    record.function = function;
    record.pc = registers.pc;
    record.sp = registers.sp;
    Record below;
    unsigned below_at = 0;
    TopRecord(below, below_at);
    record.previous = below_at;
    if (registers.sp >> 32 || registers.sp < StackMargin + sizeof(Record) + 4096)
        return RaiseFault(CpuFault::WriteUnmapped, registers.sp, 8, 0);
    unsigned cursor = ((unsigned)registers.sp - StackMargin) & ~15u;
    cursor -= sizeof(Record);
    const unsigned record_at = cursor;

    // The arguments as the ABI passes them: integers in x0..x7, floating
    // point in v0..v7, the rest in 8-byte stack slots. A variadic function
    // takes everything in the integer registers.
    unsigned integer = 0, vector = 0, stacked = 0;
    bool failed = false;
    auto next_integer = [&]() -> U64 {
        if (integer < 8) return registers.x[integer++];
        U64 value = 0;
        if (!Peek64((unsigned)registers.sp + 8 * stacked++, value)) failed = true;
        return value;
    };
    auto next_vector = [&]() -> U64 {
        if (vector < 8) return registers.v[vector++].low;
        U64 value = 0;
        if (!Peek64((unsigned)registers.sp + 8 * stacked++, value)) failed = true;
        return value;
    };
    bool has_vector = false;
    for (unsigned i = 0; i < f.param_count; ++i)
    {
        const unsigned kind = arm64_api_params[f.first_param + i].kind;
        if (kind == Arm64Api::Float32 || kind == Arm64Api::Float64) has_vector = true;
    }
    if (has_vector) machine->SyncRegisters(registers);

    U64 wide[MaximumSlots] = {};
    unsigned narrow[MaximumSlots] = {}, slot_count = 0, slot_of[MaximumSlots] = {};
    for (unsigned i = 0; i < f.param_count && slot_count + 2 <= MaximumSlots; ++i)
    {
        const auto &p = arm64_api_params[f.first_param + i];
        slot_of[i] = slot_count;
        if (p.kind == Arm64Api::Varargs)
        {
            for (unsigned k = 0; k < VarargSlots && slot_count < MaximumSlots; ++k)
            {
                U64 value = 0;
                if (integer < 8) value = registers.x[integer++];
                else if (machine->Read((unsigned)registers.sp + 8 * stacked++, &value, 8) != CpuError::Ok) break;
                narrow[slot_count++] = (unsigned)value;
            }
            continue;
        }
        if (p.kind == Arm64Api::Float32 || p.kind == Arm64Api::Float64)
        {
            const U64 value = next_vector();
            wide[slot_count] = value;
            narrow[slot_count++] = (unsigned)value;
            if (p.kind == Arm64Api::Float64) narrow[slot_count++] = (unsigned)(value >> 32);
            continue;
        }
        const U64 value = next_integer();
        wide[slot_count] = value;
        // A count of bytes beyond 4 GiB must not become a small one: the
        // system would allocate, copy or report that much and say it did.
        narrow[slot_count++] = p.kind == Arm64Api::Size && value >> 32 ? 0xffffffffu : (unsigned)value;
        if (p.kind == Arm64Api::Int64) narrow[slot_count++] = (unsigned)(value >> 32);
    }
    if (failed) return RaiseRefused();

    // Converted parameters.
    for (unsigned i = 0; i < f.param_count; ++i)
    {
        const auto &p = arm64_api_params[f.first_param + i];
        const unsigned slot = slot_of[i];
        if (p.kind == Arm64Api::FunctionPointer)
        {
            int kind = p.auxiliary;
            if (p.direction == 1 && p.count_parameter != 0xff)
            {
                // The kind of procedure follows from the hook type.
                const int type = (int)narrow[slot_of[p.count_parameter]];
                for (unsigned k = 0; k < arm64_api_hook_kind_count; ++k)
                    if (arm64_api_hook_kinds[k].type == type) kind = arm64_api_hook_kinds[k].callback;
            }
            if (When(p, narrow, slot_of)) narrow[slot] = Wrap(narrow[slot], kind);
        }
        else if (p.kind != Arm64Api::StructPointer && p.direction == Arm64Api::Keep && narrow[slot])
        {
            // A pointer the system gave a procedure of the program, handed
            // on (lParam of CallNextHookEx): the system gets its own struct
            // back, with what the procedure changed, and what the system
            // changes after that returns to the copy of the program.
            Record open;
            for (unsigned at = below_at, depth = 0; at && depth < 16; ++depth)
            {
                if (machine->Read(at, &open, sizeof open) != CpuError::Ok || open.magic != RecordMagic) break;
                bool found = false;
                for (unsigned k = 0; open.callback && k < open.fixup_count; ++k) found = found || open.fixups[k].wide == narrow[slot];
                if (found)
                {
                    const unsigned given = narrow[slot];
                    for (unsigned k = 0; k < open.fixup_count; ++k)
                    {
                        const auto &fixup = open.fixups[k];
                        if (!Convert(arm64_api_structs[fixup.structure], fixup.narrow, fixup.wide, false)) return RaiseRefused();
                        if (fixup.wide == given) narrow[slot] = fixup.narrow;
                        if (record.fixup_count < MaximumFixups)
                            record.fixups[record.fixup_count++] = {fixup.structure, Arm64Api::Out, fixup.narrow, fixup.wide, 1};
                    }
                    break;
                }
                at = open.previous;
            }
        }
        else if (p.kind == Arm64Api::StructPointer && narrow[slot] && record.fixup_count < MaximumFixups &&
                 When(p, narrow, slot_of))
        {
            const auto &s = arm64_api_structs[p.auxiliary];
            if (p.direction & (Arm64Api::Keep | Arm64Api::Drop))
            {
                // The system keeps this struct: it works on the shadow. What
                // the program holds is what counts at the call.
                const int index = (p.direction & Arm64Api::Keep) ? KeepShadow(narrow[slot], p.auxiliary)
                                                                : ShadowOfWide(narrow[slot], p.auxiliary);
                if (index >= 0)
                {
                    const unsigned address = ShadowAddress((unsigned)index);
                    if (!Convert(s, address, narrow[slot], false)) return RaiseRefused();
                    machine->Read(address, shadows[index].seen, s.size32);
                    if (p.direction & Arm64Api::Drop)
                        record.fixups[record.fixup_count++] = {p.auxiliary, Arm64Api::Drop, address, narrow[slot], 1};
                    narrow[slot] = address;
                    continue;
                }
                // No shadow: the struct crosses for this call only.
            }
            unsigned count = 1;
            if (p.count_parameter != 0xff) count = narrow[slot_of[p.count_parameter]];
            if (count > 4096) count = 4096;
            if (!count) continue;
            cursor = (cursor - s.size32 * count) & ~15u;
            if (cursor < 8192 || cursor > record_at) return RaiseFault(CpuFault::WriteUnmapped, cursor, 4, 0);
            Fixup fixup = {p.auxiliary, p.direction, cursor, narrow[slot], count};
            if (p.direction & Arm64Api::In)
            {
                for (unsigned k = 0; k < count; ++k)
                    if (!Convert(s, fixup.narrow + k * s.size32, fixup.wide + k * s.size64, false)) return RaiseRefused();
                if ((int)p.auxiliary == arm64_api_msg_struct && count == 1 && !MessageStruct(fixup.narrow, fixup.wide, false))
                    return RaiseRefused();
                if (count == 1)
                {
                    record.fixups[record.fixup_count++] = fixup;
                    narrow[slot] = cursor;
                    if (!Deepen(record, s, fixup.narrow, fixup.wide, p.direction, false, cursor)) return RaiseRefused();
                    continue;
                }
            }
            else
            {
                // An output: the system fills it, and what it leaves alone is zero.
                static const unsigned char zero[256] = {};
                for (unsigned done = 0; done < s.size32 * count;)
                {
                    const unsigned n = s.size32 * count - done < sizeof zero ? s.size32 * count - done : (unsigned)sizeof zero;
                    if (!Poke(cursor + done, zero, n)) return RaiseRefused();
                    done += n;
                }
                // A size field is an input even of an output struct.
                for (unsigned k = 0; k < s.field_count; ++k)
                    if (arm64_api_fields[s.first_field + k].kind == Arm64Api::SizeField &&
                        !Poke32(cursor + arm64_api_fields[s.first_field + k].offset32, s.size32))
                        return RaiseRefused();
            }
            record.fixups[record.fixup_count++] = fixup;
            narrow[slot] = cursor;
        }
    }
    // A parameter that states the size of a converted struct states the size
    // of the layout the provider sees.
    for (unsigned i = 0; i < f.param_count; ++i)
    {
        const auto &p = arm64_api_params[f.first_param + i];
        if (p.kind == Arm64Api::StructPointer || p.kind == Arm64Api::Varargs || p.count_parameter == 0xff) continue;
        const auto &of = arm64_api_params[f.first_param + p.count_parameter];
        if (of.kind == Arm64Api::StructPointer && narrow[slot_of[i]] >= arm64_api_structs[of.auxiliary].size64)
            narrow[slot_of[i]] = arm64_api_structs[of.auxiliary].size32;
    }
    if (f.flags & Arm64Api::MessageFunction)
    {
        const unsigned at = slot_of[(f.flags >> 8) & 255];
        if (!MessageFixup(record, narrow[at], narrow[at + 1], narrow[at + 2], &wide[at + 1], &wide[at + 2], false, cursor))
            return RaiseRefused();
    }
    if ((f.flags & Arm64Api::ObjectFunction) && slot_count == 3)
    {
        // GetObject(handle, bytes, buffer): which struct the system fills
        // follows from the type of the object, and for a bitmap from the
        // room the program offers. The record keeps the type; FinishCall
        // converts the struct whose size the provider returned.
        const unsigned type = layout.object_type ? layout.object_type(layout.context, narrow[0]) : 0;
        record.index = type;
        const Arm64Api::Struct *chosen = nullptr;
        unsigned chosen_id = 0;
        bool listed = false;
        for (unsigned i = 0; i < arm64_api_object_count; ++i)
        {
            if (arm64_api_objects[i].type != type) continue;
            listed = true;
            const auto &s = arm64_api_structs[arm64_api_objects[i].structure];
            if (!chosen && narrow[1] >= s.size64)
            {
                chosen = &s;
                chosen_id = arm64_api_objects[i].structure;
            }
        }
        if (listed && narrow[2])
        {
            if (!chosen) narrow[1] = 0; // no room for the smallest answer
            else
            {
                cursor = (cursor - chosen->size32) & ~15u;
                if (cursor < 8192) return RaiseFault(CpuFault::WriteUnmapped, cursor, 4, 0);
                unsigned char zero[128] = {};
                if (chosen->size32 > sizeof zero || !Poke(cursor, zero, chosen->size32)) return RaiseRefused();
                // Direction zero: FinishCall decides what goes back.
                record.fixups[record.fixup_count++] = {chosen_id, 0, cursor, narrow[2], 1};
                narrow[1] = chosen->size32;
                narrow[2] = cursor;
            }
        }
    }
    if (f.flags & (Arm64Api::WindowLongFunction | Arm64Api::ClassLongFunction))
    {
        // (hwnd, index[, value]). The window procedure travels as a callback
        // entry; a dialog keeps its procedure and its user value where the
        // 32-bit DWL_ indices say.
        int index = (int)narrow[1];
        record.index = (unsigned)index;
        const bool window = f.flags & Arm64Api::WindowLongFunction;
        const bool dialog = window && index >= 0 && layout.is_dialog && layout.is_dialog(layout.context, narrow[0]);
        if (dialog && (index == 8 || index == 16)) narrow[1] = (unsigned)(index / 2);
        const bool procedure = window ? index == -4 || (dialog && index == 8) : index == -24;
        if (procedure && slot_count >= 3)
            narrow[2] = Wrap(narrow[2], FindCallbackKind(dialog ? "DLGPROC" : "WNDPROC"));
    }

    // The frame: return address, then the slots.
    cursor -= 4 * (slot_count + 1);
    cursor &= ~3u;
    if (cursor < 8192) return RaiseFault(CpuFault::WriteUnmapped, cursor, 4, 0);
    const unsigned resume = ResumeAddress(layout);
    if (!Poke32(cursor, resume) || (slot_count && !Poke(cursor + 4, narrow, 4 * slot_count))) return RaiseRefused();
    record.frame = cursor;
    if (!Poke(record_at, &record, sizeof record)) return RaiseRefused();
    SetTopRecord(record_at);
    slots[Reg::Esp] = cursor;
    eip_slot = provider_gate;
    entry = false;
    (void)gate;
    if (gate_hook) gate_hook(*this, provider_gate, gate_context);
    return CpuError::Ok;
}

// The provider has answered: EIP is the resume address.
unsigned Cpu::FinishCall(Record &record, unsigned at)
{
    const auto &f = arm64_api_functions[record.function];
    for (unsigned i = 0; i < record.fixup_count; ++i)
    {
        const auto &fixup = record.fixups[i];
        if (fixup.direction == Arm64Api::Drop)
        {
            // The system let go of the struct, if the call succeeded.
            const int index = ShadowOfNarrow(fixup.narrow, fixup.structure);
            if (index >= 0 && !(unsigned)slots[Reg::Eax])
            {
                CarryShadow((unsigned)index);
                DropShadow((unsigned)index);
            }
            continue;
        }
        if (!(fixup.direction & Arm64Api::Out)) continue;
        const auto &s = arm64_api_structs[fixup.structure];
        for (unsigned k = 0; k < fixup.count; ++k)
            if (!Convert(s, fixup.narrow + k * s.size32, fixup.wide + k * s.size64, true))
            {
                registers.pc = record.pc;
                SetTopRecord(record.previous);
                return RaiseRefused();
            }
        if ((int)fixup.structure == arm64_api_msg_struct && fixup.count == 1) MessageStruct(fixup.narrow, fixup.wide, true);
    }
    U64 x0 = registers.x[0];
    WidenResult(f.result, x0);
    if ((f.flags & Arm64Api::SizeResult) && record.fixup_count && x0 == arm64_api_structs[record.fixups[0].structure].size32)
        x0 = arm64_api_structs[record.fixups[0].structure].size64;
    if (f.flags & Arm64Api::ObjectFunction)
    {
        // The provider returned the bytes of the struct it filled (a plain
        // bitmap answers with a BITMAP even where a DIBSECTION has room), or
        // the bytes a buffer needs.
        const unsigned bytes = (unsigned)slots[Reg::Eax];
        for (unsigned i = 0; i < arm64_api_object_count && bytes; ++i)
        {
            const auto &s = arm64_api_structs[arm64_api_objects[i].structure];
            if (arm64_api_objects[i].type != record.index || s.size32 != bytes) continue;
            if (record.fixup_count && !Convert(s, record.fixups[0].narrow, record.fixups[0].wide, true))
            {
                registers.pc = record.pc;
                SetTopRecord(record.previous);
                return RaiseRefused();
            }
            x0 = s.size64;
            break;
        }
    }
    if (f.flags & (Arm64Api::WindowLongFunction | Arm64Api::ClassLongFunction))
    {
        const int index = (int)record.index;
        const bool window = f.flags & Arm64Api::WindowLongFunction;
        if (window ? index == -4 || index == 8 : index == -24)
        {
            const unsigned value = Unwrap((unsigned)slots[Reg::Eax]);
            x0 = value;
        }
    }
    registers.x[0] = x0;
    registers.pc = record.pc;
    registers.sp = record.sp;
    slots[Reg::Esp] = ((unsigned)record.sp - StackMargin) & ~15u;
    SetTopRecord(record.previous);
    (void)at;
    return CpuError::Ok;
}

// Application::Call, the entry of the program or the routine of a thread:
// ESP points at a return mark and the 32-bit arguments, begin is the procedure.
unsigned Cpu::EnterProcedure(unsigned begin)
{
    machine->SyncGeneral(registers);
    unsigned target = begin;
    int kind = -1;
    if (InArea(begin) && !((begin - layout.area) & 15))
    {
        const unsigned index = (begin - layout.area) / 16;
        if (index && index < entry_count && entries[index].type == AreaEntry::CallbackEntry)
        {
            target = entries[index].target;
            kind = entries[index].kind;
        }
    }
    const unsigned frame = (unsigned)slots[Reg::Esp];
    unsigned mark = 0;
    if (machine->Read(frame, &mark, 4) != CpuError::Ok) return RaiseFault(CpuFault::ReadUnmapped, frame, 4, 0);
    // No code of the program there: the token of a native window procedure
    // called as a function (Application::EnterNativeProcedure takes the
    // fetch fault and turns it into CallWindowProc), or a null pointer.
    unsigned target_rights = 0;
    if (!machine->Rights(target, target_rights))
    {
        registers.pc = target;
        return RaiseFault(CpuFault::FetchUnmapped, target, 4, 0);
    }
    if (!InGates(mark))
    {
        snprintf(detail, sizeof detail, "ARM64 procedure %08x entered with a return address %08x that is no gate", begin, mark);
        return CpuError::Exception;
    }
    Record record = {};
    record.magic = RecordMagic;
    record.callback = 1;
    record.function = kind;
    record.frame = frame;
    record.return_gate = mark;
    Record below;
    unsigned below_at = 0;
    TopRecord(below, below_at);
    record.previous = below_at;
    if (frame < StackMargin + sizeof(Record) + sizeof(Registers) + 8192) return RaiseFault(CpuFault::WriteUnmapped, frame, 4, 0);
    unsigned cursor = (frame - StackMargin) & ~15u;
    cursor -= sizeof(Record);
    const unsigned record_at = cursor;
    record.interrupted = live;
    {
        // Preserve the caller's nonvolatile registers across the native
        // callback frame, for both ordinary return and exception search.
        machine->SyncRegisters(registers);
        cursor = (cursor - sizeof(Registers)) & ~15u;
        if (!Poke(cursor, &registers, sizeof(Registers))) return RaiseRefused();
        record.saved = cursor;
        live = false;
    }

    U64 arguments[MaximumSlots] = {};
    unsigned argument_count = 0, vector = 0;
    bool vectors_written = false;
    if (kind >= 0)
    {
        const auto &cb = arm64_api_callbacks[kind];
        unsigned narrow[MaximumSlots] = {}, slot = 0;
        unsigned slot_total = 0;
        for (unsigned i = 0; i < cb.param_count; ++i)
        {
            const unsigned k = arm64_api_params[cb.first_param + i].kind;
            slot_total += k == Arm64Api::Int64 || k == Arm64Api::Float64 ? 2 : 1;
        }
        if (slot_total > MaximumSlots) slot_total = MaximumSlots;
        if (slot_total && machine->Read(frame + 4, narrow, 4 * slot_total) != CpuError::Ok)
            return RaiseFault(CpuFault::ReadUnmapped, frame + 4, 4, 0);
        record.bytes = 4 * slot_total;
        U64 wide[MaximumSlots] = {};
        unsigned first_slot[MaximumSlots] = {};
        for (unsigned i = 0; i < cb.param_count && slot < slot_total; ++i)
        {
            const auto &p = arm64_api_params[cb.first_param + i];
            first_slot[i] = slot;
            const unsigned value = narrow[slot];
            switch (p.kind)
            {
            case Arm64Api::Handle:
            case Arm64Api::SignedPtr: wide[i] = (U64)(long long)(int)value; ++slot; break;
            case Arm64Api::Int64:
            case Arm64Api::Float64: wide[i] = (U64)narrow[slot + 1] << 32 | value; slot += 2; break;
            case Arm64Api::StructPointer:
            {
                wide[i] = value;
                ++slot;
                if (p.direction & Arm64Api::Keep)
                {
                    const int index = ShadowOfNarrow(value, p.auxiliary);
                    if (index >= 0) wide[i] = shadows[index].wide;
                    break;
                }
                if (!value || record.fixup_count >= MaximumFixups) break;
                const auto &s = arm64_api_structs[p.auxiliary];
                cursor = (cursor - s.size64) & ~15u;
                Fixup fixup = {p.auxiliary, p.direction, value, cursor, 1};
                if (!Convert(s, fixup.narrow, fixup.wide, true)) return RaiseRefused();
                record.fixups[record.fixup_count++] = fixup;
                wide[i] = cursor;
                break;
            }
            default: wide[i] = value; ++slot; break;
            }
        }
        if (cb.message == 2 && cb.param_count >= 3)
        {
            // A hook procedure: its code says what lParam points at.
            const auto *code = FindCode(kind, (int)narrow[first_slot[0]]);
            const unsigned lparam = narrow[first_slot[2]];
            if (code && lparam && record.fixup_count < MaximumFixups)
            {
                const auto &s = arm64_api_structs[code->structure];
                cursor = (cursor - s.size64) & ~15u;
                const Fixup fixup = {code->structure, code->direction, lparam, cursor, 1};
                if (!Convert(s, fixup.narrow, fixup.wide, true)) return RaiseRefused();
                record.fixups[record.fixup_count++] = fixup;
                wide[2] = cursor;
                if (!Deepen(record, s, fixup.narrow, fixup.wide, code->direction, true, cursor)) return RaiseRefused();
            }
        }
        if (cb.message == 1 && cb.param_count >= 4)
        {
            unsigned wparam = narrow[first_slot[2]], lparam = narrow[first_slot[3]];
            if (!MessageFixup(record, narrow[first_slot[1]], wparam, lparam, &wide[2], &wide[3], true, cursor))
                return RaiseRefused();
        }
        unsigned integer = 0;
        for (unsigned i = 0; i < cb.param_count; ++i)
        {
            const unsigned k = arm64_api_params[cb.first_param + i].kind;
            if ((k == Arm64Api::Float32 || k == Arm64Api::Float64) && vector < 8)
            {
                if (!vectors_written) machine->SyncRegisters(registers);
                vectors_written = true;
                registers.v[vector].low = wide[i];
                registers.v[vector++].high = 0;
            }
            else if (integer < 8) registers.x[integer++] = wide[i];
            else arguments[argument_count++] = wide[i];
        }
    }
    else
    {
        // A procedure the table has no kind for: the entry of the program,
        // or a function pointer that reached the system through an untyped
        // value. Eight slots, each sign-extended; what lies beyond the real
        // arguments is whatever the stack holds.
        record.bytes = 0;
        unsigned narrow[8] = {};
        for (unsigned i = 0; i < 8; ++i)
            if (machine->Read(frame + 4 + 4 * i, &narrow[i], 4) != CpuError::Ok) break;
        for (unsigned i = 0; i < 8; ++i) registers.x[i] = (U64)(long long)(int)narrow[i];
    }
    if (vectors_written) machine->VectorsChanged(registers);
    // Arguments beyond the eighth, in 8-byte slots at the stack pointer.
    cursor -= 8 * argument_count;
    cursor &= ~15u;
    if (cursor < 8192) return RaiseFault(CpuFault::WriteUnmapped, cursor, 8, 0);
    if (argument_count && !Poke(cursor, arguments, 8 * argument_count)) return RaiseRefused();
    record.sp = cursor;
    record.pc = target;
    if (!Poke(record_at, &record, sizeof record)) return RaiseRefused();
    SetTopRecord(record_at);

    // x18 is the TEB of the thread, on every entry: the program may rely on it.
    const unsigned teb64 = Teb64();
    U64 self = 0;
    if (teb && machine->Read(teb64 + TebSelf, &self, 8) == CpuError::Ok && !self)
    {
        unsigned limits[2] = {};
        machine->Read(teb + 4, limits, 8);
        const U64 words[2] = {limits[0], limits[1]};
        self = teb64;
        machine->Write(teb64 + TebStackBase, &words[0], 8);
        machine->Write(teb64 + TebStackLimit, &words[1], 8);
        machine->Write(teb64 + TebSelf, &self, 8);
    }
    registers.x[18] = teb ? teb64 : 0;
    registers.x[29] = 0;
    registers.x[30] = mark;
    registers.sp = cursor;
    registers.pc = target;
    registers.pstate = 0;
    return CpuError::Ok;
}

// The procedure returned into its mark.
unsigned Cpu::FinishProcedure(Record &record, unsigned at)
{
    (void)at;
    for (unsigned i = 0; i < record.fixup_count; ++i)
    {
        const auto &fixup = record.fixups[i];
        if (!(fixup.direction & Arm64Api::Out)) continue;
        if (!Convert(arm64_api_structs[fixup.structure], fixup.narrow, fixup.wide, false))
        {
            SetTopRecord(record.previous);
            return RaiseRefused();
        }
    }
    const U64 x0 = registers.x[0];
    slots[Reg::Eax] = (unsigned)x0;
    if (record.function >= 0 && arm64_api_callbacks[record.function].result == Arm64Api::Int64)
        slots[Reg::Edx] = (unsigned)(x0 >> 32);
    // stdcall: the procedure took its arguments off the stack.
    slots[Reg::Esp] = record.frame + 4 + record.bytes;
    SetTopRecord(record.previous);
    if (record.saved)
    {
        machine->GeneralChanged(registers);
        if (!Peek(record.saved, &registers, sizeof(Registers))) return RaiseRefused();
        machine->VectorsChanged(registers);
        live = record.interrupted != 0;
    }
    eip_slot = record.return_gate;
    entry = false;
    if (gate_hook) gate_hook(*this, record.return_gate, gate_context);
    return CpuError::Ok;
}

// EIP was set from outside: decide what it names.
unsigned Cpu::DispatchEntry()
{
    for (unsigned rounds = 0; entry && !stop; ++rounds)
    {
        if (rounds > 64) return CpuError::Exception;
        entry = false;
        const unsigned begin = (unsigned)eip_slot;
        // The thread was interrupted: it goes on where its registers stand.
        if (begin == ContinueAddress(layout)) continue;
        if (begin == ResumeAddress(layout))
        {
            Record record;
            unsigned at = 0;
            if (TopRecord(record, at) && !record.callback)
            {
                const unsigned error = FinishCall(record, at);
                if (error) return error;
            }
            continue;
        }
        if (InGates(begin))
        {
            // A gate as a procedure: the frame on the stack is already the
            // one the provider reads (a class whose window procedure is
            // DefWindowProcA itself).
            if (gate_hook) gate_hook(*this, begin, gate_context);
            continue;
        }
        const unsigned error = EnterProcedure(begin);
        if (error) return error;
    }
    return CpuError::Ok;
}

unsigned Cpu::Run(unsigned begin, unsigned, unsigned)
{
    stop = false;
    detail[0] = 0;
    fault_valid = false;
    if (entry && (unsigned)eip_slot != begin)
        eip_slot = begin;
    if (unsigned error = DispatchEntry()) return error;
    machine->Slice(budget_hook && slice_armed ? SliceNanoseconds : 0);
    while (!stop)
    {
        // What the system wrote into structs it keeps reaches the program
        // before it runs again.
        if (shadow_live && !CarryShadows())
        {
            if (unsigned error = RaiseRefused()) return error;
        }
        Exit exit;
        machine->Run(registers, exit);
        switch (exit.event)
        {
        case Event::Stopped:
        case Event::Slice:
            for (unsigned at : critical_leaves)
                if (!Arm64Critical::Interrupted(registers, at, *machine)) return CpuError::Exception;
            // The frame register follows the stack of the program, so a
            // callback made while it stands here lands below what it uses.
            if (!(registers.sp >> 32) && registers.sp > StackMargin) slots[Reg::Esp] = ((unsigned)registers.sp - StackMargin) & ~15u;
            live = true;
            eip_slot = ContinueAddress(layout);
            if (exit.event == Event::Stopped) return CpuError::Ok;
            // The scheduler decides: it stops the run to switch threads, or
            // arms the next slice.
            if (budget_hook) budget_hook(*this, (unsigned)registers.pc, budget_context);
            break;
        case Event::Failed:
            snprintf(detail, sizeof detail, "ARM64 machine failed, error %u syndrome %08x pc=%016llx", exit.error,
                     exit.syndrome, registers.pc);
            return exit.error ? exit.error : (unsigned)CpuError::Exception;
        case Event::Illegal:
            fault_valid = true;
            fault_pc = registers.pc;
            fault_sp = registers.sp;
            fault_lr = registers.x[30];
            fault_syndrome = exit.syndrome;
            eip_slot = (unsigned)registers.pc;
            return CpuError::InvalidInstruction;
        case Event::Fault:
        {
            const unsigned error = RaiseFault(exit.kind, exit.address, exit.size, exit.syndrome);
            if (error) return error;
            break;
        }
        case Event::Call:
        {
            live = false;
            const U64 at = registers.pc - 4;
            if (InGates(at) && !(at & 15))
            {
                const unsigned gate = (unsigned)at;
                Record record;
                unsigned record_at = 0;
                // The mark of the open callback: its procedure returned.
                if (TopRecord(record, record_at) && record.callback && record.return_gate == gate)
                {
                    if (unsigned error = FinishProcedure(record, record_at)) return error;
                }
                else
                {
                    const int function = Binding(gate);
                    if (function < 0)
                    {
                        eip_slot = gate;
                        return CpuError::Exception;
                    }
                    if (unsigned error = EnterCall(gate, function, gate)) return error;
                }
            }
            else if (InArea(at) && !((at - layout.area) & 15) && (at - layout.area) / 16 < entry_count &&
                     entries[(at - layout.area) / 16].type == AreaEntry::AliasEntry)
            {
                const auto &alias = entries[(at - layout.area) / 16];
                if (unsigned error = EnterCall((unsigned)at, alias.kind, alias.target)) return error;
            }
            else if (InArea(at) && !((at - layout.area) & 15) && (at - layout.area) / 16 < entry_count &&
                     (entries[(at - layout.area) / 16].type == AreaEntry::CoreEntry ||
                      entries[(at - layout.area) / 16].type == AreaEntry::ReturnEntry))
            {
                // An export the processor answers itself, or the return of
                // an exception handler: no frame for a provider, the program
                // goes on where the registers stand afterwards.
                const auto &own = entries[(at - layout.area) / 16];
                if (unsigned error = own.type == AreaEntry::CoreEntry ? CoreCall(own.kind) : HandlerReturned()) return error;
                break;
            }
            else
            {
                // `svc` anywhere else is no service of this system.
                registers.pc = at;
                fault_valid = true;
                fault_pc = at;
                fault_sp = registers.sp;
                fault_lr = registers.x[30];
                fault_syndrome = exit.syndrome;
                eip_slot = (unsigned)at;
                return CpuError::InvalidInstruction;
            }
            if (unsigned error = DispatchEntry()) return error;
            break;
        }
        }
    }
    return CpuError::Ok;
}

// X86::CreateCpu(CpuBackend::Arm64) leads here wherever this file is linked.
namespace
{
struct Registration
{
    Registration() { X86::RegisterCpuBackend(X86::CpuBackend::Arm64, CreateCpu); }
} registration;
} // namespace

void RegisterMachine(MachineFactory factory, MachineShutdown shutdown)
{
    machine_factory = factory;
    machine_shutdown = shutdown;
}
void ShutdownMachines()
{
    if (machine_shutdown) machine_shutdown();
}
bool MachineAvailable() { return machine_factory != nullptr; }
namespace
{
unsigned long long counter_pause = 0;
}
void SetCounterPause(unsigned long long microseconds) { __atomic_store_n(&counter_pause, microseconds, __ATOMIC_RELAXED); }
unsigned long long CounterPause() { return __atomic_load_n(&counter_pause, __ATOMIC_RELAXED); }

void FillGates(unsigned char *bytes, unsigned size)
{
    // svc #0; ret; udf #0; udf #0
    static const unsigned code[4] = {0xd4000001u, 0xd65f03c0u, 0, 0};
    for (unsigned offset = 0; offset + 16 <= size; offset += 16)
        memcpy(bytes + offset, code, 16);
}

unsigned CreateCpu(X86::Cpu *&cpu)
{
    cpu = nullptr;
    if (!machine_factory) return CpuError::Arch;
    Machine *machine = nullptr;
    if (unsigned error = machine_factory(machine)) return error;
    cpu = new Cpu(machine);
    return CpuError::Ok;
}

static Cpu *Own(X86::Cpu &cpu)
{
    return cpu.Backend() == X86::CpuBackend::Arm64 ? static_cast<Cpu *>(&cpu) : nullptr;
}
void Configure(X86::Cpu &cpu, const Layout &layout)
{
    if (auto *own = Own(cpu)) own->Configure(layout);
}
const char *ErrorDetail(X86::Cpu &cpu)
{
    auto *own = Own(cpu);
    return own ? own->Detail() : nullptr;
}
bool FaultState(X86::Cpu &cpu, unsigned long long &pc, unsigned long long &sp, unsigned long long &lr,
                unsigned &syndrome)
{
    auto *own = Own(cpu);
    return own && own->Fault(pc, sp, lr, syndrome);
}
bool Described(const char *dll, const char *name, const char *&provider)
{
    const int index = FindFunction(dll, name);
    if (index < 0) return false;
    provider = arm64_api_functions[index].provider;
    return true;
}
unsigned CallbackTarget(X86::Cpu &cpu, unsigned entry, int &kind)
{
    kind = -1;
    auto *own = Own(cpu);
    return own ? own->CallbackTarget(entry, kind) : entry;
}
unsigned CallbackEntry(X86::Cpu &cpu, unsigned target, int kind)
{
    auto *own = Own(cpu);
    return own ? own->CallbackEntry(target, kind) : target;
}
unsigned AliasGate(X86::Cpu &cpu, const char *dll, const char *name, unsigned provider_gate)
{
    auto *own = Own(cpu);
    const int index = FindFunction(dll, name);
    return own && index >= 0 ? own->AliasGate(index, provider_gate) : 0;
}
unsigned CoreGate(X86::Cpu &cpu, const char *dll, const char *name)
{
    auto *own = Own(cpu);
    const int index = FindFunction(dll, name);
    return own && index >= 0 ? own->CoreGate(index) : 0;
}
bool DeliverFault(X86::Cpu &cpu, unsigned code, unsigned parameters, const unsigned long long *information,
                  unsigned run_error, unsigned trap, unsigned fault_kind)
{
    auto *own = Own(cpu);
    return own && own->DeliverFault(code, parameters, information, run_error, trap, fault_kind);
}
bool TakeUnhandled(X86::Cpu &cpu, UnhandledException &exception)
{
    auto *own = Own(cpu);
    return own && own->TakeUnhandled(exception);
}
} // namespace Arm64
