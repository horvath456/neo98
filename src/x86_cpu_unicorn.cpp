// SPDX-License-Identifier: GPL-2.0-or-later
// The Unicorn engine behind X86::Cpu.
#include "x86_cpu.h"
#include "x86_address_space.h"
#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../third_party/unicorn/include/unicorn/unicorn.h"
#include "jit_memory.h"

namespace X86
{
namespace
{
// The numbering of CpuError is Unicorn's; these pin it.
static_assert(CpuError::ReadUnmapped == UC_ERR_READ_UNMAPPED && CpuError::FetchUnmapped == UC_ERR_FETCH_UNMAPPED &&
                  CpuError::Map == UC_ERR_MAP && CpuError::WriteProtected == UC_ERR_WRITE_PROT &&
                  CpuError::FetchProtected == UC_ERR_FETCH_PROT && CpuError::Argument == UC_ERR_ARG &&
                  CpuError::Exception == UC_ERR_EXCEPTION && CpuError::Overflow == UC_ERR_OVERFLOW,
              "CpuError follows uc_err");
static_assert(Prot::Read == UC_PROT_READ && Prot::Write == UC_PROT_WRITE && Prot::Exec == UC_PROT_EXEC &&
                  Prot::All == UC_PROT_ALL,
              "Prot follows uc_prot");
constexpr int engine_registers[Reg::Count] = {
    UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX,
    UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI,
    UC_X86_REG_EIP, UC_X86_REG_EFLAGS,
    UC_X86_REG_ES, UC_X86_REG_CS, UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_FS, UC_X86_REG_GS,
    UC_X86_REG_CR0,
    UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FPTAG, UC_X86_REG_FIP, UC_X86_REG_FCS,
    UC_X86_REG_FDP, UC_X86_REG_FDS, UC_X86_REG_FOP, UC_X86_REG_MXCSR,
    UC_X86_REG_DR0, UC_X86_REG_DR1, UC_X86_REG_DR2, UC_X86_REG_DR3, UC_X86_REG_DR6, UC_X86_REG_DR7,
    UC_X86_REG_FP0, UC_X86_REG_FP1, UC_X86_REG_FP2, UC_X86_REG_FP3,
    UC_X86_REG_FP4, UC_X86_REG_FP5, UC_X86_REG_FP6, UC_X86_REG_FP7,
    UC_X86_REG_XMM0, UC_X86_REG_XMM1, UC_X86_REG_XMM2, UC_X86_REG_XMM3,
    UC_X86_REG_XMM4, UC_X86_REG_XMM5, UC_X86_REG_XMM6, UC_X86_REG_XMM7,
    UC_X86_REG_GDTR, UC_X86_REG_LDTR};
static_assert(Reg::Edi == 7 && Reg::Ldtr + 1 == Reg::Count, "register table");

unsigned FaultKind(uc_mem_type type)
{
    switch (type)
    {
    case UC_MEM_READ_UNMAPPED: return CpuFault::ReadUnmapped;
    case UC_MEM_WRITE_UNMAPPED: return CpuFault::WriteUnmapped;
    case UC_MEM_FETCH_UNMAPPED: return CpuFault::FetchUnmapped;
    case UC_MEM_READ_PROT: return CpuFault::ReadProtected;
    case UC_MEM_WRITE_PROT: return CpuFault::WriteProtected;
    case UC_MEM_FETCH_PROT: return CpuFault::FetchProtected;
    default: return 0;
    }
}

// The functions of the patch groups "budget" and "registers" of
// tools/build_unicorn_host.py. Other builds of the engine (the pip package of
// the guest tests under tools/) lack them, so on the host they are looked up at
// run time.
using BudgetCallback = void (*)(uc_engine *, uint64_t, uint32_t, void *);
struct EngineExtensions
{
    void (*budget)(uc_engine *, int) = nullptr;
    void (*budget_hook)(uc_engine *, BudgetCallback, void *) = nullptr;
    uint64_t *(*registers)(uc_engine *, uint64_t **) = nullptr;
};
const EngineExtensions &Extensions()
{
    static const EngineExtensions found = [] {
        EngineExtensions e;
        e.budget = reinterpret_cast<void (*)(uc_engine *, int)>(dlsym(RTLD_DEFAULT, "uc_neo98_budget"));
        e.budget_hook = reinterpret_cast<void (*)(uc_engine *, BudgetCallback, void *)>(
            dlsym(RTLD_DEFAULT, "uc_neo98_budget_hook"));
        e.registers = reinterpret_cast<uint64_t *(*)(uc_engine *, uint64_t **)>(
            dlsym(RTLD_DEFAULT, "uc_neo98_x86_registers"));

        if (!e.budget || !e.budget_hook)
            e.budget = nullptr, e.budget_hook = nullptr;
        return e;
    }();
    return found;
}

class UnicornCpu final : public Cpu
{
  public:
    unsigned ImageDataPageSize() const override
    {
#if defined(__APPLE__)
        return getpagesize() == 16384 ? 16384 : 0;
#else
        return 0;
#endif
    }
    uc_engine *uc = nullptr;
    // The hooks this engine forwards. Each Unicorn callback below gets the
    // UnicornCpu as its data and calls the hook of the port with its own.
    struct
    {
        GateHook hook = nullptr;
        void *context = nullptr;
        uc_hook handle = 0;
    } gate;
    struct
    {
        FaultHook hook = nullptr;
        void *context = nullptr;
        uc_hook handle = 0;
    } fault;
    struct
    {
        InterruptHook hook = nullptr;
        void *context = nullptr;
        uc_hook handle = 0;
    } interrupt;
    struct
    {
        PortInputHook input = nullptr;
        PortOutputHook output = nullptr;
        void *context = nullptr;
        uc_hook in_handle = 0, out_handle = 0;
    } ports;
    struct
    {
        BudgetHook hook = nullptr;
        void *context = nullptr;
        uc_hook handle = 0;
        bool counted = false;
    } budget;
    struct
    {
        WriteHook hook = nullptr;
        void *context = nullptr;
        uc_hook handle = 0;
    } writes;
    struct Trace
    {
        TraceHook hook = nullptr;
        void *context = nullptr;
        UnicornCpu *cpu = nullptr;
        Trace *next = nullptr;
    };
    Trace *traces = nullptr;
    // Written by any thread, read by the emulating one.
    bool stop_requested = false;

    ~UnicornCpu() override
    {
        free(host_maps);
        if (uc)
            uc_close(uc);
        while (traces)
        {
            Trace *next = traces->next;
            delete traces;
            traces = next;
        }
    }
    CpuBackend Backend() const override { return CpuBackend::Unicorn; }
    // Saved states (src/x86_state.h). Unicorn does not say which of its
    // regions the caller backs with bytes of its own, so MapHost and Unmap
    // keep that list here.
    struct HostMap
    {
        unsigned long long begin, end;
        unsigned char *host;
    };
    HostMap *host_maps = nullptr;
    unsigned host_map_count = 0, host_map_capacity = 0;
    void NoteHost(unsigned long long begin, unsigned long long end, unsigned char *host)
    {
        if (host_map_count == host_map_capacity)
        {
            host_map_capacity = host_map_capacity ? host_map_capacity * 2 : 64;
            host_maps = static_cast<HostMap *>(realloc(host_maps, host_map_capacity * sizeof(HostMap)));
        }
        host_maps[host_map_count++] = {begin, end, host};
    }
    void ForgetHost(unsigned long long begin, unsigned long long end)
    {
        const unsigned count = host_map_count;
        for (unsigned i = 0; i < count; ++i)
        {
            HostMap map = host_maps[i];
            if (map.end <= begin || map.begin >= end)
                continue;
            // What is left of the mapping in front of and behind the range.
            host_maps[i].begin = host_maps[i].end = 0;
            if (map.begin < begin)
                NoteHost(map.begin, begin, map.host);
            if (map.end > end)
                NoteHost(end, map.end, map.host + (end - map.begin));
        }
        unsigned kept = 0;
        for (unsigned i = 0; i < host_map_count; ++i)
            if (host_maps[i].end > host_maps[i].begin)
                host_maps[kept++] = host_maps[i];
        host_map_count = kept;
    }
    DeviceAccess device_access = nullptr;
    void *device_context = nullptr;
    unsigned device_base = 0;
    unsigned MapDevice(unsigned address, unsigned size, DeviceAccess access, void *context) override
    {
        if (device_access) return CpuError::Argument;
        device_access = access; device_context = context; device_base = address;
        return uc_mmio_map(uc, address, size,
            [](uc_engine *, uint64_t offset, unsigned size, void *opaque) -> uint64_t {
                auto &self = *static_cast<UnicornCpu *>(opaque);
                uint64_t value = 0;
                self.device_access(self.device_context, self.device_base + unsigned(offset),
                                   reinterpret_cast<unsigned char *>(&value), size, false);
                return value;
            }, this,
            [](uc_engine *, uint64_t offset, unsigned size, uint64_t value, void *opaque) {
                auto &self = *static_cast<UnicornCpu *>(opaque);
                self.device_access(self.device_context, self.device_base + unsigned(offset),
                                   reinterpret_cast<unsigned char *>(&value), size, true);
            }, this);
    }
    const char *StateRefusal() const override { return nullptr; }
    bool Regions(RegionVisitor each, void *context) override
    {
        uc_mem_region *regions = nullptr;
        uint32_t count = 0;
        if (uc_mem_regions(uc, &regions, &count) != UC_ERR_OK)
            return false;
        for (uint32_t r = 0; r < count; ++r)
        {
            // Device bytes live in the application's typed device state.
            if (device_access && regions[r].begin == device_base) continue;
            unsigned long long at = regions[r].begin;
            const unsigned long long end = regions[r].end + 1;
            while (at < end)
            {
                unsigned long long stop = end;
                unsigned char *host = nullptr;
                for (unsigned i = 0; i < host_map_count; ++i)
                {
                    const auto &map = host_maps[i];
                    if (at >= map.begin && at < map.end)
                    {
                        host = map.host + (at - map.begin);
                        stop = map.end < end ? map.end : end;
                        break;
                    }
                    if (map.begin > at && map.begin < stop)
                        stop = map.begin;
                }
                each(context, (unsigned)at, (unsigned)(stop - at), regions[r].perms & 7, host);
                at = stop;
            }
        }
        uc_free(regions);
        return true;
    }
    // A context is the register file of the engine up to the end of what a
    // reset clears, behind a head of the library's own.
    unsigned ContextBytes() override
    {
        uc_context *probe = nullptr;
        if (uc_context_alloc(uc, &probe) != UC_ERR_OK)
            return 0;
        const size_t bytes = *reinterpret_cast<const size_t *>(probe);
        uc_context_free(probe);
        return bytes < uc_context_size(uc) ? (unsigned)bytes : 0;
    }
    bool ContextExport(const CpuContext *context, void *out) override
    {
        const size_t bytes = *reinterpret_cast<const size_t *>(context), whole = uc_context_size(uc);
        if (bytes >= whole)
            return false;
        memcpy(out, reinterpret_cast<const char *>(context) + (whole - bytes), bytes);
        return true;
    }
    bool ContextImport(CpuContext *context, const void *in) override
    {
        const size_t bytes = *reinterpret_cast<const size_t *>(context), whole = uc_context_size(uc);
        if (bytes >= whole)
            return false;
        memcpy(reinterpret_cast<char *>(context) + (whole - bytes), in, bytes);
        return true;
    }
    unsigned Open()
    {
        unsigned error = uc_open(UC_ARCH_X86, UC_MODE_32, &uc);
        if (error)
            return error;
        // Set the translation buffer size before engine initialization.
        error = uc_ctl_set_tcg_buffer_size(uc, NEO98_JIT_CACHE_BYTES);
        if (error)
            return error;
        // The first register access initialises the engine; only then does it
        // have the CPU whose registers the patch group hands out.
        unsigned long long value = 0;
        error = uc_reg_read(uc, UC_X86_REG_EAX, &value);
        if (error)
            return error;
        if (const auto registers = Extensions().registers)
        {
            uint64_t *where = nullptr;
            gpr = reinterpret_cast<unsigned long long *>(registers(uc, &where));
            eip = gpr ? reinterpret_cast<const unsigned long long *>(where) : nullptr;
        }
        return CpuError::Ok;
    }

    // UC_MODE_32 reads the low half of a register and writes it zero-extended.
    // A narrower register (a selector, an x87 word) fills only its own bytes
    // and FIP and FDP fill eight, so the value goes through a zeroed 64-bit
    // slot; little-endian, its low half is the register either way.
    unsigned Get(int reg, unsigned &value) override
    {
        value = 0;
        if (reg < 0 || reg > Reg::Dr7)
            return CpuError::Argument;
        unsigned long long slot = 0;
        const unsigned error = uc_reg_read(uc, engine_registers[reg], &slot);
        value = (unsigned)slot;
        return error;
    }
    // uc_reg_write of EIP also makes a running engine leave its block, which
    // is why EIP is never written through the pointer.
    unsigned Set(int reg, unsigned value) override
    {
        if (reg < 0 || reg > Reg::Dr7)
            return CpuError::Argument;
        const unsigned long long slot = value;
        return uc_reg_write(uc, engine_registers[reg], &slot);
    }
    unsigned GetWide(int reg, void *value) override
    {
        if (reg == Reg::Gdtr || reg == Reg::Ldtr)
        {
            uc_x86_mmr table = {};
            const unsigned error = uc_reg_read(uc, engine_registers[reg], &table);
            auto &out = *static_cast<DescriptorTable *>(value);
            out.base = (unsigned)table.base;
            out.limit = table.limit;
            out.selector = table.selector;
            out.flags = table.flags;
            return error;
        }
        if (reg < Reg::Fp0 || reg >= Reg::Count)
            return CpuError::Argument;
        return uc_reg_read(uc, engine_registers[reg], value);
    }
    unsigned SetWide(int reg, const void *value) override
    {
        if (reg == Reg::Gdtr || reg == Reg::Ldtr)
        {
            const auto &in = *static_cast<const DescriptorTable *>(value);
            uc_x86_mmr table = {};
            table.base = in.base;
            table.limit = in.limit;
            table.selector = (uint16_t)in.selector;
            table.flags = in.flags;
            return uc_reg_write(uc, engine_registers[reg], &table);
        }
        if (reg < Reg::Fp0 || reg >= Reg::Count)
            return CpuError::Argument;
        return uc_reg_write(uc, engine_registers[reg], value);
    }

    unsigned NewContext(CpuContext *&context) override
    {
        uc_context *made = nullptr;
        const unsigned error = uc_context_alloc(uc, &made);
        context = reinterpret_cast<CpuContext *>(made);
        return error;
    }
    void FreeContext(CpuContext *context) override
    {
        if (context)
            uc_context_free(reinterpret_cast<uc_context *>(context));
    }
    unsigned Save(CpuContext *context) override
    {
        return uc_context_save(uc, reinterpret_cast<uc_context *>(context));
    }
    unsigned Restore(const CpuContext *context) override
    {
        return uc_context_restore(uc, reinterpret_cast<uc_context *>(const_cast<CpuContext *>(context)));
    }
    unsigned ContextGet(const CpuContext *context, int reg, unsigned &value) override
    {
        value = 0;
        if (reg < 0 || reg > Reg::Dr7)
            return CpuError::Argument;
        unsigned long long slot = 0;
        const unsigned error = uc_context_reg_read(reinterpret_cast<uc_context *>(const_cast<CpuContext *>(context)),
                                                   engine_registers[reg], &slot);
        value = (unsigned)slot;
        return error;
    }

    unsigned Map(unsigned address, unsigned size, unsigned prot) override
    {
        return uc_mem_map(uc, address, size, prot);
    }
    unsigned MapHost(unsigned address, unsigned size, unsigned prot, void *host) override
    {
        if (const unsigned error = RevokeImageData(address, size)) return error;
        const unsigned error = uc_mem_map_ptr(uc, address, size, prot, host);
        if (!error)
            NoteHost(address, (unsigned long long)address + size, static_cast<unsigned char *>(host));
        return error;
    }
    unsigned Unmap(unsigned address, unsigned size) override
    {
        if (const unsigned error = RevokeImageData(address, size)) return error;
        const unsigned error = uc_mem_unmap(uc, address, size);
        if (!error)
            ForgetHost(address, (unsigned long long)address + size);
        return error;
    }
    unsigned Protect(unsigned address, unsigned size, unsigned prot) override
    {
        if (const unsigned error = RevokeImageData(address, size)) return error;
        return uc_mem_protect(uc, address, size, prot);
    }
    unsigned Read(unsigned address, void *out, unsigned size) override
    {
        return uc_mem_read(uc, address, out, size);
    }
    unsigned Write(unsigned address, const void *data, unsigned size) override
    {
        return uc_mem_write(uc, address, data, size);
    }
    unsigned InvalidateCode(unsigned address, unsigned size) override
    {
        return uc_ctl_remove_cache(uc, address, (unsigned long long)address + size);
    }
    unsigned FlushCode() override { return uc_ctl_flush_tb(uc); }
    // Unicorn keeps its RAM blocks to itself.
    void *HostPointer(unsigned, unsigned) override { return nullptr; }

    unsigned Run(unsigned begin, unsigned until, unsigned instructions) override
    {
        if (__atomic_exchange_n(&stop_requested, false, __ATOMIC_ACQ_REL))
            return CpuError::Ok;
        return uc_emu_start(uc, begin, until, 0, instructions);
    }
    void Stop() override { uc_emu_stop(uc); }
    // uc_emu_stop changes caller W^X state and emulator state, so another
    // thread cannot call it directly. Gate and budget callbacks check a
    // request flag.
    void RequestStop() override { __atomic_store_n(&stop_requested, true, __ATOMIC_RELEASE); }
    void Budget(int blocks) override
    {
        if (budget.counted)
            Extensions().budget(uc, blocks);
    }

    bool TakeStopRequest()
    {
        if (!__atomic_load_n(&stop_requested, __ATOMIC_ACQUIRE) ||
            !__atomic_exchange_n(&stop_requested, false, __ATOMIC_ACQ_REL))
            return false;
        uc_emu_stop(uc);
        return true;
    }
    static void GateCallback(uc_engine *, uint64_t address, uint32_t, void *data)
    {
        auto &cpu = *static_cast<UnicornCpu *>(data);
        if (cpu.TakeStopRequest())
            return;
        cpu.gate.hook(cpu, (unsigned)address, cpu.gate.context);
    }
    static bool FaultCallback(uc_engine *, uc_mem_type type, uint64_t address, int size, int64_t, void *data)
    {
        auto &cpu = *static_cast<UnicornCpu *>(data);
        return cpu.fault.hook(cpu, FaultKind(type), (unsigned)address, size > 0 ? (unsigned)size : 0u,
                              cpu.fault.context);
    }
    static void InterruptCallback(uc_engine *, uint32_t number, void *data)
    {
        auto &cpu = *static_cast<UnicornCpu *>(data);
        cpu.interrupt.hook(cpu, number, cpu.interrupt.context);
    }
    static uint32_t PortInputCallback(uc_engine *, uint32_t port, int size, void *data)
    {
        auto &cpu = *static_cast<UnicornCpu *>(data);
        return cpu.ports.input(cpu, port, (unsigned)size, cpu.ports.context);
    }
    static void PortOutputCallback(uc_engine *, uint32_t port, int size, uint32_t value, void *data)
    {
        auto &cpu = *static_cast<UnicornCpu *>(data);
        cpu.ports.output(cpu, port, (unsigned)size, value, cpu.ports.context);
    }
    static void BudgetCallbackOf(uc_engine *, uint64_t address, uint32_t, void *data)
    {
        auto &cpu = *static_cast<UnicornCpu *>(data);
        if (cpu.TakeStopRequest())
            return;
        cpu.budget.hook(cpu, (unsigned)address, cpu.budget.context);
    }
    static void WriteCallback(uc_engine *, uc_mem_type, uint64_t address, int size, int64_t, void *data)
    {
        auto &cpu = *static_cast<UnicornCpu *>(data);
        cpu.writes.hook(cpu, (unsigned)address, size > 0 ? (unsigned)size : 0u, cpu.writes.context);
    }
    static void TraceCallback(uc_engine *, uint64_t address, uint32_t size, void *data)
    {
        auto &trace = *static_cast<Trace *>(data);
        trace.hook(*trace.cpu, (unsigned)address, size, trace.context);
    }

    void Drop(uc_hook &handle)
    {
        if (handle)
            uc_hook_del(uc, handle);
        handle = 0;
    }
    unsigned SetGateHook(unsigned begin, unsigned end, GateHook hook, void *context) override
    {
        Drop(gate.handle);
        gate.hook = hook;
        gate.context = context;
        return hook ? uc_hook_add(uc, &gate.handle, UC_HOOK_CODE, (void *)GateCallback, this, begin, end)
                    : CpuError::Ok;
    }
    unsigned SetFaultHook(FaultHook hook, void *context) override
    {
        Drop(fault.handle);
        fault.hook = hook;
        fault.context = context;
        return hook ? uc_hook_add(uc, &fault.handle, UC_HOOK_MEM_INVALID, (void *)FaultCallback, this, 1, 0)
                    : CpuError::Ok;
    }
    unsigned SetInterruptHook(InterruptHook hook, void *context) override
    {
        Drop(interrupt.handle);
        interrupt.hook = hook;
        interrupt.context = context;
        return hook ? uc_hook_add(uc, &interrupt.handle, UC_HOOK_INTR, (void *)InterruptCallback, this, 1, 0)
                    : CpuError::Ok;
    }
    unsigned SetPortHooks(PortInputHook input, PortOutputHook output, void *context) override
    {
        Drop(ports.in_handle);
        Drop(ports.out_handle);
        ports.input = input;
        ports.output = output;
        ports.context = context;
        unsigned error = CpuError::Ok;
        if (input)
            error = uc_hook_add(uc, &ports.in_handle, UC_HOOK_INSN, (void *)PortInputCallback, this, 1, 0,
                                UC_X86_INS_IN);
        if (!error && output)
            error = uc_hook_add(uc, &ports.out_handle, UC_HOOK_INSN, (void *)PortOutputCallback, this, 1, 0,
                                UC_X86_INS_OUT);
        return error;
    }
    // With the group "budget" the translated code counts blocks down and
    // calls back only before the block that finds the budget used up;
    // without it the hook is a block hook, called before every block.
    unsigned SetBudgetHook(BudgetHook hook, void *context, bool &counted) override
    {
        Drop(budget.handle);
        budget.hook = hook;
        budget.context = context;
        const auto &e = Extensions();
        budget.counted = counted = counted && hook && e.budget_hook;
        if (budget.counted)
        {
            e.budget_hook(uc, BudgetCallbackOf, this);
            return CpuError::Ok;
        }
        if (e.budget_hook)
            e.budget_hook(uc, nullptr, nullptr);
        return hook ? uc_hook_add(uc, &budget.handle, UC_HOOK_BLOCK, (void *)BudgetCallbackOf, this, 1, 0)
                    : CpuError::Ok;
    }
    unsigned WatchWrites(WriteHook hook, void *context) override
    {
        if (const unsigned error = ImageDataWriteWatch(hook != nullptr)) return error;
        Drop(writes.handle);
        writes.hook = hook;
        writes.context = context;
        return uc_hook_add(uc, &writes.handle, UC_HOOK_MEM_WRITE, (void *)WriteCallback, this, 1, 0);
    }
    void UnwatchWrites() override { ImageDataWriteWatch(false); Drop(writes.handle); }
    unsigned AddTrace(TraceKind kind, unsigned begin, unsigned end, TraceHook hook, void *context) override
    {
        auto *trace = new Trace{hook, context, this, traces};
        uc_hook handle = 0;
        const unsigned error = uc_hook_add(uc, &handle, kind == TraceKind::Block ? UC_HOOK_BLOCK : UC_HOOK_CODE,
                                           (void *)TraceCallback, trace, begin, end);
        if (error)
            delete trace;
        else
            traces = trace;
        return error;
    }
};
} // namespace

unsigned CreateUnicornCpu(Cpu *&cpu)
{
    auto *made = new UnicornCpu;
    const unsigned error = made->Open();
    if (error) { delete made; return error; }
    cpu = made;
    return CpuError::Ok;
}
} // namespace X86
