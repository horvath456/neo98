// SPDX-License-Identifier: GPL-2.0-or-later
// Two engines over one guest memory: the processor of a 32-bit process whose
// main engine cannot run 16-bit code (FEXCore) and that hosts 16-bit
// libraries (printer drivers, MCI drivers, the 16-bit half of a flat thunk;
// src/x86_win16_hosted.cpp).
//
// The main engine owns the memory. Every mapping goes there first, and the
// second engine (Unicorn, made on the first 16-bit run) maps the same host
// bytes through MapHost, taken from the main engine's HostPointer. So both
// see every byte the other writes, and a fault of the second engine is a
// fault of the guest as with Unicorn alone.
// Execution goes to the engine that can run the next instruction: code in a
// segment of the LDT (the 16-bit segments of the hosted modules and the gate
// selector of their calls) runs on the second engine, and so does the far
// return by which Win16HostedCall enters 16-bit code; everything else runs on
// the main engine. The live registers belong to one engine, the active one;
// a switch carries the processor state across, and a saved context
// remembers which engine it came from, so a restore makes that engine active
// again.
#include "x86_cpu.h"
#include "x86_cpu_state.h"
#include <stdlib.h>
#include <string.h>

namespace X86
{
namespace
{
constexpr unsigned PageBits = 12, Pages = 1u << (32 - PageBits);
// Per guest page: rights, main mapping, an actual second-engine mapping, and
// whether that mapping is out of date and has to go before the next 16-bit run.
constexpr unsigned char PageMapped = 8, PageMirrored = 16, PageDrop = 32;

struct DualContext
{
    CpuContext *part[2] = {};
    int engine = 0;
};

class DualCpu final : public Cpu
{
  public:
    Cpu *engines[2] = {};
    int active = 0;
    unsigned char *pages = nullptr;
    CpuContext *ring0[2] = {};
    int budget = -1;
    bool budget_set = false;
    struct
    {
        unsigned begin = 0, end = 0;
        GateHook hook = nullptr;
        void *context = nullptr;
    } gate;
    struct
    {
        FaultHook hook = nullptr;
        void *context = nullptr;
    } fault;
    struct
    {
        InterruptHook hook = nullptr;
        void *context = nullptr;
    } interrupt;
    struct
    {
        PortInputHook input = nullptr;
        PortOutputHook output = nullptr;
        void *context = nullptr;
    } ports;
    struct
    {
        BudgetHook hook = nullptr;
        void *context = nullptr;
        bool set = false, counted = false;
    } budget_hook;
    // The second engine without a block budget of its own (a Unicorn
    // without the group "budget" of tools/build_unicorn_host.py) calls its
    // hook before every block. When the main engine counts, the caller
    // expects a call only once the budget is used up (Application::Slice adds
    // the whole armed budget per call), so the pair counts for the second
    // engine: `second_left` blocks before the caller's hook.
    bool second_counted = true;
    int second_left = 0;
    static void SecondBlock(Cpu &, unsigned address, void *context)
    {
        auto &dual = *static_cast<DualCpu *>(context);
        if (--dual.second_left >= 0)
            return;
        dual.second_left = 0x7fffffff;
        dual.budget_hook.hook(dual, address, dual.budget_hook.context);
    }

    explicit DualCpu(Cpu *main)
    {
        engines[0] = main;
        gpr = main->gpr;
        eip = main->eip;
        // The memory is the main engine's; so is the host's way into it.
        memory = main->memory;
        page_read = main->page_read;
        page_write = main->page_write;
        pages = static_cast<unsigned char *>(calloc(Pages, 1));
    }
    ~DualCpu() override
    {
        for (int i = 0; i < 2; ++i)
            if (ring0[i])
                engines[i]->FreeContext(ring0[i]);
        delete engines[1];
        delete engines[0];
        free(pages);
    }
    Cpu &Main() const { return *engines[0]; }
    Cpu &Active() { return *engines[active]; }
    CpuBackend Backend() const override { return engines[0]->Backend(); }
    // A dormant second engine is a cache over the main engine's memory.
    // Its next entry carries the flat registers across and remaps those
    // same bytes, so merely having used it does not prevent a state.
    // The state supplier also checks every saved thread context: none may
    // still name a 16-bit code segment when this engine is reconstructed.
    static constexpr unsigned OpaqueLimit = 16384;
    struct SecondaryState
    {
        unsigned magic = 0x32554344, version = 1, engine = 1, bytes = 0, ring0_bytes = 0;
        CpuPortableRegisters flat;
        unsigned char secondary[OpaqueLimit] = {}, ring0[OpaqueLimit] = {};
    };
    const char *StateRefusal() const override
    {
        if (const char *why = Main().StateRefusal()) return why;
        return active && (!engines[1] || engines[1]->StateRefusal()) ?
            "the secondary engine has no register codec" : nullptr;
    }
    const char *ContextStateRefusal(const CpuContext *context) const override
    {
        const auto *dual = reinterpret_cast<const DualContext *>(context);
        if (!dual) return nullptr;
        if (dual->engine < 0 || dual->engine > 1 || !dual->part[dual->engine]) return "an invalid paired context";
        if (!dual->engine) return Main().ContextStateRefusal(dual->part[0]);
        if (!engines[1] || !dual->part[0] || engines[1]->ContextStateRefusal(dual->part[1]) ||
            !engines[1]->ContextBytes() || engines[1]->ContextBytes() > OpaqueLimit)
            return "a secondary context has no complete typed register state";
        return nullptr;
    }
    unsigned ContextBytes() override { return sizeof(SecondaryState); }
    unsigned ContextStateBytes(const CpuContext *context) override
    { return reinterpret_cast<const DualContext *>(context)->engine ? sizeof(SecondaryState) : 0; }
    bool Portable(CpuContext *context, CpuPortableRegisters &state, bool importing)
    {
        CpuContext *live = nullptr;
        if (Main().NewContext(live) || Main().Save(live)) { if (live) Main().FreeContext(live); return false; }
        bool okay = true;
        if (importing)
        {
            WriteCpuPortableRegisters(Main(), state);
            okay = Main().Save(context) == CpuError::Ok;
        }
        else
        {
            okay = Main().Restore(context) == CpuError::Ok;
            if (okay) ReadCpuPortableRegisters(Main(), state);
        }
        okay = Main().Restore(live) == CpuError::Ok && okay;
        Main().FreeContext(live);
        return okay;
    }
    bool ContextExport(const CpuContext *context, void *out) override
    {
        const auto *dual = reinterpret_cast<const DualContext *>(context);
        if (!dual || dual->engine != 1 || ContextStateRefusal(context)) return false;
        auto *state = static_cast<SecondaryState *>(out);
        *state = {};
        state->bytes = engines[1]->ContextBytes();
        if (!Portable(dual->part[0], state->flat, false) ||
            !engines[1]->ContextExport(dual->part[1], state->secondary)) return false;
        if (ring0[1])
        {
            state->ring0_bytes = state->bytes;
            if (!engines[1]->ContextExport(ring0[1], state->ring0)) return false;
        }
        return true;
    }
    bool ContextImport(CpuContext *context, const void *in) override
    {
        const auto &state = *static_cast<const SecondaryState *>(in);
        if (state.magic != 0x32554344 || state.version != 1 || state.engine != 1 ||
            !state.bytes || state.bytes > OpaqueLimit ||
            (state.ring0_bytes && state.ring0_bytes != state.bytes) || !Second() ||
            Sync() || engines[1]->ContextBytes() != state.bytes) return false;
        auto *dual = reinterpret_cast<DualContext *>(context);
        CpuPortableRegisters flat = state.flat;
        if (!Portable(dual->part[0], flat, true)) return false;
        if (!dual->part[1] && engines[1]->NewContext(dual->part[1])) return false;
        if (!engines[1]->ContextImport(dual->part[1], state.secondary)) return false;
        if (state.ring0_bytes)
        {
            if (!ring0[1] && engines[1]->NewContext(ring0[1])) return false;
            if (!engines[1]->ContextImport(ring0[1], state.ring0)) return false;
        }
        dual->engine = 1;
        return true;
    }
    bool Regions(RegionVisitor each, void *context) override { return engines[0]->Regions(each, context); }
    unsigned ImageDataPageSize() const override { return engines[0]->ImageDataPageSize(); }
    bool Runs16Bit() const override { return true; }

    // ---------------------------------------------------------- the second
    // The pages the main engine has, in runs of the same rights whose host
    // bytes lie in one piece, go to the second engine as shared bytes.
    unsigned MirrorPages(unsigned page, unsigned last)
    {
        Cpu *second = engines[1];
        if (!second)
            return CpuError::Ok;
        while (page <= last)
        {
            if ((pages[page] & (PageMapped | PageMirrored)) != PageMapped)
            {
                ++page;
                continue;
            }
            const unsigned char rights = pages[page] & 7;
            unsigned end = page + 1;
            while (end <= last && (pages[end] & (PageMapped | PageMirrored)) == PageMapped && (pages[end] & 7) == rights)
                ++end;
            const unsigned begin = page << PageBits, bytes = (end - page) << PageBits;
            if (void *host = Main().HostPointer(begin, bytes))
            {
                const unsigned error = second->MapHost(begin, bytes, rights, host);
                if (error)
                    return error;
                for (unsigned p = page; p < end; ++p) pages[p] |= PageMirrored;
            }
            else
                // A run whose bytes do not lie in one piece of the main
                // engine (a private page beside shared ones) goes page by
                // page; a page without host bytes stays unmapped there and
                // faults if 16-bit code reaches it.
                for (unsigned p = page; p < end; ++p)
                    if (void *one = Main().HostPointer(p << PageBits, 1u << PageBits))
                    {
                        const unsigned error = second->MapHost(p << PageBits, 1u << PageBits, rights, one);
                        if (error) return error;
                        pages[p] |= PageMirrored;
                    }
            page = end;
        }
        return CpuError::Ok;
    }
    // The second engine runs seldom (a flat thunk, a 16-bit driver call) and
    // the main engine maps and unmaps all the time: a Win32 heap commits and
    // decommits pages with every block, and each change of Unicorn's memory
    // rebuilds its whole region list. 3D Driving-School spent most of its
    // loading time there. So a change only marks its pages, and the
    // second engine catches up when it is about to run: first the mappings
    // that are out of date go, in runs, then the missing ones come.
    unsigned dirty_first = Pages, dirty_last = 0;
    void Dirty(unsigned address, unsigned size)
    {
        const unsigned first = address >> PageBits, last = (unsigned)(((unsigned long long)address + size - 1) >> PageBits);
        if (first < dirty_first) dirty_first = first;
        if (last > dirty_last) dirty_last = last;
    }
    unsigned Sync()
    {
        if (!engines[1] || dirty_first > dirty_last)
            return CpuError::Ok;
        const unsigned first = dirty_first, last = dirty_last;
        for (unsigned long long p = first; p <= last;)
        {
            if (!(pages[p] & PageDrop)) { ++p; continue; }
            const unsigned long long begin = p++;
            while (p <= last && (pages[p] & PageDrop)) ++p;
            const unsigned error = engines[1]->Unmap((unsigned)(begin << PageBits), (unsigned)((p - begin) << PageBits));
            if (error) return error;
            for (unsigned long long q = begin; q < p; ++q) pages[q] &= ~(PageMirrored | PageDrop);
        }
        dirty_first = Pages;
        dirty_last = 0;
        return MirrorPages(first, last);
    }
    bool Second()
    {
        if (engines[1])
            return true;
        Cpu *made = nullptr;
        if (CreateCpu(CpuBackend::Unicorn, made, "16-bit-code-in-win32-process") != CpuError::Ok || !made)
            return false;
        engines[1] = made;
        // From here on a host write may have to drop translations of the
        // second engine (Forget), so every write goes through Write.
        page_write = nullptr;
        if ((device_access && made->MapDevice(device_base, device_size, device_access, device_context)) ||
            MirrorPages(0, Pages - 1) != CpuError::Ok || (gate.hook && made->SetGateHook(gate.begin, gate.end, gate.hook, gate.context)) ||
            (fault.hook && made->SetFaultHook(fault.hook, fault.context)) ||
            (interrupt.hook && made->SetInterruptHook(interrupt.hook, interrupt.context)) ||
            ((ports.input || ports.output) && made->SetPortHooks(ports.input, ports.output, ports.context)))
        {
            delete made;
            engines[1] = nullptr;
            for (unsigned p = 0; p < Pages; ++p) pages[p] &= ~(PageMirrored | PageDrop);
            return false;
        }
        if (budget_hook.set)
            SecondBudgetHook();
        return true;
    }
    // The whole processor state from the active engine to the other one.
    void Carry(int to)
    {
        Cpu &from = Active(), &into = *engines[to];
        unsigned value = 0;
        // A register write cannot lower the privilege level: Unicorn checks a
        // new CS against the level of the code it ran last, and that was the
        // 16-bit code at level three. The first state an engine took at level
        // zero comes back first, and the registers go on top of it.
        if (to == 1 && ring0[1])
            into.Restore(ring0[1]);
        from.Get(Reg::Cr0, value);
        into.Set(Reg::Cr0, value);
        for (int i = 0; i < 2; ++i)
        {
            DescriptorTable table;
            if (from.GetWide(i ? Reg::Ldtr : Reg::Gdtr, &table) == CpuError::Ok)
                into.SetWide(i ? Reg::Ldtr : Reg::Gdtr, &table);
        }
        // The code segment first, so the privilege level the others are
        // checked against is the one of the state that arrives.
        static const int segments[] = {Reg::Cs, Reg::Ss, Reg::Ds, Reg::Es, Reg::Fs, Reg::Gs};
        for (int reg : segments)
        {
            from.Get(reg, value);
            into.Set(reg, value);
        }
        for (int reg = Reg::Eax; reg <= Reg::Edi; ++reg)
        {
            from.Get(reg, value);
            into.Set(reg, value);
        }
        static const int words[] = {Reg::Eflags, Reg::Fpcw, Reg::Fpsw, Reg::Fptag, Reg::Fip, Reg::Fcs,
                                    Reg::Fdp,    Reg::Fds,  Reg::Fop,  Reg::Mxcsr};
        for (int reg : words)
        {
            from.Get(reg, value);
            into.Set(reg, value);
        }
        for (int reg = Reg::Fp0; reg <= Reg::Xmm7; ++reg)
        {
            unsigned char wide[16] = {};
            if (from.GetWide(reg, wide) == CpuError::Ok)
                into.SetWide(reg, wide);
        }
        from.Get(Reg::Eip, value);
        into.Set(Reg::Eip, value);
        unsigned cs = 0;
        into.Get(Reg::Cs, cs);
        if (to == 1 && !ring0[1] && !(cs & 3) && into.NewContext(ring0[1]) == CpuError::Ok)
            into.Save(ring0[1]);
        Activate(to);
    }
    void Activate(int to)
    {
        active = to;
        gpr = engines[to]->gpr;
        eip = engines[to]->eip;
        if (budget_set)
            SetBudget(budget);
    }
    void SetBudget(int blocks)
    {
        if (active == 1 && !second_counted)
            second_left = blocks;
        else
            Active().Budget(blocks);
    }
    // The hook of the second engine: the caller's own when both engines
    // agree, the counting one when only the main engine counts.
    void SecondBudgetHook()
    {
        bool counted = budget_hook.counted;
        engines[1]->SetBudgetHook(budget_hook.hook, budget_hook.context, counted);
        second_counted = counted == budget_hook.counted;
        if (!second_counted && budget_hook.hook)
        {
            bool per_block = false;
            engines[1]->SetBudgetHook(SecondBlock, this, per_block);
        }
    }
    // The engine for the instruction at CS:EIP: an LDT code segment, or the
    // far return (CBh, CAh) whose CS on the stack is one, is 16-bit code.
    int EngineFor()
    {
        unsigned cs = 0;
        Active().Get(Reg::Cs, cs);
        if (cs & 4)
            return 1;
        unsigned at = 0, esp = 0;
        unsigned char opcode = 0;
        Active().Get(Reg::Eip, at);
        if (Main().Read(at, &opcode, 1) != CpuError::Ok || (opcode != 0xcb && opcode != 0xca))
            return 0;
        Active().Get(Reg::Esp, esp);
        unsigned target = 0;
        if (Main().Read(esp + 4, &target, 4) != CpuError::Ok)
            return 0;
        return (target & 4) ? 1 : 0;
    }

    // ------------------------------------------------------------ registers
    unsigned Get(int reg, unsigned &value) override { return Active().Get(reg, value); }
    unsigned Set(int reg, unsigned value) override { return Active().Set(reg, value); }
    unsigned GetWide(int reg, void *value) override { return Active().GetWide(reg, value); }
    unsigned SetWide(int reg, const void *value) override { return Active().SetWide(reg, value); }

    // ------------------------------------------------------------- contexts
    unsigned NewContext(CpuContext *&context) override
    {
        auto *made = new DualContext;
        const unsigned error = Main().NewContext(made->part[0]);
        if (error)
        {
            delete made;
            context = nullptr;
            return error;
        }
        context = reinterpret_cast<CpuContext *>(made);
        return CpuError::Ok;
    }
    void FreeContext(CpuContext *context) override
    {
        auto *dual = reinterpret_cast<DualContext *>(context);
        if (!dual)
            return;
        for (int i = 0; i < 2; ++i)
            if (dual->part[i])
                engines[i]->FreeContext(dual->part[i]);
        delete dual;
    }
    unsigned Save(CpuContext *context) override
    {
        auto *dual = reinterpret_cast<DualContext *>(context);
        if (!dual->part[active])
        {
            const unsigned error = Active().NewContext(dual->part[active]);
            if (error)
                return error;
        }
        dual->engine = active;
        if (active && Main().Save(dual->part[0])) return CpuError::Resource;
        return Active().Save(dual->part[active]);
    }
    unsigned Restore(const CpuContext *context) override
    {
        const auto *dual = reinterpret_cast<const DualContext *>(context);
        const int engine = dual->engine;
        if (!dual->part[engine])
            return CpuError::Argument;
        if (engine && (!Second() || Sync())) return CpuError::Resource;
        if (engine && Main().Restore(dual->part[0])) return CpuError::Resource;
        const unsigned error = engines[engine]->Restore(dual->part[engine]);
        if (!error && engine != active)
            Activate(engine);
        return error;
    }
    unsigned ContextGet(const CpuContext *context, int reg, unsigned &value) override
    {
        const auto *dual = reinterpret_cast<const DualContext *>(context);
        value = 0;
        if (!dual->part[dual->engine])
            return CpuError::Argument;
        return engines[dual->engine]->ContextGet(dual->part[dual->engine], reg, value);
    }

    // --------------------------------------------------------------- memory
    // The state the main engine has now. What the second engine still holds
    // for the page is out of date with any change: other rights, and after an
    // Unmap possibly other host bytes.
    void Mark(unsigned address, unsigned size, unsigned char value)
    {
        for (unsigned long long p = address >> PageBits; p <= ((unsigned long long)address + size - 1) >> PageBits; ++p)
            pages[p] = value | ((pages[p] & PageMirrored) ? (PageMirrored | PageDrop) : 0);
        if (engines[1])
            Dirty(address, size);
    }
    unsigned Map(unsigned address, unsigned size, unsigned prot) override
    {
        const unsigned error = Main().Map(address, size, prot);
        if (error || !size)
            return error;
        Mark(address, size, PageMapped | (prot & 7));
        return CpuError::Ok;
    }
    DeviceAccess device_access = nullptr;
    void *device_context = nullptr;
    unsigned device_base = 0, device_size = 0;
    unsigned MapDevice(unsigned address, unsigned size, DeviceAccess access, void *context) override
    {
        if (device_access) return CpuError::Argument;
        const unsigned error = Main().MapDevice(address, size, access, context);
        if (error) return error;
        device_base = address; device_size = size; device_access = access; device_context = context;
        return engines[1] ? engines[1]->MapDevice(address, size, access, context) : CpuError::Ok;
    }
    unsigned MapHost(unsigned address, unsigned size, unsigned prot, void *host) override
    {
        if (const unsigned error = RevokeImageData(address, size)) return error;
        const unsigned error = Main().MapHost(address, size, prot, host);
        if (error || !size)
            return error;
        Mark(address, size, PageMapped | (prot & 7));
        return CpuError::Ok;
    }
    unsigned Unmap(unsigned address, unsigned size) override
    {
        if (const unsigned error = RevokeImageData(address, size)) return error;
        const unsigned error = Main().Unmap(address, size);
        if (error || !size)
            return error;
        Mark(address, size, 0);
        return CpuError::Ok;
    }
    unsigned Protect(unsigned address, unsigned size, unsigned prot) override
    {
        if (const unsigned error = RevokeImageData(address, size)) return error;
        const unsigned error = Main().Protect(address, size, prot);
        if (error || !size)
            return error;
        Mark(address, size, PageMapped | (prot & 7));
        return CpuError::Ok;
    }
    unsigned Read(unsigned address, void *out, unsigned size) override { return Main().Read(address, out, size); }
    // Unicorn drops the translations of a page when its own stores reach
    // it; bytes the host writes through the main engine bypass that, so an
    // executable page the host writes loses its translations there page by
    // page (the loader reusing the memory of a freed module, a procedure
    // thunk in a reused slot). Pages without the execute right hold no
    // translation: a fetch from them stops with FetchProtected first.
    void Forget(unsigned address, unsigned size)
    {
        if (!engines[1] || !size)
            return;
        for (unsigned long long p = address >> PageBits; p <= ((unsigned long long)address + size - 1) >> PageBits; ++p)
            if ((pages[p] & (PageMapped | Prot::Exec)) == (PageMapped | Prot::Exec))
                // Unicorn turns the start into a RAM address once and counts
                // on from there, which is wrong across its mapped blocks;
                // one page at a time is right.
                engines[1]->InvalidateCode((unsigned)(p << PageBits), 1u << PageBits);
    }
    unsigned Write(unsigned address, const void *data, unsigned size) override
    {
        const unsigned error = Main().Write(address, data, size);
        if (!error)
            Forget(address, size);
        return error;
    }
    unsigned InvalidateCode(unsigned address, unsigned size) override
    {
        if (engines[1] && size)
            for (unsigned long long p = address >> PageBits; p <= ((unsigned long long)address + size - 1) >> PageBits;
                 ++p)
                engines[1]->InvalidateCode((unsigned)(p << PageBits), 1u << PageBits);
        return Main().InvalidateCode(address, size);
    }
    unsigned FlushCode() override
    {
        if (engines[1])
            engines[1]->FlushCode();
        return Main().FlushCode();
    }
    void *HostPointer(unsigned address, unsigned size) override { return Main().HostPointer(address, size); }

    // ------------------------------------------------------------ execution
    unsigned Run(unsigned begin, unsigned until, unsigned instructions) override
    {
        const int want = EngineFor();
        if (want != active)
        {
            if (want == 1 && !Second())
                return CpuError::Resource;
            Carry(want);
        }
        if (active == 1)
        {
            const unsigned error = Sync();
            if (error)
                return error;
        }
        return Active().Run(begin, until, instructions);
    }
    void Stop() override { Active().Stop(); }
    // The engine frame of the first engine (Cpu::DetachFrame). FEXCore keeps a
    // Run open across the yield of a gate served inside it; without passing
    // these on, the next fiber found the frame of the one that yielded as its
    // own, and a Run nested in it pointed back into that fiber's stack after
    // the thread had ended and its stack was freed. The second
    // engine keeps nothing across a yield.
    void *DetachFrame() override { return Main().DetachFrame(); }
    void AttachFrame(void *frame) override { Main().AttachFrame(frame); }
    void RequestStop() override
    {
        for (Cpu *engine : engines)
            if (engine)
                engine->RequestStop();
    }
    void Budget(int blocks) override
    {
        budget = blocks;
        budget_set = true;
        SetBudget(blocks);
    }

    // ---------------------------------------------------------------- hooks
    unsigned SetGateHook(unsigned begin, unsigned end, GateHook hook, void *context) override
    {
        gate = {begin, end, hook, context};
        if (engines[1])
            engines[1]->SetGateHook(begin, end, hook, context);
        return Main().SetGateHook(begin, end, hook, context);
    }
    unsigned SetFaultHook(FaultHook hook, void *context) override
    {
        fault = {hook, context};
        if (engines[1])
            engines[1]->SetFaultHook(hook, context);
        return Main().SetFaultHook(hook, context);
    }
    unsigned SetInterruptHook(InterruptHook hook, void *context) override
    {
        interrupt = {hook, context};
        if (engines[1])
            engines[1]->SetInterruptHook(hook, context);
        return Main().SetInterruptHook(hook, context);
    }
    unsigned SetPortHooks(PortInputHook input, PortOutputHook output, void *context) override
    {
        ports = {input, output, context};
        if (engines[1])
            engines[1]->SetPortHooks(input, output, context);
        return Main().SetPortHooks(input, output, context);
    }
    // Both engines have to agree on the kind of budget the caller sees; the
    // second one is asked when it is made and counted for when it cannot.
    unsigned SetBudgetHook(BudgetHook hook, void *context, bool &counted) override
    {
        const unsigned error = Main().SetBudgetHook(hook, context, counted);
        budget_hook = {hook, context, true, counted};
        if (engines[1])
            SecondBudgetHook();
        return error;
    }
    unsigned WatchWrites(WriteHook hook, void *context) override
    {
        if (const unsigned error = ImageDataWriteWatch(hook != nullptr)) return error;
        return Main().WatchWrites(hook, context);
    }
    void UnwatchWrites() override { ImageDataWriteWatch(false); Main().UnwatchWrites(); }
    void RearmWrites(unsigned address, unsigned size) override { Main().RearmWrites(address, size); }
    unsigned AddTrace(TraceKind kind, unsigned begin, unsigned end, TraceHook hook, void *context) override
    {
        return Main().AddTrace(kind, begin, end, hook, context);
    }
};
} // namespace

Cpu *CreateDualCpu(Cpu *main)
{
    auto *made = new DualCpu(main);
    if (!made->pages)
    {
        made->engines[0] = nullptr;
        delete made;
        return main;
    }
    return made;
}
} // namespace X86
