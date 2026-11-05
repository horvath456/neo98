// SPDX-License-Identifier: GPL-2.0-or-later
// FEXCore behind X86::Cpu, the second x86 engine of the macOS host. Native
// host code. The FEX patches it relies on are in fex/patches/.
//
// Every processor owns a context of FEXCore with one guest thread and a
// reserved 4 GiB window of host address space; the patched FEX adds the window
// base to every guest address (fex/patches/0002-neo98-guest.patch). Guest
// rights are kept per 4 KiB page as Win98 knows them; the host protects 16 KiB
// pages, so a host page carries the rights all of its four guest pages share
// and the signal handler carries out the accesses the guest page allows but
// the host page refuses (see the memory section).
#include "../src/x86_cpu.h"
#include "../src/x86_state.h"

#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/Context.h>
#include <FEXCore/Core/CoreState.h>
#include <FEXCore/Core/HostFeatures.h>
#include <FEXCore/Core/SignalDelegator.h>
#include <FEXCore/Core/X86Enums.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include <FEXCore/HLE/SyscallHandler.h>
#include <FEXCore/Utils/AllocatorHooks.h>
#include <FEXCore/Utils/LogManager.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <cstdio>
#include <cstring>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <vector>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/ucontext.h>
#include <unistd.h>

namespace X86
{
namespace
{
constexpr uint64_t WindowBytes = 1ull << 32, GuardBytes = 1ull << 16;
// The layout the patched FEXCore expects behind GuestMemBase (see
// NEO98CheckedHost in fex/patches/0002-neo98-guest.patch): the window, at
// +4 GiB and +8 GiB one byte per guest page for "reads" and "writes may use
// the direct view", at +16 GiB the direct view.
constexpr uint64_t ReadTableOffset = 1ull << 32, WriteTableOffset = 1ull << 33, DirectOffset = 1ull << 34;
constexpr uint64_t ReservationBytes = DirectOffset + WindowBytes + 2 * GuardBytes;
constexpr unsigned GuestPageBits = 12, GuestPages = 1u << (32 - GuestPageBits);
unsigned host_page_bits = 14;

// Per guest page.
enum : uint8_t
{
    PageRead = 1,
    PageWrite = 2,
    PageExec = 4,
    PageMapped = 8,
    PageCode = 16,
    PageShared = 32,
};

struct FexContext
{
    FEXCore::Core::CPUState state;
    // The x87 registers of state hold doubles (reduced precision).
    bool x87_reduced = false;
    unsigned fsw_shadow = 0, fip = 0, fdp = 0, fcs = 0, fds = 0, fop = 0, cr0 = 0, dr[6] = {};
    DescriptorTable gdtr, ldtr;
};

class FexCpu;

// The pauses of the machine in ticks of the host's cycle counter: what the
// translated RDTSC takes off CNTVCT_EL0 (CPUState::NEO98CycleOffset, patch
// 0006). Asked before every entry into translated code, so the
// conversion is kept per thread until the sum changes.
uint64_t CycleOffset()
{
    const uint64_t microseconds = CounterPause();
    if (!microseconds)
        return 0;
    static thread_local uint64_t converted = 0, ticks = 0;
    if (converted != microseconds)
    {
        uint64_t frequency = 0;
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
        ticks = (uint64_t)((unsigned __int128)microseconds * frequency / 1000000);
        converted = microseconds;
    }
    return ticks;
}

// Live processors, for the signal handler to find the window a fault lies in.
constexpr unsigned MaxCpus = 64;
std::atomic<FexCpu *> live[MaxCpus];
thread_local FexCpu *running = nullptr;
struct sigaction previous_segv, previous_bus;

FEXCore::HostFeatures host_features;

bool Feature(const char *name)
{
    int value = 0;
    size_t size = sizeof value;
    return sysctlbyname(name, &value, &size, nullptr, 0) == 0 && value;
}

void LogMessage(LogMan::DebugLevels level, const char *message)
{
    if (level <= LogMan::DebugLevels::ERROR)
        fprintf(stderr, "FEX: %s\n", message);
}
void LogAssert(const char *message) { fprintf(stderr, "FEX assert: %s\n", message); }

void SignalHandler(int signal, siginfo_t *info, void *context);
bool Debug()
{
    static const bool on = getenv("NEO98_FEX_DEBUG") != nullptr;
    return on;
}

void GlobalInit()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    host_page_bits = __builtin_ctz((unsigned)getpagesize());
    LogMan::Throw::InstallHandler(LogAssert);
    LogMan::Msg::InstallHandler(LogMessage);
    FEXCore::Config::Initialize();
    FEXCore::Config::Load();
    using namespace FEXCore::Config;
    Set(CONFIG_IS64BIT_MODE, "0");
    // Whole functions per translation; the patched JIT counts the block
    // budget at every guest block head, loops inside a multiblock included.
    // NEO98_FEX_MULTIBLOCK=0 goes back to one block per translation.
    const char *multiblock = getenv("NEO98_FEX_MULTIBLOCK");
    Set(CONFIG_MULTIBLOCK, multiblock && !strcmp(multiblock, "0") ? "0" : "1");
    // NEO98_FEX_MAXINST=n caps the guest instructions of one block (also for
    // diagnosing translation faults). Default 300: in a large
    // function every return address starts a new multiblock over the rest of
    // the function; uncapped, programs with big functions (ds-recomp's
    // generated code) translated the same code again and again (10-20 times
    // slower). NEO98_FEX_SMC=none|mtrack|full picks how FEXCore checks code
    // for modification.
    const char *limit = getenv("NEO98_FEX_MAXINST");
    Set(CONFIG_MAXINST, limit ? limit : "300");
    if (const char *smc = getenv("NEO98_FEX_SMC"))
        Set(CONFIG_SMCCHECKS, !strcmp(smc, "full") ? "2" : !strcmp(smc, "none") ? "0" : "1");
    // Every guest thread runs as a fiber on one host thread: no memory model
    // to emulate between guest threads.
    Set(CONFIG_TSOENABLED, "0");
    Set(CONFIG_VECTORTSOENABLED, "0");
    Set(CONFIG_MEMCPYSETTSOENABLED, "0");
    // x87 with 64-bit doubles: a Win32 thread starts at 53-bit precision
    // (control word 027Fh), and a process that asks for
    // 64-bit precision is switched to 80-bit values (FexCpu::X87Full).
    // NEO98_FEX_X87=80 starts every process with 80-bit values.
    const char *x87 = getenv("NEO98_FEX_X87");
    Set(CONFIG_X87REDUCEDPRECISION, x87 && !strcmp(x87, "80") ? "0" : "1");
    // The dispatcher looks a guest address up in the page table of the lookup
    // cache itself instead of calling CompileBlock for it. Every gate call
    // returns through the dispatcher, so this is on the API path.
    Set(CONFIG_DISABLEL2CACHE, "0");

    auto &f = host_features;
    f.DCacheLineLog2 = 6;
    f.SupportsCRC = Feature("hw.optional.armv8_crc32");
    f.SupportsAtomics = Feature("hw.optional.arm.FEAT_LSE");
    f.SupportsRCPC = Feature("hw.optional.arm.FEAT_LRCPC");
    f.SupportsTSOImm9 = Feature("hw.optional.arm.FEAT_LRCPC2");
    f.SupportsAES = Feature("hw.optional.arm.FEAT_AES");
    f.SupportsSHA = Feature("hw.optional.arm.FEAT_SHA256");
    f.SupportsPMULL_128Bit = Feature("hw.optional.arm.FEAT_PMULL");
    f.SupportsFlagM = Feature("hw.optional.arm.FEAT_FlagM");
    f.SupportsFlagM2 = Feature("hw.optional.arm.FEAT_FlagM2");
    f.SupportsFRINTTS = Feature("hw.optional.arm.FEAT_FRINTTS");
    f.SupportsDotProd = Feature("hw.optional.arm.FEAT_DotProd");
    f.SupportsFCMA = Feature("hw.optional.arm.FEAT_FCMA");
    f.SupportsCSSC = Feature("hw.optional.arm.FEAT_CSSC");
    f.SupportsRPRES = Feature("hw.optional.arm.FEAT_RPRES");
    f.SupportsAFP = Feature("hw.optional.arm.FEAT_AFP");
    f.SupportsI8MM = Feature("hw.optional.arm.FEAT_I8MM");
    f.SupportsPreserveAllABI = 1;
    f.HostType = FEXCore::HostFeatures::HostTypeEnum::Wow64;
    f.CPUMIDRs.push_back(0x610F0000);

    struct sigaction action = {};
    action.sa_sigaction = SignalHandler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, &previous_segv);
    sigaction(SIGBUS, &action, &previous_bus);
}

void Chain(int signal, siginfo_t *info, void *context)
{
    const struct sigaction &old = signal == SIGBUS ? previous_bus : previous_segv;
    if (old.sa_flags & SA_SIGINFO)
    {
        if (old.sa_sigaction)
            old.sa_sigaction(signal, info, context);
        return;
    }
    if (old.sa_handler == SIG_IGN)
        return;
    if (old.sa_handler && old.sa_handler != SIG_DFL)
    {
        old.sa_handler(signal);
        return;
    }
    // Default action: let the fault happen again without a handler.
    struct sigaction reset = {};
    reset.sa_handler = SIG_DFL;
    sigaction(signal, &reset, nullptr);
}

class FexCpu final : public Cpu
{
  public:
    struct Syscalls final : FEXCore::HLE::SyscallHandler
    {
        FexCpu *cpu = nullptr;
        void HandleSyscall(FEXCore::Core::CpuStateFrame *) override
        {
            // A Run inside the handler (a callback of a gate served here) has
            // syscalls of its own.
            const bool outer = cpu->in_syscall;
            cpu->in_syscall = true;
            // The block that made the call stays on this host stack until the
            // handler returns, also while its fiber sleeps in a gate
            // (Cpu::DetachFrame) and other threads run on this FEXCore
            // thread. A code buffer that fills up meanwhile must outlive it:
            // FEXCore keeps old buffers while the frame counts signal
            // handlers with code on the stack (CPUBackend::
            // RegisterForSignalHandler), so the handler counts as one.
            auto &blocks = cpu->thread->CurrentFrame->SignalHandlerRefCounter;
            ++blocks;
            cpu->OnSyscall();
            --blocks;
            cpu->in_syscall = outer;
        }
        FEXCore::HLE::ExecutableRangeInfo QueryGuestExecutableRange(FEXCore::Core::InternalThreadState *,
                                                                    uint64_t address) override
        {
            return cpu->ExecutableRange(address);
        }
        std::optional<FEXCore::ExecutableFileSectionInfo> LookupExecutableFileSection(FEXCore::Core::InternalThreadState *,
                                                                                      uint64_t) override
        {
            return std::nullopt;
        }
        void MarkGuestExecutableRange(FEXCore::Core::InternalThreadState *, uint64_t start, uint64_t length) override
        {
            cpu->MarkCode(start, length);
        }
        // A block that found its bytes changed drops itself.
        void InvalidateGuestCodeRange(FEXCore::Core::InternalThreadState *, uint64_t start, uint64_t length) override
        {
            cpu->InvalidateRange(start, length);
        }
    };
    struct Signals final : FEXCore::SignalDelegator
    {
    };
    // One Run on the stack; hooks inside it may start another.
    struct RunFrame
    {
        RunFrame *outer = nullptr;
        bool stopped = false;
        unsigned error = CpuError::Ok;
    };

    fextl::unique_ptr<FEXCore::Context::Context> ctx;
    FEXCore::Core::InternalThreadState *thread = nullptr;
    Syscalls syscalls;
    Signals signals;
    uint8_t *window = nullptr, *reservation = nullptr;
    uint8_t *direct = nullptr, *read_ok = nullptr, *write_ok = nullptr;
    // Diagnostics per code page, and fault counts per guest instruction.
    // The 4-GiB count table is demand-zero; only faulting code touches it.
    uint8_t *checked_code = nullptr, *access_emulations = nullptr;
    uint8_t *pages = nullptr, *write_reported = nullptr; // GuestPages entries
    uint8_t *host_backed = nullptr; // one per host page
    uint8_t *checked = nullptr, *drops = nullptr, *host_prot = nullptr, *has_side = nullptr, *emul = nullptr; // per host page
    // Private guest pages inside a shared host page: their own 4 KiB.
    std::unordered_map<unsigned, uint8_t *> side;
    std::unordered_map<uint64_t, uint64_t> emulated_pages;
    // Guest pages a fetch failed on. FEXCore keeps the block it made for
    // such an address (it raises the fault); when the page becomes usable
    // (Map, Protect: the port grants the execute right on demand) that block
    // has to go.
    std::unordered_map<unsigned, bool> failed_fetch;
    void ForgetFailedFetches(unsigned address, unsigned size)
    {
        if (failed_fetch.empty())
            return;
        for (uint64_t p = address >> GuestPageBits; p <= ((uint64_t)address + size - 1) >> GuestPageBits; ++p)
        {
            auto found = failed_fetch.find((unsigned)p);
            if (found == failed_fetch.end())
                continue;
            failed_fetch.erase(found);
            InvalidateRange(p << GuestPageBits, 1u << GuestPageBits);
        }
    }
    RunFrame *active = nullptr;
    uint8_t *callret = nullptr;
    uint64_t callret_bytes = 0, callret_default = 0;
    // Host page -> the host bytes MapHost shares into it.
    std::unordered_map<uint64_t, uint64_t> shared_source;
    std::atomic<bool> stop_requested {false};
    unsigned fault_error = CpuError::Ok;
    unsigned long long smc_restarts = 0, smc_drops = 0, side_pages = 0, budget_exits = 0, emulations = 0, debug_faults = 0, checked_code_pages = 0;
    unsigned long long memory_faults = 0, write_faults = 0;
    unsigned fsw_shadow = 0, fip = 0, fdp = 0, fcs = 0, fds = 0, fop = 0, cr0 = 0, dr[6] = {};
    DescriptorTable gdtr, ldtr;
    unsigned until = 0;
    // Inside the syscall handler of FEXCore, where translated code may not be
    // dropped; a switch to 80-bit x87 waits for the next exit then.
    bool in_syscall = false, x87_switch_pending = false;
    unsigned long long x87_switches = 0;
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
        BudgetHook hook = nullptr;
        void *context = nullptr;
    } budget;
    struct
    {
        WriteHook hook = nullptr;
        void *context = nullptr;
    } writes;
    struct
    {
        PortInputHook input = nullptr;
        PortOutputHook output = nullptr;
        void *context = nullptr;
    } ports;

    FEXCore::Core::CPUState &State() { return thread->CurrentFrame->State; }
    uint64_t HostPageBytes() const { return 1ull << host_page_bits; }

    ~FexCpu() override
    {
        if (Debug())
        {
            unsigned checked_pages = 0;
            for (uint64_t h = 0; h < (WindowBytes >> host_page_bits); ++h)
                if (checked && checked[h])
                {
                    ++checked_pages;
                    fprintf(stderr, "FEXCPU checked host page %08llx\n", (unsigned long long)(h << host_page_bits));
                }
            fprintf(stderr, "FEXCPU smc_restarts=%llu smc_drops=%llu side_pages=%llu checked=%u budget_exits=%llu emulations=%llu checked_code=%llu x87_switches=%llu faults=%llu write_faults=%llu\n",
                    smc_restarts, smc_drops, side_pages, checked_pages, budget_exits, emulations, checked_code_pages, x87_switches,
                    memory_faults, write_faults);
            std::vector<std::pair<uint64_t, uint64_t>> hot(emulated_pages.begin(), emulated_pages.end());
            std::sort(hot.begin(), hot.end(), [](auto &a, auto &b) { return a.second > b.second; });
            for (size_t i = 0; i < hot.size() && i < 8; ++i)
                fprintf(stderr, "FEXCPU emulated host page %08llx: %llu\n",
                        (unsigned long long)(hot[i].first << host_page_bits), (unsigned long long)hot[i].second);
        }
        for (auto &slot : live)
        {
            FexCpu *self = this;
            slot.compare_exchange_strong(self, nullptr);
        }
        if (thread)
            ctx->DestroyThread(thread);
        ctx.reset();
        if (reservation)
        {
            munmap(reservation, ReservationBytes);
        }
        if (checked_code && checked_code != MAP_FAILED)
            munmap(checked_code, GuestPages);
        if (access_emulations && access_emulations != MAP_FAILED)
            munmap(access_emulations, WindowBytes);
        for (auto &entry : side)
            free(entry.second);
        if (pages)
            munmap(pages, GuestPages);
        if (has_side && has_side != MAP_FAILED)
            munmap(has_side, WindowBytes >> host_page_bits);
        if (emul && emul != MAP_FAILED)
            munmap(emul, WindowBytes >> host_page_bits);
        if (host_backed)
            munmap(host_backed, WindowBytes >> host_page_bits);
        if (callret)
            munmap(callret, callret_bytes);
        if (checked)
            munmap(checked, WindowBytes >> host_page_bits);
        if (drops)
            munmap(drops, WindowBytes >> host_page_bits);
        if (host_prot && host_prot != MAP_FAILED)
            munmap(host_prot, WindowBytes >> host_page_bits);
        if (write_reported && write_reported != MAP_FAILED)
            munmap(write_reported, GuestPages);
    }
    CpuBackend Backend() const override { return CpuBackend::Fex; }
    unsigned ImageDataPageSize() const override { return host_page_bits == 14 ? 16384 : 0; }
    // Saved states (src/x86_state.h). The guest memory and the registers go
    // through this interface; translated code, the side tables of the
    // window and the 80-bit form of the x87 registers are made anew.
    DeviceAccess device_access = nullptr;
    void *device_context = nullptr;
    unsigned device_base = 0, device_size = 0;
    bool DevicePage(unsigned page) const
    { return device_access && page >= (device_base >> GuestPageBits) && page < ((device_base + device_size) >> GuestPageBits); }
    unsigned MapDevice(unsigned address, unsigned size, DeviceAccess access, void *context) override
    {
        if (device_access || !access || !size || (address | size) & (HostPageBytes() - 1)) return CpuError::Argument;
        const unsigned error = Map(address, size, Prot::Read | Prot::Write);
        if (error) return error;
        device_base = address; device_size = size; device_access = access; device_context = context;
        for (uint64_t h = address >> host_page_bits; h < (uint64_t(address) + size) >> host_page_bits; ++h) ApplyHost(h);
        return CpuError::Ok;
    }
    const char *StateRefusal() const override { return nullptr; }
    bool Regions(RegionVisitor each, void *context) override
    {
        const uint64_t page_bytes = 1ull << GuestPageBits, host_mask = HostPageBytes() - 1;
        uint64_t begin = 0, expected_host = 0;
        unsigned run_prot = 0;
        void *run_host = nullptr;
        bool open = false;
        auto close = [&](uint64_t end) {
            if (open)
                each(context, (unsigned)(begin << GuestPageBits), (unsigned)((end - begin) << GuestPageBits), run_prot, run_host);
            open = false;
        };
        for (uint64_t p = 0; p < GuestPages; ++p)
        {
            if (!(pages[p] & PageMapped) || DevicePage(unsigned(p)))
            {
                close(p);
                continue;
            }
            const unsigned prot = pages[p] & 7;
            uint64_t host = 0;
            if (pages[p] & PageShared)
            {
                auto found = shared_source.find((p << GuestPageBits) >> host_page_bits);
                if (found == shared_source.end())
                    return false;
                host = found->second + ((p << GuestPageBits) & host_mask);
            }
            if (open && (prot != run_prot || (host != 0) != (run_host != nullptr) || (host && host != expected_host)))
                close(p);
            if (!open)
            {
                open = true;
                begin = p;
                run_prot = prot;
                run_host = reinterpret_cast<void *>(host);
            }
            expected_host = host ? host + page_bytes : 0;
        }
        close(GuestPages);
        return true;
    }

    unsigned Open()
    {
        GlobalInit();
        reservation = static_cast<uint8_t *>(mmap(nullptr, ReservationBytes, PROT_NONE,
                                                  MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0));
        if (reservation == MAP_FAILED)
        {
            reservation = nullptr;
            return CpuError::NoMemory;
        }
        window = reservation + GuardBytes;
        direct = window + DirectOffset;
        read_ok = static_cast<uint8_t *>(mmap(window + ReadTableOffset, GuestPages, PROT_READ | PROT_WRITE,
                                              MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0));
        write_ok = static_cast<uint8_t *>(mmap(window + WriteTableOffset, GuestPages, PROT_READ | PROT_WRITE,
                                               MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0));
        checked_code = static_cast<uint8_t *>(mmap(nullptr, GuestPages, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
        access_emulations = static_cast<uint8_t *>(mmap(nullptr, WindowBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
        if (read_ok == MAP_FAILED || write_ok == MAP_FAILED || checked_code == MAP_FAILED || access_emulations == MAP_FAILED)
            return CpuError::NoMemory;
        pages = static_cast<uint8_t *>(mmap(nullptr, GuestPages, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
        host_backed = static_cast<uint8_t *>(
            mmap(nullptr, WindowBytes >> host_page_bits, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
        checked = static_cast<uint8_t *>(
            mmap(nullptr, WindowBytes >> host_page_bits, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
        drops = static_cast<uint8_t *>(
            mmap(nullptr, WindowBytes >> host_page_bits, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
        host_prot = static_cast<uint8_t *>(
            mmap(nullptr, WindowBytes >> host_page_bits, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
        write_reported = static_cast<uint8_t *>(mmap(nullptr, GuestPages, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
        has_side = static_cast<uint8_t *>(
            mmap(nullptr, WindowBytes >> host_page_bits, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
        emul = static_cast<uint8_t *>(
            mmap(nullptr, WindowBytes >> host_page_bits, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
        if (host_prot == MAP_FAILED || write_reported == MAP_FAILED || has_side == MAP_FAILED || emul == MAP_FAILED)
            return CpuError::NoMemory;
        if (pages == MAP_FAILED || host_backed == MAP_FAILED || checked == MAP_FAILED || drops == MAP_FAILED)
            return CpuError::NoMemory;
        syscalls.cpu = this;
        pthread_jit_write_protect_np(0);
        ctx = FEXCore::Context::Context::CreateNewContext(host_features);
        ctx->SetSignalDelegator(&signals);
        ctx->SetSyscallHandler(&syscalls);
        ctx->EnableExitOnHLT();
        const bool initialized = ctx->InitCore();
        if (initialized)
            thread = ctx->CreateThread();
        pthread_jit_write_protect_np(1);
        if (!initialized || !thread)
            return CpuError::Resource;
        auto &state = State();
        state.GuestMemBase = reinterpret_cast<uint64_t>(window);
        // FEXCore reads guest code through the direct view.
        ctx->NEO98SetCodeView(direct - window);
        // Page by page, because a private page in a shared host page keeps
        // its bytes in a side page.
        ctx->NEO98SetCheckedLookup(
            [](void *opaque, uint64_t rip) -> bool {
                return static_cast<FexCpu *>(opaque)->access_emulations[rip & 0xffffffffu] >= 4;
            },
            this);
        ctx->NEO98SetCodePageLookup(
            [](void *opaque, uint64_t page) -> const uint8_t * {
                return static_cast<FexCpu *>(opaque)->GuestBytes((unsigned)page);
            },
            this);
        // The call-return stack of FEXCore, with a guard page on both sides;
        // the embedder owns it (as FEX's Windows frontend does).
        callret_bytes = FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE + 2 * HostPageBytes();
        callret = static_cast<uint8_t *>(mmap(nullptr, callret_bytes, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0));
        if (callret == MAP_FAILED)
            return CpuError::NoMemory;
        mprotect(callret + HostPageBytes(), FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE, PROT_READ | PROT_WRITE);
        thread->CallRetStackBase = callret + HostPageBytes();
        callret_default = reinterpret_cast<uint64_t>(thread->CallRetStackBase) +
                          FEXCore::Core::InternalThreadState::CALLRET_STACK_SIZE / 4;
        state.callret_sp = callret_default;
        // Until the port loads its own GDT, a flat 32-bit code segment in
        // FEXCore's private table at selector 8, as the processor starts.
        auto &code = state.private_gdt[1];
        FEXCore::Core::CPUState::SetGDTBase(&code, 0);
        FEXCore::Core::CPUState::SetGDTLimit(&code, 0xfffff);
        code.Type = 0xb;
        code.S = 1;
        code.P = 1;
        code.D = 1;
        code.G = 1;
        state.segment_arrays[0] = state.private_gdt;
        state.segment_arrays[1] = state.private_gdt;
        state.cs_idx = 8;
        gpr = reinterpret_cast<unsigned long long *>(state.gregs);
        eip = reinterpret_cast<const unsigned long long *>(&state.rip);
        // The tables of the checked blocks say exactly what the host needs
        // to know to touch a page in place (ApplyHost keeps them).
        memory = direct;
        page_read = read_ok;
        page_write = write_ok;
        for (auto &slot : live)
        {
            FexCpu *empty = nullptr;
            if (slot.compare_exchange_strong(empty, this))
                return CpuError::Ok;
        }
        return CpuError::Resource;
    }

    // ------------------------------------------------------------ memory
    // Two views of the same guest memory. The window is what translated code
    // uses (GuestMem); a host page there carries only the rights all of
    // its four guest pages have, so every access the guest may not make
    // faults. The direct view maps the same host pages always read/write; the
    // host itself (Read, Write, HostPointer, the decoder of FEXCore) and the
    // emulation of a faulting access use it. An access the host page refuses
    // but the guest page allows is carried out by the signal handler
    // (Emulate), so the guest sees its rights per 4 KiB page as on Win98.
    // A host page takes its bytes either from a private anonymous page or
    // from host bytes MapHost shares. Private guest pages in a shared host
    // page keep their bytes in a side page of 4 KiB; such a host page is
    // closed in the window, and all guest accesses to it are emulated.

    uint8_t *DirectHost(uint64_t host_page) { return direct + (host_page << host_page_bits); }
    uint8_t *WindowHost(uint64_t host_page) { return window + (host_page << host_page_bits); }
    unsigned PerHost() const { return 1u << (host_page_bits - GuestPageBits); }
    // The host bytes of one guest page, for the host and the emulation.
    uint8_t *GuestBytes(unsigned page)
    {
        if (has_side[page >> (host_page_bits - GuestPageBits)])
        {
            auto found = side.find(page);
            if (found != side.end())
                return found->second;
        }
        return direct + ((uint64_t)page << GuestPageBits);
    }
    bool Alias(uint8_t *target, uint8_t *source, uint64_t bytes)
    {
        mach_vm_address_t address = reinterpret_cast<mach_vm_address_t>(target);
        vm_prot_t current = 0, maximum = 0;
        return mach_vm_remap(mach_task_self(), &address, bytes, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, mach_task_self(),
                             reinterpret_cast<mach_vm_address_t>(source), FALSE, &current, &maximum,
                             VM_INHERIT_NONE) == KERN_SUCCESS;
    }
    // Fresh zero bytes for a run of host pages, in both views.
    bool BackPrivate(uint64_t first, uint64_t count)
    {
        const uint64_t bytes = count << host_page_bits;
        if (mmap(WindowHost(first), bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) == MAP_FAILED)
            return false;
        if (!Alias(DirectHost(first), WindowHost(first), bytes))
            return false;
        mprotect(DirectHost(first), bytes, PROT_READ | PROT_WRITE);
        for (uint64_t h = first; h < first + count; ++h)
        {
            host_backed[h] = 1;
            host_prot[h] = PROT_READ | PROT_WRITE;
            shared_source.erase(h);
        }
        return true;
    }
    bool BackShared(uint64_t h, uint64_t source)
    {
        if (!Alias(WindowHost(h), reinterpret_cast<uint8_t *>(source), HostPageBytes()) ||
            !Alias(DirectHost(h), reinterpret_cast<uint8_t *>(source), HostPageBytes()))
            return false;
        mprotect(DirectHost(h), HostPageBytes(), PROT_READ | PROT_WRITE);
        host_backed[h] = 1;
        host_prot[h] = PROT_READ | PROT_WRITE;
        shared_source[h] = source;
        return true;
    }
    void Release(uint64_t h)
    {
        mmap(WindowHost(h), HostPageBytes(), PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED | MAP_NORESERVE, -1, 0);
        mmap(DirectHost(h), HostPageBytes(), PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED | MAP_NORESERVE, -1, 0);
        host_backed[h] = 0;
        host_prot[h] = 0;
        shared_source.erase(h);
    }
    void FreeSide(unsigned page)
    {
        auto found = side.find(page);
        if (found == side.end())
            return;
        free(found->second);
        side.erase(found);
    }
    void UpdateSide(uint64_t h)
    {
        bool any = false;
        for (unsigned i = 0; i < PerHost() && !any; ++i)
            any = side.count((unsigned)(h * PerHost() + i)) != 0;
        has_side[h] = any;
    }

    unsigned HostProt(uint64_t host_page)
    {
        if (has_side[host_page] || DevicePage(unsigned(host_page * PerHost())))
            return PROT_NONE;
        unsigned prot = PROT_READ | PROT_WRITE;
        for (unsigned i = 0; i < PerHost(); ++i)
        {
            const unsigned page = (unsigned)(host_page * PerHost() + i);
            const uint8_t p = pages[page];
            if (!(p & PageMapped) || !(p & (PageRead | PageWrite)))
                return PROT_NONE;
            if (!(p & PageWrite))
                prot &= ~PROT_WRITE;
            // Code the guest may overwrite: a write has to drop its
            // translation first, unless its blocks check their own bytes.
            if ((p & PageCode) && !checked[host_page])
                prot &= ~PROT_WRITE;
            if (writes.hook && !write_reported[page])
                prot &= ~PROT_WRITE;
        }
        return prot;
    }
    bool AnyMapped(uint64_t host_page)
    {
        for (unsigned i = 0; i < PerHost(); ++i)
            if (pages[host_page * PerHost() + i] & PageMapped)
                return true;
        return false;
    }
    bool AnyShared(uint64_t host_page)
    {
        for (unsigned i = 0; i < PerHost(); ++i)
            if (pages[host_page * PerHost() + i] & PageShared)
                return true;
        return false;
    }
    void ApplyHost(uint64_t host_page)
    {
        if (!AnyMapped(host_page))
        {
            if (host_backed[host_page])
                Release(host_page);
            for (unsigned i = 0; i < PerHost(); ++i)
                read_ok[host_page * PerHost() + i] = write_ok[host_page * PerHost() + i] = 0;
            return;
        }
        for (unsigned i = 0; i < PerHost(); ++i)
        {
            const unsigned page = (unsigned)(host_page * PerHost() + i);
            const uint8_t p = pages[page];
            const bool own = !DevicePage(page) && (!has_side[host_page] || !side.count(page));
            read_ok[page] = own && Allowed(p, false);
            write_ok[page] = own && Allowed(p, true) && !((p & PageCode) && !checked[host_page]) &&
                             !(writes.hook && !write_reported[page]);
        }
        const uint8_t prot = (uint8_t)HostProt(host_page);
        if (prot != host_prot[host_page])
        {
            host_prot[host_page] = prot;
            mprotect(WindowHost(host_page), HostPageBytes(), prot);
        }
    }
    bool AllMapped(unsigned address, unsigned size)
    {
        if (!size)
            return true;
        if ((uint64_t)address + size > WindowBytes)
            return false;
        for (uint64_t p = address >> GuestPageBits; p <= ((uint64_t)address + size - 1) >> GuestPageBits; ++p)
            if (!(pages[p] & PageMapped))
                return false;
        return true;
    }
    bool AnyMappedGuest(unsigned address, unsigned size)
    {
        for (uint64_t p = address >> GuestPageBits; p <= ((uint64_t)address + size - 1) >> GuestPageBits; ++p)
            if (pages[p] & PageMapped)
                return true;
        return false;
    }
    static bool Aligned(unsigned address, unsigned size)
    {
        return size && !(address & 4095) && !(size & 4095) && (uint64_t)address + size <= WindowBytes;
    }

    unsigned Map(unsigned address, unsigned size, unsigned prot) override
    {
        if (!Aligned(address, size))
            return CpuError::Argument;
        if (AnyMappedGuest(address, size))
            return CpuError::Map;
        const uint64_t first = (uint64_t)address >> host_page_bits, last = ((uint64_t)address + size - 1) >> host_page_bits;
        for (uint64_t h = first; h <= last;)
        {
            if (!host_backed[h])
            {
                uint64_t end = h + 1;
                while (end <= last && !host_backed[end])
                    ++end;
                if (!BackPrivate(h, end - h))
                    return CpuError::NoMemory;
                h = end;
                continue;
            }
            // A page shares its host page with pages mapped before; it starts
            // out zero like any fresh mapping. In a shared host page it gets
            // a side page of its own.
            const uint64_t begin = std::max<uint64_t>(address, h << host_page_bits);
            const uint64_t end = std::min<uint64_t>((uint64_t)address + size, (h + 1) << host_page_bits);
            if (shared_source.count(h))
            {
                for (uint64_t p = begin >> GuestPageBits; p < end >> GuestPageBits; ++p)
                {
                    void *bytes = nullptr;
                    if (posix_memalign(&bytes, 1u << GuestPageBits, 1u << GuestPageBits))
                        return CpuError::NoMemory;
                    memset(bytes, 0, 1u << GuestPageBits);
                    side[(unsigned)p] = static_cast<uint8_t *>(bytes);
                    ++side_pages;
                }
                has_side[h] = 1;
                if (checked[h])
                {
                    DropHostPage(h);
                    checked[h] = 0;
                }
            }
            else
                memset(direct + begin, 0, end - begin);
            ++h;
        }
        for (uint64_t p = address >> GuestPageBits; p <= ((uint64_t)address + size - 1) >> GuestPageBits; ++p)
        {
            pages[p] = PageMapped | (prot & 7);
            write_reported[p] = 0;
        }
        for (uint64_t h = first; h <= last; ++h)
            ApplyHost(h);
        ForgetFailedFetches(address, size);
        return CpuError::Ok;
    }
    // Host bytes the caller shares between processes. The host maps whole
    // 16 KiB pages, so the guest address and the host address must lie at the
    // same offset in their page. Other guest pages of such a host page that
    // are private move into side pages; the shared bytes of the neighbours
    // are never touched.
    unsigned MapHost(unsigned address, unsigned size, unsigned prot, void *host) override
    {
        const uint64_t mask = HostPageBytes() - 1, source = reinterpret_cast<uint64_t>(host);
        if (!Aligned(address, size) || ((source - address) & mask))
        {
            if (Debug())
                fprintf(stderr, "FEXCPU MapHost refused address=%08x size=%x host=%p\n", address, size, host);
            return CpuError::Map;
        }
        if (AnyMappedGuest(address, size))
            return CpuError::Map;
        if (const unsigned error = RevokeImageData(address, size)) return error;
        const uint64_t first = (uint64_t)address >> host_page_bits, last = ((uint64_t)address + size - 1) >> host_page_bits;
        for (uint64_t h = first; h <= last; ++h)
        {
            const uint64_t page_source = (source - address + (h << host_page_bits));
            auto known = shared_source.find(h);
            if (host_backed[h] && known != shared_source.end())
            {
                if (known->second == page_source)
                    continue;
                if (Debug())
                    fprintf(stderr, "FEXCPU MapHost conflict address=%08x size=%x host=%p\n", address, size, host);
                return CpuError::Map;
            }
        }
        for (uint64_t h = first; h <= last; ++h)
        {
            const uint64_t page_source = (source - address + (h << host_page_bits));
            if (host_backed[h] && shared_source.count(h))
                continue;
            if (host_backed[h])
            {
                // Private guest pages already live in this host page (a
                // shared section inside an image): their bytes go to side
                // pages before the shared bytes replace the host page.
                for (unsigned i = 0; i < PerHost(); ++i)
                {
                    const unsigned page = (unsigned)(h * PerHost() + i);
                    if (!(pages[page] & PageMapped))
                        continue;
                    void *bytes = nullptr;
                    if (posix_memalign(&bytes, 1u << GuestPageBits, 1u << GuestPageBits))
                        return CpuError::NoMemory;
                    memcpy(bytes, direct + ((uint64_t)page << GuestPageBits), 1u << GuestPageBits);
                    side[page] = static_cast<uint8_t *>(bytes);
                    ++side_pages;
                    has_side[h] = 1;
                }
                // Blocks that check their bytes read them through the direct
                // view, which no longer holds a side page's bytes.
                if (checked[h])
                {
                    DropHostPage(h);
                    checked[h] = 0;
                }
            }
            if (!BackShared(h, page_source))
                return CpuError::Map;
        }
        for (uint64_t p = address >> GuestPageBits; p <= ((uint64_t)address + size - 1) >> GuestPageBits; ++p)
        {
            pages[p] = PageMapped | PageShared | (prot & 7);
            write_reported[p] = 0;
        }
        for (uint64_t h = first; h <= last; ++h)
        {
            host_prot[h] = 0xff;
            ApplyHost(h);
        }
        ForgetFailedFetches(address, size);
        return CpuError::Ok;
    }
    unsigned Unmap(unsigned address, unsigned size) override
    {
        if (!Aligned(address, size))
            return CpuError::Argument;
        if (!AllMapped(address, size))
            return CpuError::NoMemory;
        if (const unsigned error = RevokeImageData(address, size)) return error;
        DropCode(address, size);
        for (uint64_t p = address >> GuestPageBits; p <= ((uint64_t)address + size - 1) >> GuestPageBits; ++p)
        {
            pages[p] = 0;
            FreeSide((unsigned)p);
        }
        for (uint64_t h = (uint64_t)address >> host_page_bits; h <= ((uint64_t)address + size - 1) >> host_page_bits; ++h)
        {
            UpdateSide(h);
            if (!Privatize(h))
                return CpuError::NoMemory;
            ApplyHost(h);
        }
        return CpuError::Ok;
    }
    // A shared host page whose last shared guest page went, with private
    // guest pages left: they get a private host page again, with the bytes of
    // their side pages.
    bool Privatize(uint64_t h)
    {
        if (!host_backed[h] || !shared_source.count(h) || !AnyMapped(h) || AnyShared(h))
            return true;
        std::vector<uint8_t> copy(HostPageBytes());
        for (unsigned i = 0; i < PerHost(); ++i)
        {
            const unsigned page = (unsigned)(h * PerHost() + i);
            if (pages[page] & PageMapped)
                memcpy(copy.data() + (i << GuestPageBits), GuestBytes(page), 1u << GuestPageBits);
        }
        if (!BackPrivate(h, 1))
            return false;
        memcpy(DirectHost(h), copy.data(), HostPageBytes());
        for (unsigned i = 0; i < PerHost(); ++i)
            FreeSide((unsigned)(h * PerHost() + i));
        has_side[h] = 0;
        host_prot[h] = 0xff;
        return true;
    }
    unsigned Protect(unsigned address, unsigned size, unsigned prot) override
    {
        if (!Aligned(address, size))
            return CpuError::Argument;
        if (!AllMapped(address, size))
            return CpuError::NoMemory;
        if (const unsigned error = RevokeImageData(address, size)) return error;
        for (uint64_t p = address >> GuestPageBits; p <= ((uint64_t)address + size - 1) >> GuestPageBits; ++p)
            pages[p] = (pages[p] & ~7) | (prot & 7);
        for (uint64_t h = (uint64_t)address >> host_page_bits; h <= ((uint64_t)address + size - 1) >> host_page_bits; ++h)
            ApplyHost(h);
        ForgetFailedFetches(address, size);
        return CpuError::Ok;
    }
    // Copies between host memory and guest pages through the direct view,
    // page by page; the host ignores the guest's rights.
    template <typename F> void EachPage(unsigned address, unsigned size, F &&f)
    {
        unsigned done = 0;
        while (done < size)
        {
            const unsigned at = address + done, offset = at & ((1u << GuestPageBits) - 1);
            const unsigned n = std::min(size - done, (1u << GuestPageBits) - offset);
            f(GuestBytes(at >> GuestPageBits) + offset, done, n);
            done += n;
        }
    }
    unsigned Read(unsigned address, void *out, unsigned size) override
    {
        // Split accesses crossing either edge of the device aperture before
        // taking the RAM fast path. Its PROT_NONE bytes are never host RAM.
        if (device_access && address < uint64_t(device_base) + device_size &&
            uint64_t(address) + size > device_base)
        {
            const unsigned edge = address < device_base ? device_base : device_base + device_size;
            if (uint64_t(address) + size > edge)
            {
                const unsigned n = edge - address;
                if (const unsigned error = Read(address, out, n)) return error;
                return Read(edge, static_cast<unsigned char *>(out) + n, size - n);
            }
        }
        if (device_access && address >= device_base && uint64_t(address) + size <= uint64_t(device_base) + device_size)
        {
            device_access(device_context, address, static_cast<unsigned char *>(out), size, false);
            return CpuError::Ok;
        }

        // Most reads are an API argument or a word of the thread block: one
        // page, no walk.
        if (size && (address & ((1u << GuestPageBits) - 1)) + size <= (1u << GuestPageBits))
        {
            if (!(pages[address >> GuestPageBits] & PageMapped))
                return CpuError::ReadUnmapped;
            memcpy(out, GuestBytes(address >> GuestPageBits) + (address & ((1u << GuestPageBits) - 1)), size);
            return CpuError::Ok;
        }
        if (!AllMapped(address, size))
            return CpuError::ReadUnmapped;
        EachPage(address, size, [&](uint8_t *bytes, unsigned done, unsigned n) { memcpy((uint8_t *)out + done, bytes, n); });
        return CpuError::Ok;
    }
    unsigned Write(unsigned address, const void *data, unsigned size) override
    {
        // Split accesses crossing either edge of the device aperture before
        // taking the RAM fast path. Its PROT_NONE bytes are never host RAM.
        if (device_access && address < uint64_t(device_base) + device_size &&
            uint64_t(address) + size > device_base)
        {
            const unsigned edge = address < device_base ? device_base : device_base + device_size;
            if (uint64_t(address) + size > edge)
            {
                const unsigned n = edge - address;
                if (const unsigned error = Write(address, data, n)) return error;
                return Write(edge, static_cast<const unsigned char *>(data) + n, size - n);
            }
        }
        if (device_access && address >= device_base && uint64_t(address) + size <= uint64_t(device_base) + device_size)
        {
            device_access(device_context, address, static_cast<unsigned char *>(const_cast<void *>(data)), size, true);
            return CpuError::Ok;
        }

        if (size && (address & ((1u << GuestPageBits) - 1)) + size <= (1u << GuestPageBits))
        {
            const unsigned page = address >> GuestPageBits;
            if (!(pages[page] & PageMapped))
                return CpuError::WriteUnmapped;
            if (pages[page] & PageCode)
                DropPage(page);
            memcpy(GuestBytes(page) + (address & ((1u << GuestPageBits) - 1)), data, size);
            return CpuError::Ok;
        }
        if (!AllMapped(address, size))
            return CpuError::WriteUnmapped;
        if (!size)
            return CpuError::Ok;
        DropCode(address, size);
        EachPage(address, size,
                 [&](uint8_t *bytes, unsigned done, unsigned n) { memcpy(bytes, (const uint8_t *)data + done, n); });
        return CpuError::Ok;
    }
    // The translations of the guest pages of a range that hold code go, and
    // the pages lose their code mark.
    void DropCode(uint64_t address, uint64_t size)
    {
        if (!size) return;
        const uint64_t end = ((address + size - 1) >> GuestPageBits) + 1;
        for (uint64_t page = address >> GuestPageBits; page < end;)
        {
            if (!(pages[page] & PageCode)) { ++page; continue; }
            const uint64_t begin = page++;
            while (page < end && (pages[page] & PageCode)) ++page;
            // FEX scans its cached blocks for each invalidation. One scan
            // per contiguous code range avoids quadratic work on DLL/heap
            // release while keeping unrelated translations alive.
            InvalidateRange(begin << GuestPageBits, (page - begin) << GuestPageBits);
            for (uint64_t p = begin; p < page; ++p) pages[p] &= ~PageCode;
            const unsigned shift = host_page_bits - GuestPageBits;
            for (uint64_t h = begin >> shift; h <= (page - 1) >> shift; ++h) ApplyHost(h);
        }
    }
    void DropPage(unsigned page)
    {
        InvalidateRange((uint64_t)page << GuestPageBits, 1u << GuestPageBits);
        pages[page] &= ~PageCode;
        ApplyHost(page >> (host_page_bits - GuestPageBits));
    }
    // Every code page of a host page loses its translations and its mark.
    void DropHostPage(uint64_t h)
    {
        DropCode(h << host_page_bits, HostPageBytes());
    }
    // FEXCore wants its invalidation lock held uniquely around both steps,
    // the way its own frontends do it.
    void InvalidateRange(uint64_t start, uint64_t length)
    {
        ++stat_invalidations;
        stat_invalidated += length;
        // Unlinking blocks rewrites translated code, which needs the JIT
        // pages writable for this thread.
        FEXCore::Allocator::NEO98JitWriteScope writable;
        std::unique_lock lock(ctx->GetCodeInvalidationMutex());
        ctx->InvalidateCodeBuffersCodeRange(start, length);
        ctx->InvalidateThreadCachedCodeRange(thread, start, length);
    }
    unsigned InvalidateCode(unsigned address, unsigned size) override
    {
        if (size)
            InvalidateRange(address, size);
        return CpuError::Ok;
    }
    // FlushInstructionCache: every page with translations loses them, the
    // way a write into it would (DropPage), so code the host wrote through
    // HostPointer is read anew. The code buffer stays: clearing the cache
    // here cost a new, ever larger buffer per call (ATL thunks of every
    // window) and freed the one the calling block still ran in.
    unsigned FlushCode() override
    {
        DropCode(0, WindowBytes);
        return CpuError::Ok;
    }
    // All translated code goes: the x87 form of this process changed. The
    // blocks on the stack keep their code buffer (HandleSyscall).
    void ClearCode()
    {
        FEXCore::Allocator::NEO98JitWriteScope writable;
        std::unique_lock lock(ctx->GetCodeInvalidationMutex());
        ctx->ClearCodeCache(thread);
    }
    // The host bytes of a range, where one run of the direct view holds them.
    void *HostPointer(unsigned address, unsigned size) override
    {
        if (device_access && uint64_t(address) + size > device_base && address < uint64_t(device_base) + device_size)
            return nullptr;
        if (!AllMapped(address, size))
            return nullptr;
        for (uint64_t h = (uint64_t)address >> host_page_bits; size && h <= ((uint64_t)address + size - 1) >> host_page_bits; ++h)
            if (has_side[h])
                return nullptr;
        return direct + address;
    }

    // Called by FEXCore when it translates code from a guest page: the page
    // gets the code mark, and its host page loses the write right, so a write
    // into it is caught and drops the translation (OnFault).
    void MarkCode(uint64_t start, uint64_t length)
    {
        if (!length || start >= WindowBytes)
            return;
        // NEO98_FEX_DUMP_CODE=lo-hi (hex): the guest bytes of every
        // translation that starts in the range, for finding a block the JIT
        // gets wrong.
        static const char *dump = getenv("NEO98_FEX_DUMP_CODE");
        if (dump)
        {
            static unsigned lo = 0, hi = 0;
            static bool parsed = false;
            if (!parsed) { parsed = true; sscanf(dump, "%x-%x", &lo, &hi); }
            if (start >= lo && start < hi && AllMapped((unsigned)start, (unsigned)length))
            {
                fprintf(stderr, "FEXCPU code %08x %llu ", (unsigned)start, (unsigned long long)length);
                for (uint64_t i = 0; i < length; ++i)
                    fprintf(stderr, "%02x", GuestBytes((unsigned)(start + i) >> GuestPageBits)[(start + i) & 0xfff]);
                fprintf(stderr, "\n");
            }
        }
        for (uint64_t p = start >> GuestPageBits; p <= std::min(start + length - 1, WindowBytes - 1) >> GuestPageBits; ++p)
            if (pages[p] & PageMapped)
                pages[p] |= PageCode;
        for (uint64_t h = start >> host_page_bits; h <= std::min(start + length - 1, WindowBytes - 1) >> host_page_bits; ++h)
            if (host_backed[h])
                ApplyHost(h);
    }

    // The readable run of pages from address on. "Writable" tells the patched
    // FEXCore to compile blocks that check their own bytes: code on a host page
    // the guest keeps writing into (see OnFault).
    FEXCore::HLE::ExecutableRangeInfo ExecutableRange(uint64_t address)
    {
        const auto readable = [&](uint64_t page) {
            return page < GuestPages && (pages[page] & PageMapped) && (pages[page] & PageRead);
        };
        const unsigned shift = host_page_bits - GuestPageBits;
        const uint64_t first = address >> GuestPageBits;
        if (address >= WindowBytes || !readable(first))
            return {0, 0, false};
        // Only a guest page the guest may write needs the check; a read-only
        // page next to data in the same host page cannot change behind our
        // back (the host's own writes go through Write and DropCode).
        const auto needs_check = [&](uint64_t page) {
            return checked[page >> shift] && !has_side[page >> shift] && (pages[page] & PageWrite);
        };
        const bool check = needs_check(first);
        uint64_t last = first;
        while (last - first < 255 && readable(last + 1) && needs_check(last + 1) == check)
            ++last;
        return {first << GuestPageBits, (last - first + 1) << GuestPageBits, check};
    }

    // ------------------------------------------------------------ registers

    unsigned Descriptor(unsigned selector, uint64_t &base)
    {
        const DescriptorTable &table = selector & 4 ? ldtr : gdtr;
        const unsigned offset = selector & ~7u;
        uint64_t raw = 0;
        if (!(selector & ~3u))
        {
            base = 0;
            return CpuError::Ok;
        }
        if (offset + 7 > table.limit || Read(table.base + offset, &raw, 8))
            return CpuError::Exception;
        base = ((raw >> 16) & 0xffffff) | ((raw >> 32) & 0xff000000);
        return CpuError::Ok;
    }
    unsigned SetSegment(int reg, unsigned selector)
    {
        auto &s = State();
        uint64_t base = 0;
        const unsigned error = Descriptor(selector, base);
        if (error)
            return error;
        switch (reg)
        {
        case Reg::Es: s.es_idx = selector; s.es_cached = (uint32_t)base; break;
        case Reg::Cs: s.cs_idx = selector; s.cs_cached = (uint32_t)base; break;
        case Reg::Ss: s.ss_idx = selector; s.ss_cached = (uint32_t)base; break;
        case Reg::Ds: s.ds_idx = selector; s.ds_cached = (uint32_t)base; break;
        case Reg::Fs: s.fs_idx = selector; s.fs_cached = base; break;
        case Reg::Gs: s.gs_idx = selector; s.gs_cached = base; break;
        }
        return CpuError::Ok;
    }
    void PointSegmentArrays()
    {
        auto &s = State();
        if (!gdtr.limit)
            return;
        s.segment_arrays[FEXCore::Core::CPUState::SEGMENT_ARRAY_INDEX_GDT] =
            reinterpret_cast<FEXCore::Core::CPUState::gdt_segment *>(direct + gdtr.base);
        s.segment_arrays[FEXCore::Core::CPUState::SEGMENT_ARRAY_INDEX_LDT] =
            reinterpret_cast<FEXCore::Core::CPUState::gdt_segment *>(direct + ldtr.base);
    }
    // ------------------------------------------------------------ x87 precision

    // A double (reduced precision keeps one in the low 64 bits of mm[i]) as
    // the 80-bit value it stands for; exact.
    static void DoubleToExtended(uint64_t bits, uint64_t &mantissa, uint64_t &top)
    {
        const uint64_t sign = bits >> 63;
        const unsigned exponent = unsigned(bits >> 52) & 0x7ff;
        uint64_t fraction = bits & ((1ull << 52) - 1);
        if (exponent == 0x7ff)
        {
            mantissa = (1ull << 63) | (fraction << 11);
            top = (sign << 15) | 0x7fff;
            return;
        }
        if (!exponent)
        {
            if (!fraction)
            {
                mantissa = 0;
                top = sign << 15;
                return;
            }
            // A denormal double, fraction * 2^-1074, is a normal extended
            // value: mantissa * 2^(E - 16383 - 63).
            const int shift = __builtin_clzll(fraction);
            mantissa = fraction << shift;
            top = (sign << 15) | unsigned(16383 + 63 - 1074 - shift);
            return;
        }
        mantissa = (1ull << 63) | (fraction << 11);
        top = (sign << 15) | unsigned(exponent - 1023 + 16383);
    }
    // An 80-bit value as the double reduced precision keeps, rounded to
    // nearest even like FST m64; overflow gives infinity, underflow zero or a
    // denormal.
    static uint64_t ExtendedToDouble(uint64_t mantissa, uint64_t top)
    {
        const uint64_t sign = (top >> 15) & 1;
        const int exponent = int(top & 0x7fff);
        if (exponent == 0x7fff)
        {
            const uint64_t fraction = (mantissa << 1) >> 12;
            return (sign << 63) | (0x7ffull << 52) | (fraction ? (fraction | (1ull << 51)) : 0);
        }
        if (!mantissa)
            return sign << 63;
        int e = exponent - 16383 + 1023;
        // Normalise pseudo-denormals and unnormals.
        const int lead = __builtin_clzll(mantissa);
        mantissa <<= lead;
        e -= lead;
        if (!exponent)
            e += 1;
        uint64_t kept;
        unsigned shift = 11;
        if (e <= 0)
        {
            shift = unsigned(11 + 1 - e);
            e = 0;
            if (shift > 64)
                return sign << 63;
        }
        kept = shift >= 64 ? 0 : mantissa >> shift;
        const uint64_t rest = shift >= 64 ? mantissa : mantissa & ((1ull << shift) - 1);
        const uint64_t half = 1ull << (shift - 1);
        if (rest > half || (rest == half && (kept & 1)))
            ++kept;
        if (e && kept >> 53)
        {
            kept >>= 1;
            ++e;
        }
        if (!e && kept >> 52)
            e = 1;
        if (e >= 0x7ff)
            return (sign << 63) | (0x7ffull << 52);
        return (sign << 63) | (uint64_t(e) << 52) | (kept & ((1ull << 52) - 1));
    }
    bool X87Reduced() const { return ctx->NEO98X87Reduced(); }
    // The x86 rounding control (0 nearest, 1 down, 2 up, 3 toward zero) in
    // FPCR.RMode, bit-reversed as FEXCore's SetRoundingMode does it.
    static void SetHostRounding(unsigned control)
    {
        const uint64_t mode = ((control & 1) << 1) | ((control >> 1) & 1);
        uint64_t fpcr;
        __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
        fpcr = (fpcr & ~(3ull << 22)) | (mode << 22);
        __asm__ volatile("msr fpcr, %0" ::"r"(fpcr));
    }
    static void RegistersToExtended(FEXCore::Core::CPUState &state)
    {
        for (auto &reg : state.mm)
        {
            uint64_t mantissa = 0, top = 0;
            DoubleToExtended(reg[0], mantissa, top);
            reg[0] = mantissa;
            reg[1] = top;
        }
    }
    // The guest asked for 64-bit precision: from now on this process uses
    // 80-bit x87 values. Translated code goes, the register file changes its
    // form; saved contexts change theirs when they are restored.
    void X87Full()
    {
        x87_switch_pending = false;
        if (!X87Reduced())
            return;
        ctx->NEO98SetX87Reduced(false);
        RegistersToExtended(State());
        ClearCode();
        ++x87_switches;
        if (Debug())
            fprintf(stderr, "FEXCPU x87 switched to 80-bit at %08x fcw=%04x\n", (unsigned)State().rip, State().FCW);
    }
    unsigned Fsw()
    {
        using namespace FEXCore::X86State;
        const auto &f = State().flags;
        return (fsw_shadow & ~0x47ffu & 0xffff) | (fsw_shadow & 0xbe) | (f[X87FLAG_IE_LOC] & 1) |
               (f[X87FLAG_C0_LOC] & 1) << 8 | (f[X87FLAG_C1_LOC] & 1) << 9 | (f[X87FLAG_C2_LOC] & 1) << 10 |
               (f[X87FLAG_TOP_LOC] & 7) << 11 | (f[X87FLAG_C3_LOC] & 1) << 14;
    }
    void SetFsw(unsigned value)
    {
        using namespace FEXCore::X86State;
        auto &f = State().flags;
        f[X87FLAG_IE_LOC] = value & 1;
        f[X87FLAG_C0_LOC] = (value >> 8) & 1;
        f[X87FLAG_C1_LOC] = (value >> 9) & 1;
        f[X87FLAG_C2_LOC] = (value >> 10) & 1;
        f[X87FLAG_TOP_LOC] = (value >> 11) & 7;
        f[X87FLAG_C3_LOC] = (value >> 14) & 1;
        fsw_shadow = value & 0xbe;
    }
    unsigned Ftw()
    {
        auto &s = State();
        unsigned tags = 0;
        for (unsigned i = 0; i < 8; ++i)
        {
            unsigned tag = 3;
            if (s.AbridgedFTW & (1u << i))
            {
                uint64_t mantissa = s.mm[i][0], top = s.mm[i][1];
                if (X87Reduced())
                    DoubleToExtended(s.mm[i][0], mantissa, top);
                const unsigned exponent = (unsigned)top & 0x7fff;
                if (!exponent && !mantissa)
                    tag = 1;
                else if (exponent == 0x7fff || !exponent || !(mantissa >> 63))
                    tag = 2;
                else
                    tag = 0;
            }
            tags |= tag << (i * 2);
        }
        return tags;
    }
    unsigned Get(int reg, unsigned &value) override
    {
        auto &s = State();
        value = 0;
        if (reg >= Reg::Eax && reg <= Reg::Edi)
        {
            value = (unsigned)s.gregs[reg];
            return CpuError::Ok;
        }
        switch (reg)
        {
        case Reg::Eip: value = (unsigned)s.rip; break;
        case Reg::Eflags: value = ctx->ReconstructCompactedEFLAGS(thread, false, nullptr, 0); break;
        case Reg::Es: value = s.es_idx; break;
        case Reg::Cs: value = s.cs_idx; break;
        case Reg::Ss: value = s.ss_idx; break;
        case Reg::Ds: value = s.ds_idx; break;
        case Reg::Fs: value = s.fs_idx; break;
        case Reg::Gs: value = s.gs_idx; break;
        case Reg::Cr0: value = cr0; break;
        case Reg::Fpcw: value = s.FCW; break;
        case Reg::Fpsw: value = Fsw(); break;
        case Reg::Fptag: value = Ftw(); break;
        case Reg::Fip: value = fip; break;
        case Reg::Fcs: value = fcs; break;
        case Reg::Fdp: value = fdp; break;
        case Reg::Fds: value = fds; break;
        case Reg::Fop: value = fop; break;
        case Reg::Mxcsr: value = s.mxcsr; break;
        case Reg::Dr0: case Reg::Dr1: case Reg::Dr2: case Reg::Dr3: value = dr[reg - Reg::Dr0]; break;
        case Reg::Dr6: value = dr[4]; break;
        case Reg::Dr7: value = dr[5]; break;
        default: return CpuError::Argument;
        }
        return CpuError::Ok;
    }
    unsigned Set(int reg, unsigned value) override
    {
        auto &s = State();
        if (reg >= Reg::Eax && reg <= Reg::Edi)
        {
            s.gregs[reg] = value;
            return CpuError::Ok;
        }
        switch (reg)
        {
        case Reg::Eip: s.rip = value; break;
        case Reg::Eflags: ctx->SetFlagsFromCompactedEFLAGS(thread, value); break;
        case Reg::Es: case Reg::Cs: case Reg::Ss: case Reg::Ds: case Reg::Fs: case Reg::Gs:
            return SetSegment(reg, value & 0xffff);
        case Reg::Cr0: cr0 = value; break;
        case Reg::Fpcw:
            s.FCW = (uint16_t)value;
            // With doubles the x87 rounding control lives in the host FPCR,
            // which FEXCore only sets on a guest FLDCW; do what that does.
            if (X87Reduced())
                SetHostRounding((value >> 10) & 3);
            // The host sets the control word for the guest (_controlfp and
            // _control87 of the CRT, a restored CONTEXT): 64-bit precision
            // switches the process like a guest FLDCW does.
            if ((value & 0x300) == 0x300 && X87Reduced())
            {
                if (in_syscall)
                {
                    x87_switch_pending = true;
                    s.NEO98Exit = 3;
                }
                else
                    X87Full();
            }
            break;
        case Reg::Fpsw: SetFsw(value); break;
        case Reg::Fptag:
        {
            uint8_t abridged = 0;
            for (unsigned i = 0; i < 8; ++i)
                if (((value >> (i * 2)) & 3) != 3)
                    abridged |= 1u << i;
            s.AbridgedFTW = abridged;
            break;
        }
        case Reg::Fip: fip = value; break;
        case Reg::Fcs: fcs = value; break;
        case Reg::Fdp: fdp = value; break;
        case Reg::Fds: fds = value; break;
        case Reg::Fop: fop = value; break;
        case Reg::Mxcsr: s.mxcsr = value; break;
        case Reg::Dr0: case Reg::Dr1: case Reg::Dr2: case Reg::Dr3: dr[reg - Reg::Dr0] = value; break;
        case Reg::Dr6: dr[4] = value; break;
        case Reg::Dr7: dr[5] = value; break;
        default: return CpuError::Argument;
        }
        return CpuError::Ok;
    }
    unsigned GetWide(int reg, void *value) override
    {
        auto &s = State();
        if (reg >= Reg::Fp0 && reg <= Reg::Fp7)
        {
            uint64_t raw[2] = {s.mm[reg - Reg::Fp0][0], s.mm[reg - Reg::Fp0][1] & 0xffff};
            if (X87Reduced())
                DoubleToExtended(s.mm[reg - Reg::Fp0][0], raw[0], raw[1]);
            memcpy(value, raw, sizeof raw);
            return CpuError::Ok;
        }
        if (reg >= Reg::Xmm0 && reg <= Reg::Xmm7)
        {
            memcpy(value, s.xmm.sse.data[reg - Reg::Xmm0], 16);
            return CpuError::Ok;
        }
        if (reg == Reg::Gdtr || reg == Reg::Ldtr)
        {
            *static_cast<DescriptorTable *>(value) = reg == Reg::Gdtr ? gdtr : ldtr;
            return CpuError::Ok;
        }
        return CpuError::Argument;
    }
    unsigned SetWide(int reg, const void *value) override
    {
        auto &s = State();
        if (reg >= Reg::Fp0 && reg <= Reg::Fp7)
        {
            uint64_t raw[2] = {};
            memcpy(raw, value, 10);
            if (X87Reduced())
            {
                s.mm[reg - Reg::Fp0][0] = ExtendedToDouble(raw[0], raw[1] & 0xffff);
                s.mm[reg - Reg::Fp0][1] = 0;
                return CpuError::Ok;
            }
            s.mm[reg - Reg::Fp0][0] = raw[0];
            s.mm[reg - Reg::Fp0][1] = raw[1] & 0xffff;
            return CpuError::Ok;
        }
        if (reg >= Reg::Xmm0 && reg <= Reg::Xmm7)
        {
            memcpy(s.xmm.sse.data[reg - Reg::Xmm0], value, 16);
            return CpuError::Ok;
        }
        if (reg == Reg::Gdtr || reg == Reg::Ldtr)
        {
            (reg == Reg::Gdtr ? gdtr : ldtr) = *static_cast<const DescriptorTable *>(value);
            PointSegmentArrays();
            return CpuError::Ok;
        }
        return CpuError::Argument;
    }

    // ------------------------------------------------------------ contexts

    static void CopyArchitectural(FEXCore::Core::CPUState &to, const FEXCore::Core::CPUState &from)
    {
        to.pf_raw = from.pf_raw;
        to.af_raw = from.af_raw;
        to.rip = from.rip;
        memcpy(to.gregs, from.gregs, sizeof to.gregs);
        memcpy(to.avx_high, from.avx_high, sizeof to.avx_high);
        to.xmm = from.xmm;
        to.es_idx = from.es_idx; to.cs_idx = from.cs_idx; to.ss_idx = from.ss_idx; to.ds_idx = from.ds_idx;
        to.gs_idx = from.gs_idx; to.fs_idx = from.fs_idx;
        to.mxcsr = from.mxcsr;
        to.es_cached = from.es_cached; to.cs_cached = from.cs_cached; to.ss_cached = from.ss_cached;
        to.ds_cached = from.ds_cached; to.gs_cached = from.gs_cached; to.fs_cached = from.fs_cached;
        memcpy(to.flags, from.flags, sizeof to.flags);
        memcpy(to.mm, from.mm, sizeof to.mm);
        to.FCW = from.FCW;
        to.AbridgedFTW = from.AbridgedFTW;
    }
    unsigned NewContext(CpuContext *&context) override
    {
        context = reinterpret_cast<CpuContext *>(new FexContext);
        return CpuError::Ok;
    }
    void FreeContext(CpuContext *context) override { delete reinterpret_cast<FexContext *>(context); }
    unsigned Save(CpuContext *context) override
    {
        auto &c = *reinterpret_cast<FexContext *>(context);
        CopyArchitectural(c.state, State());
        c.x87_reduced = X87Reduced();
        c.fsw_shadow = fsw_shadow; c.fip = fip; c.fdp = fdp; c.fcs = fcs; c.fds = fds; c.fop = fop; c.cr0 = cr0;
        memcpy(c.dr, dr, sizeof dr);
        c.gdtr = gdtr; c.ldtr = ldtr;
        return CpuError::Ok;
    }
    unsigned Restore(const CpuContext *context) override
    {
        const auto &c = *reinterpret_cast<const FexContext *>(context);
        CopyArchitectural(State(), c.state);
        if (c.x87_reduced && !X87Reduced())
            RegistersToExtended(State());
        // Each thread has its own rounding control; with doubles it lives in
        // the host FPCR (SetHostRounding).
        if (X87Reduced())
            SetHostRounding((State().FCW >> 10) & 3);
        fsw_shadow = c.fsw_shadow; fip = c.fip; fdp = c.fdp; fcs = c.fcs; fds = c.fds; fop = c.fop; cr0 = c.cr0;
        memcpy(dr, c.dr, sizeof dr);
        gdtr = c.gdtr; ldtr = c.ldtr;
        PointSegmentArrays();
        return CpuError::Ok;
    }
    unsigned ContextGet(const CpuContext *context, int reg, unsigned &value) override
    {
        // The register as the context would show it once restored.
        FexContext live;
        Save(reinterpret_cast<CpuContext *>(&live));
        Restore(context);
        const unsigned error = Get(reg, value);
        Restore(reinterpret_cast<CpuContext *>(&live));
        return error;
    }

    // ------------------------------------------------------------ execution

    // The syscall handler of FEXCore: a hooked address (gate range or the
    // "until" of Run) or INT 2Eh. The guest registers lie in the state.
    void OnSyscall()
    {
        auto &s = State();
        // A guest FLDCW, FLDENV, FRSTOR or FXRSTOR with PC=11b in reduced
        // precision (patch 0002, NEO98X87PrecisionGuard): leave the JIT with
        // NEO98Exit 3 and RIP on the instruction; Run switches the context.
        if (s.NEO98Exit == 3)
            return;
        const unsigned rip = (unsigned)s.rip;
        RunFrame *frame = active;
        if (rip - gate.begin < gate.end - gate.begin)
        {
            if (gate.hook)
                gate.hook(*this, rip, gate.context);
            if (frame->stopped || stop_requested.load(std::memory_order_relaxed))
            {
                s.NEO98Exit = 2;
                return;
            }
            // The hook served the call itself and set EIP to the caller
            // (Application::DirectGate): go on there.
            if ((unsigned)s.rip != rip)
                return;
            // The end of the run lies in the gate range and its hook has
            // nothing to do there (the return address of the TrueType
            // machine): the run ends before the instruction, as under Unicorn.
            if (until && rip == until)
            {
                frame->stopped = true;
                s.NEO98Exit = 2;
                return;
            }
            // The hook let the guest go on: the gate's own instruction runs,
            // which is one of the few the port writes into a gate.
            uint8_t code[5] = {};
            // Gates are 16 bytes apart and aligned, so the five bytes lie in
            // one page; most of the time that page is readable in place.
            if (read_ok[rip >> GuestPageBits] && (rip & 0xfff) <= 0xffb)
                memcpy(code, direct + rip, sizeof code);
            else
                Read(rip, code, sizeof code);
            uint32_t esp = (uint32_t)s.gregs[FEXCore::X86State::REG_RSP], target = 0;
            if (code[0] == 0xc3 || code[0] == 0xc2)
            {
                if (read_ok[esp >> GuestPageBits] && (esp & 0xfff) <= 0xffc)
                    memcpy(&target, direct + esp, 4);
                else
                    Read(esp, &target, 4);
                const unsigned pop = code[0] == 0xc2 ? (unsigned)(code[1] | code[2] << 8) : 0;
                s.gregs[FEXCore::X86State::REG_RSP] = (uint32_t)(esp + 4 + pop);
                s.rip = target;
            }
            else if (code[0] == 0xe9)
            {
                int32_t rel;
                memcpy(&rel, code + 1, 4);
                s.rip = (uint32_t)(rip + 5 + rel);
            }
            else if (code[0] == 0xeb)
                s.rip = (uint32_t)(rip + 2 + (int8_t)code[1]);
            else
            {
                frame->error = CpuError::Exception;
                frame->stopped = true;
                s.NEO98Exit = 2;
            }
            return;
        }
        if (until && rip == until)
        {
            frame->stopped = true;
            s.NEO98Exit = 2;
            return;
        }
        uint8_t code[4] = {};
        Read(rip, code, sizeof code);
        unsigned at = 0;
        bool operand16 = false;
        while (at < 3 && (code[at] == 0x66 || code[at] == 0x26 || code[at] == 0x2e || code[at] == 0x36 ||
                          code[at] == 0x3e || code[at] == 0x64 || code[at] == 0x65))
            operand16 |= code[at++] == 0x66;
        const uint8_t opcode = code[at];
        if ((opcode & 0xf4) == 0xe4)
        {
            // IN and OUT (patched FEXCore sends them here): E4/E5/E6/E7 take
            // an immediate port, EC/ED/EE/EF the port in DX; the odd ones move
            // a word or a doubleword.
            const bool immediate = opcode < 0xe8;
            const unsigned port = immediate ? code[at + 1] : (unsigned)s.gregs[FEXCore::X86State::REG_RDX] & 0xffff;
            const unsigned size = opcode & 1 ? (operand16 ? 2 : 4) : 1;
            const unsigned mask = size == 4 ? 0xffffffffu : size == 2 ? 0xffffu : 0xffu;
            auto &eax = s.gregs[FEXCore::X86State::REG_RAX];
            if (opcode & 2)
            {
                if (ports.output)
                    ports.output(*this, port, size, (unsigned)eax & mask, ports.context);
            }
            else
            {
                const unsigned value = ports.input ? ports.input(*this, port, size, ports.context) : mask;
                eax = (uint32_t)(((unsigned)eax & ~mask) | (value & mask));
            }
            s.rip = rip + at + (immediate ? 2 : 1);
            if (frame->stopped || stop_requested.load(std::memory_order_relaxed))
                s.NEO98Exit = 2;
            return;
        }
        // INT 2Eh: RIP stands on the instruction, the hook sees it done.
        s.rip = rip + 2;
        if (interrupt.hook)
            interrupt.hook(*this, 0x2e, interrupt.context);
        if (frame->stopped || stop_requested.load(std::memory_order_relaxed))
            s.NEO98Exit = 2;
    }

    // NEO98_FEX_STATS: once a second on stderr, what the JIT did in that second: translations and their time,
    // invalidated code (ranges, KiB), self-modifying code drops, cleared code caches,
    // emulated memory accesses and write faults.
    unsigned long long stat_invalidations = 0, stat_invalidated = 0;
    uint64_t stat_last_ns = 0, stat_compiles = 0, stat_compile_ns = 0, stat_clears = 0, stat_seen_invalidations = 0, stat_seen_invalidated = 0, stat_drops = 0;
    unsigned long long stat_emulations = 0, stat_write_faults = 0, stat_faults = 0;
    unsigned long long jit_exits = 0, stat_jit_exits = 0, stat_budget_exits = 0;
    static bool StatsOn()
    {
        static const bool on = getenv("NEO98_FEX_STATS") != nullptr;
        return on;
    }
    void PrintStats()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        const uint64_t now = (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
        if (!stat_last_ns)
            stat_last_ns = now;
        if (now - stat_last_ns < 1000000000ull)
            return;
        const auto &f = NEO98FexStats;
        fprintf(stderr, "FEXSTATS t=%.1f compiles=%llu jit_ms=%.1f clears=%llu invalidations=%llu kib=%llu smc_drops=%llu emulations=%llu write_faults=%llu span_ms=%.1f cpu=%p faults=%llu exits=%llu budget_exits=%llu\n",
                    now / 1e9, (unsigned long long)(f.Compiles - stat_compiles), (f.CompileNs - stat_compile_ns) / 1e6,
                    (unsigned long long)(f.Clears - stat_clears), stat_invalidations - stat_seen_invalidations,
                    (stat_invalidated - stat_seen_invalidated) >> 10, (unsigned long long)(smc_drops - stat_drops),
                    emulations - stat_emulations, write_faults - stat_write_faults, (now - stat_last_ns) / 1e6,
                    static_cast<void *>(this), memory_faults - stat_faults,
                    jit_exits - stat_jit_exits, budget_exits - stat_budget_exits);
        stat_last_ns = now;
        stat_compiles = f.Compiles;
        stat_compile_ns = f.CompileNs;
        stat_clears = f.Clears;
        stat_seen_invalidations = stat_invalidations;
        stat_seen_invalidated = stat_invalidated;
        stat_drops = smc_drops;
        stat_emulations = emulations;
        stat_write_faults = write_faults;
        stat_faults = memory_faults;
        stat_jit_exits = jit_exits;
        stat_budget_exits = budget_exits;
    }
    unsigned Run(unsigned begin, unsigned until_address, unsigned) override
    {
        RunFrame frame;
        frame.outer = active;
        active = &frame;
        auto *cpu_frame = thread->CurrentFrame;
        const uint64_t returning = cpu_frame->ReturningStackLocation, in_syscall = cpu_frame->InSyscallInfo;
        const unsigned outer_until = until;
        FexCpu *outer_running = running;
        // Inside a syscall handler of an outer Run the exit mark of that Run
        // must survive this one (0: the outer run goes on).
        const uint8_t outer_exit = thread->CurrentFrame->State.NEO98Exit;
        running = this;
        // A run inside the syscall handler of another processor (the TrueType
        // machine inside a GDI call of a process): the host FPCR carries the
        // x87 rounding of that processor's thread and comes back with it.
        const bool nested = outer_running && outer_running != this;
        uint64_t outer_fpcr = 0;
        if (nested)
        {
            __asm__ volatile("mrs %0, fpcr" : "=r"(outer_fpcr));
            if (X87Reduced())
                SetHostRounding((State().FCW >> 10) & 3);
        }
        until = until_address;
        if (until)
            ctx->NEO98SetHookRange(1, until, (uint64_t)until + 1);
        auto &s = State();
        s.rip = begin;
        unsigned result = CpuError::Ok;
        for (;;)
        {
            if (stop_requested.exchange(false))
                break;
            if (x87_switch_pending)
                X87Full();
            s.NEO98Exit = 0;
            s.NEO98CycleOffset = CycleOffset();
            cpu_frame->SynchronousFaultData.FaultToTopAndGeneratedException = 0;
            fault_error = CpuError::Ok;
            cpu_frame->InSyscallInfo = 0;
            ctx->ExecuteThread(thread);
            if (StatsOn()) {
                ++jit_exits;
                PrintStats();
            }
            if (fault_error)
            {
                result = fault_error;
                break;
            }
            // A store rewrote translated code (OnFault): run on from the
            // instruction, in fresh translations.
            if (s.NEO98Exit == 4)
            {
                if (Debug())
                    fprintf(stderr, "FEXCPU smc restart resumes rip=%08llx\n", (unsigned long long)s.rip);
                continue;
            }
            if (s.NEO98Exit == 3 || x87_switch_pending)
            {
                X87Full();
                if (s.NEO98Exit == 3)
                    continue;
            }
            if (s.NEO98Exit == 2)
            {
                result = frame.error;
                break;
            }
            if (s.NEO98Exit == 1)
            {
                ++budget_exits;
                if (stop_requested.exchange(false))
                    break;
                if (budget.hook)
                    budget.hook(*this, (unsigned)s.rip, budget.context);
                if (frame.stopped)
                    break;
                // The block the budget stopped before runs now, as it does
                // after the budget callback of Unicorn: its entry counts once
                // more, so give that count back.
                if (s.NEO98Budget < 0x7fffffff)
                    ++s.NEO98Budget;
                continue;
            }
            if (cpu_frame->SynchronousFaultData.FaultToTopAndGeneratedException)
            {
                result = Trap();
                if (result || frame.stopped)
                    break;
                continue;
            }
            result = CpuError::Exception;
            break;
        }
        if (until)
            ctx->NEO98SetHookRange(1, outer_until, outer_until ? (uint64_t)outer_until + 1 : 0);
        until = outer_until;
        if (nested)
            __asm__ volatile("msr fpcr, %0" ::"r"(outer_fpcr));
        running = outer_running;
        cpu_frame->ReturningStackLocation = returning;
        cpu_frame->InSyscallInfo = in_syscall;
        cpu_frame->State.NEO98Exit = outer_exit;
        active = frame.outer;
        return result;
    }
    // BOUND r16/32, m16&16/m32&32 with 32-bit addressing and the flat
    // segments of a Win32 process. Other: not that instruction, or bytes or
    // bounds the guest cannot read (then the invalid opcode stands).
    enum class BoundResult { Inside, Outside, Other };
    template <class GuestState> BoundResult Bound(GuestState &s, unsigned rip)
    {
        uint8_t code[12] = {};
        unsigned refused = 0, at = 0;
        if (!GuestAccess(rip, code, sizeof code, false, refused))
            return BoundResult::Other;
        const bool word = code[at] == 0x66;
        if (word)
            ++at;
        if (code[at++] != 0x62)
            return BoundResult::Other;
        const unsigned modrm = code[at++], mod = modrm >> 6, reg = (modrm >> 3) & 7, rm = modrm & 7;
        if (mod == 3)
            return BoundResult::Other;
        auto general = [&](unsigned index) { return (uint32_t)s.gregs[index]; };
        uint32_t address = 0;
        if (rm == 4)
        {
            const unsigned sib = code[at++], scale = sib >> 6, index = (sib >> 3) & 7, base = sib & 7;
            if (index != 4)
                address += general(index) << scale;
            if (base == 5 && mod == 0)
            {
                uint32_t displacement = 0;
                memcpy(&displacement, code + at, 4);
                at += 4;
                address += displacement;
            }
            else
                address += general(base);
        }
        else if (rm == 5 && mod == 0)
        {
            memcpy(&address, code + at, 4);
            at += 4;
        }
        else
            address = general(rm);
        if (mod == 1)
            address += (uint32_t)(int32_t)(int8_t)code[at++];
        else if (mod == 2)
        {
            uint32_t displacement = 0;
            memcpy(&displacement, code + at, 4);
            at += 4;
            address += displacement;
        }
        int32_t lower = 0, upper = 0, value = (int32_t)general(reg);
        if (word)
        {
            int16_t bounds[2] = {};
            if (!GuestAccess(address, bounds, sizeof bounds, false, refused))
                return BoundResult::Other;
            lower = bounds[0];
            upper = bounds[1];
            value = (int16_t)value;
        }
        else
        {
            int32_t bounds[2] = {};
            if (!GuestAccess(address, bounds, sizeof bounds, false, refused))
                return BoundResult::Other;
            lower = bounds[0];
            upper = bounds[1];
        }
        if (value < lower || value > upper)
            return BoundResult::Outside;
        s.rip = rip + at;
        return BoundResult::Inside;
    }
    // A Break of the guest: INT n, INT3, a single step, an invalid opcode, a
    // fetch from memory the guest cannot read.
    unsigned Trap()
    {
        auto &s = State();
        const auto data = thread->CurrentFrame->SynchronousFaultData;
        thread->CurrentFrame->SynchronousFaultData.FaultToTopAndGeneratedException = 0;
        using namespace FEXCore::X86State;
        const unsigned rip = (unsigned)s.rip;
        if (data.Signal == FEXCore::Core::FAULT_SIGILL)
        {
            // FEXCore has no BOUND (62 /r), the range check Delphi and Turbo
            // Pascal compile around every array index with {$R+} (WinPac 2). The instruction only compares, so it is carried out
            // here: inside the bounds the guest goes on behind it, outside it
            // gets interrupt 5 with EIP at the instruction, as on a 386.
            switch (Bound(s, rip))
            {
            case BoundResult::Inside: return CpuError::Ok;
            case BoundResult::Outside:
                if (interrupt.hook)
                    interrupt.hook(*this, 5, interrupt.context);
                return CpuError::Ok;
            case BoundResult::Other: break;
            }
            return CpuError::InvalidInstruction;
        }
        if (data.TrapNo == X86_TRAPNO_GP && (data.err_code & 7) == 2)
        {
            s.rip = rip + 2;
            if (interrupt.hook)
                interrupt.hook(*this, data.err_code >> 3, interrupt.context);
            return CpuError::Ok;
        }
        if (data.TrapNo == X86_TRAPNO_PF || (data.Signal == FEXCore::Core::FAULT_SIGSEGV && data.TrapNo != X86_TRAPNO_GP))
        {
            const unsigned kind = pages[rip >> GuestPageBits] & PageMapped ? CpuFault::FetchProtected : CpuFault::FetchUnmapped;
            failed_fetch[rip >> GuestPageBits] = true;
            if (fault.hook && fault.hook(*this, kind, rip, 1, fault.context))
                return CpuError::Ok;
            return kind == CpuFault::FetchProtected ? CpuError::FetchProtected : CpuError::FetchUnmapped;
        }
        if (data.TrapNo == X86_TRAPNO_GP)
            return CpuError::Exception;
        if (interrupt.hook)
            interrupt.hook(*this, data.TrapNo, interrupt.context);
        return CpuError::Ok;
    }
    // The engine state of the Runs open on one host stack (see Cpu::DetachFrame).
    struct EngineFrame
    {
        uint64_t returning = 0, in_syscall_info = 0;
        RunFrame *active = nullptr;
        unsigned until = 0;
        FexCpu *running = nullptr;
        bool in_syscall = false;
        uint8_t exit = 0;
    };
    void *DetachFrame() override
    {
        if (!active)
            return nullptr;
        auto *frame = new EngineFrame;
        auto *cpu_frame = thread->CurrentFrame;
        *frame = {cpu_frame->ReturningStackLocation, cpu_frame->InSyscallInfo, active, until, running, in_syscall,
                  cpu_frame->State.NEO98Exit};
        cpu_frame->ReturningStackLocation = 0;
        cpu_frame->InSyscallInfo = 0;
        active = nullptr;
        if (until)
            ctx->NEO98SetHookRange(1, 0, 0);
        until = 0;
        running = nullptr;
        in_syscall = false;
        cpu_frame->State.NEO98Exit = 0;
        return frame;
    }
    void AttachFrame(void *opaque) override
    {
        auto *frame = static_cast<EngineFrame *>(opaque);
        if (!frame)
            return;
        auto *cpu_frame = thread->CurrentFrame;
        cpu_frame->ReturningStackLocation = frame->returning;
        cpu_frame->InSyscallInfo = frame->in_syscall_info;
        active = frame->active;
        until = frame->until;
        if (until)
            ctx->NEO98SetHookRange(1, until, (uint64_t)until + 1);
        running = frame->running;
        in_syscall = frame->in_syscall;
        cpu_frame->State.NEO98Exit = frame->exit;
        delete frame;
    }
    void Stop() override
    {
        if (active)
            active->stopped = true;
        State().NEO98Exit = 2;
    }
    void RequestStop() override
    {
        stop_requested.store(true);
        __atomic_store_n(&State().NEO98Budget, -1, __ATOMIC_RELAXED);
    }
    void Budget(int blocks) override { State().NEO98Budget = blocks; }

    unsigned SetGateHook(unsigned begin, unsigned end, GateHook hook, void *context) override
    {
        gate = {begin, end + 1, hook, context};
        ctx->NEO98SetHookRange(0, begin, (uint64_t)end + 1);
        return CpuError::Ok;
    }
    unsigned SetFaultHook(FaultHook hook, void *context) override
    {
        fault = {hook, context};
        return CpuError::Ok;
    }
    unsigned SetInterruptHook(InterruptHook hook, void *context) override
    {
        interrupt = {hook, context};
        return CpuError::Ok;
    }
    unsigned SetPortHooks(PortInputHook input, PortOutputHook output, void *context) override
    {
        ports = {input, output, context};
        return CpuError::Ok;
    }
    unsigned SetBudgetHook(BudgetHook hook, void *context, bool &counted) override
    {
        budget = {hook, context};
        counted = true;
        return CpuError::Ok;
    }
    // GetWriteWatch: while watching, a writable host page stays read-only until
    // its first write, which reports the whole host page (four guest pages)
    // and makes it writable until the caller re-arms it.
    unsigned WatchWrites(WriteHook hook, void *context) override
    {
        if (const unsigned error = ImageDataWriteWatch(hook != nullptr)) return error;
        writes = {hook, context};
        memset(write_reported, 0, GuestPages);
        ApplyAll();
        return CpuError::Ok;
    }
    void UnwatchWrites() override
    {
        ImageDataWriteWatch(false);
        writes = {};
        ApplyAll();
    }
    void RearmWrites(unsigned address, unsigned size) override
    {
        if (!writes.hook || !size)
            return;
        for (uint64_t p = (uint64_t)address >> GuestPageBits; p <= ((uint64_t)address + size - 1) >> GuestPageBits; ++p)
            if (write_reported[p])
            {
                write_reported[p] = 0;
                if (host_backed[p >> (host_page_bits - GuestPageBits)])
                    ApplyHost(p >> (host_page_bits - GuestPageBits));
            }
    }
    void ApplyAll()
    {
        for (uint64_t h = 0; h < (WindowBytes >> host_page_bits); ++h)
            if (host_backed[h])
                ApplyHost(h);
    }
    unsigned AddTrace(TraceKind, unsigned, unsigned, TraceHook, void *) override { return CpuError::Hook; }

    // ------------------------------------------------------------ faults

    // Before the guest writes a page: GetWriteWatch learns of it, and code
    // translated from it goes.
    void BeforeGuestWrite(unsigned page)
    {
        const uint64_t h = page >> (host_page_bits - GuestPageBits);
        if (writes.hook && !write_reported[page])
        {
            write_reported[page] = 1;
            writes.hook(*this, page << GuestPageBits, 1u << GuestPageBits, writes.context);
            ApplyHost(h);
        }
        if ((pages[page] & PageCode) && !checked[h])
        {
            DropPage(page);
            ++smc_drops;
            // Code and data the guest writes share this guest page. Instead
            // of losing the code at every write, its blocks check their own
            // bytes from now on.
            // The other guest pages of this host page keep translations
            // made without the check, and from now on no write to them
            // faults: they go too, and come back checked.
            if (++drops[h] >= 2 && !has_side[h])
            {
                checked[h] = 1;
                DropHostPage(h);
            }
            ApplyHost(h);
        }
    }
    static bool Allowed(uint8_t bits, bool write)
    {
        return (bits & PageMapped) && (write ? (bits & PageWrite) : (bits & (PageRead | PageWrite)));
    }
    // A guest access of the emulation, page by page. Answers false with the
    // guest address the guest may not touch.
    bool GuestAccess(unsigned address, void *buffer, unsigned size, bool write, unsigned &refused)
    {
        if (device_access && address >= device_base && uint64_t(address) + size <= uint64_t(device_base) + device_size)
        {
            device_access(device_context, address, static_cast<unsigned char *>(buffer), size, write);
            return true;
        }

        for (unsigned done = 0; done < size;)
        {
            const unsigned at = address + done;
            if (!Allowed(pages[at >> GuestPageBits], write))
            {
                refused = at;
                return false;
            }
            done += std::min(size - done, (1u << GuestPageBits) - (at & ((1u << GuestPageBits) - 1)));
        }
        if (write)
        {
            for (uint64_t p = address >> GuestPageBits; p <= ((uint64_t)address + size - 1) >> GuestPageBits; ++p)
                BeforeGuestWrite((unsigned)p);
            if (Write(address, buffer, size)) { refused = address; return false; }
        }
        else
            if (Read(address, buffer, size)) { refused = address; return false; }
        return true;
    }

    // Registers of the interrupted host thread.
    struct Machine
    {
        ucontext_t *uc;
        uint64_t X(unsigned n, bool sp) const
        {
            auto &ss = uc->uc_mcontext->__ss;
            if (n < 29) return ss.__x[n];
            if (n == 29) return ss.__fp;
            if (n == 30) return ss.__lr;
            return sp ? ss.__sp : 0;
        }
        void SetX(unsigned n, uint64_t value, bool sp)
        {
            auto &ss = uc->uc_mcontext->__ss;
            if (n < 29) ss.__x[n] = value;
            else if (n == 29) ss.__fp = value;
            else if (n == 30) ss.__lr = value;
            else if (sp) ss.__sp = value;
        }
        uint8_t *V(unsigned n) const { return reinterpret_cast<uint8_t *>(&uc->uc_mcontext->__ns.__v[n]); }
    };
    enum class Emulated { Done, Refused, Unknown };
    static uint64_t SignExtend(uint64_t value, unsigned bits)
    {
        const uint64_t sign = 1ull << (bits - 1);
        value &= bits == 64 ? ~0ull : (sign << 1) - 1;
        return (value ^ sign) - sign;
    }
    // Loads and stores into a register from the bytes of the access.
    static void LoadInteger(Machine &m, unsigned rt, const uint8_t *bytes, unsigned size, bool sign, bool to64)
    {
        uint64_t value = 0;
        memcpy(&value, bytes, size);
        if (sign)
        {
            value = SignExtend(value, size * 8);
            if (!to64)
                value &= 0xffffffffull;
        }
        m.SetX(rt, value, false);
    }
    // The guest address of a host address in the window or in the direct view
    // (checked blocks use the latter, and an unaligned atomic faults there).
    bool GuestOf(uint64_t host, unsigned size, unsigned &guest)
    {
        uint64_t offset = host - reinterpret_cast<uint64_t>(window);
        if (offset >= DirectOffset)
            offset -= DirectOffset;
        if (offset >= WindowBytes || offset + size > WindowBytes)
            return false;
        guest = (unsigned)offset;
        return true;
    }
    // Carries out the load or store at pc against the guest pages. The
    // translated code of FEXCore addresses guest memory as window + address,
    // so the host address of the access leads back to the guest address.
    Emulated Emulate(Machine &m, uint32_t in, unsigned &refused, bool &write_out)
    {
        const uint64_t base = reinterpret_cast<uint64_t>(window);
        auto guest_of = [&](uint64_t host, unsigned size, unsigned &guest) { return GuestOf(host, size, guest); };
        const unsigned rt = in & 31, rn = (in >> 5) & 31;
        uint8_t data[32] = {};
        unsigned guest = 0;
        // Load/store register: unsigned offset, unscaled, pre/post index,
        // register offset.
        if ((in & 0x3a000000) == 0x38000000)
        {
            const unsigned size_bits = in >> 30, opc = (in >> 22) & 3;
            const bool vector = (in >> 26) & 1;
            uint64_t address = m.X(rn, true);
            uint64_t writeback = 0;
            bool has_writeback = false;
            unsigned size = 1u << size_bits;
            bool load, sign = false, to64 = false;
            if (vector)
            {
                if (opc & 2)
                {
                    if (size_bits)
                        return Emulated::Unknown;
                    size = 16;
                }
                load = opc & 1;
            }
            else
            {
                load = opc != 0;
                if (opc >= 2)
                {
                    if (size_bits == 3)
                    {
                        // PRFM: no access at all.
                        if (opc == 2)
                        {
                            m.uc->uc_mcontext->__ss.__pc += 4;
                            return Emulated::Done;
                        }
                        return Emulated::Unknown;
                    }
                    sign = true;
                    to64 = opc == 2;
                }
            }
            const unsigned scale = __builtin_ctz(size);
            if (in & (1u << 24))
                address += (uint64_t)((in >> 10) & 0xfff) << scale;
            else if (!((in >> 21) & 1))
            {
                const int64_t imm9 = (int64_t)SignExtend((in >> 12) & 0x1ff, 9);
                const unsigned index = (in >> 10) & 3;
                if (index == 1)
                {
                    writeback = address + imm9;
                    has_writeback = true;
                }
                else
                {
                    address += imm9;
                    if (index == 3)
                    {
                        writeback = address;
                        has_writeback = true;
                    }
                }
            }
            else if (((in >> 10) & 3) == 2)
            {
                const unsigned rm = (in >> 16) & 31, option = (in >> 13) & 7;
                uint64_t offset = m.X(rm, false);
                switch (option)
                {
                case 2: offset = (uint32_t)offset; break;
                case 3: case 7: break;
                case 6: offset = SignExtend(offset, 32); break;
                default: return Emulated::Unknown;
                }
                if ((in >> 12) & 1)
                    offset <<= scale;
                address += offset;
            }
            else
                return EmulateAtomic(m, in, refused, write_out);
            if (!guest_of(address, size, guest))
                return Emulated::Unknown;
            write_out = !load;
            if (load)
            {
                if (!GuestAccess(guest, data, size, false, refused))
                    return Emulated::Refused;
                if (vector)
                {
                    memset(m.V(rt), 0, 16);
                    memcpy(m.V(rt), data, size);
                }
                else if (rt != 31)
                    LoadInteger(m, rt, data, size, sign, to64);
            }
            else
            {
                if (vector)
                    memcpy(data, m.V(rt), size);
                else
                {
                    const uint64_t value = m.X(rt, false);
                    memcpy(data, &value, size);
                }
                if (!GuestAccess(guest, data, size, true, refused))
                    return Emulated::Refused;
            }
            if (has_writeback)
                m.SetX(rn, writeback, true);
            m.uc->uc_mcontext->__ss.__pc += 4;
            return Emulated::Done;
        }
        // Load/store pair.
        if ((in & 0x3a000000) == 0x28000000)
        {
            const unsigned opc = in >> 30, index = (in >> 23) & 3, rt2 = (in >> 10) & 31;
            const bool vector = (in >> 26) & 1, load = (in >> 22) & 1;
            unsigned size;
            bool sign = false;
            if (vector)
            {
                if (opc == 3)
                    return Emulated::Unknown;
                size = 4u << opc;
            }
            else
            {
                if (opc == 3 || (opc == 1 && !load))
                    return Emulated::Unknown;
                size = opc == 2 ? 8 : 4;
                sign = opc == 1;
            }
            const int64_t offset = (int64_t)SignExtend((in >> 15) & 0x7f, 7) * size;
            uint64_t address = m.X(rn, true), writeback = 0;
            bool has_writeback = false;
            if (index == 1)
            {
                writeback = address + offset;
                has_writeback = true;
            }
            else
            {
                address += offset;
                if (index == 3)
                {
                    writeback = address;
                    has_writeback = true;
                }
            }
            if (!guest_of(address, 2 * size, guest))
                return Emulated::Unknown;
            write_out = !load;
            if (load)
            {
                if (!GuestAccess(guest, data, 2 * size, false, refused))
                    return Emulated::Refused;
                if (vector)
                {
                    memset(m.V(rt), 0, 16);
                    memcpy(m.V(rt), data, size);
                    memset(m.V(rt2), 0, 16);
                    memcpy(m.V(rt2), data + size, size);
                }
                else
                {
                    if (rt != 31)
                        LoadInteger(m, rt, data, size, sign, true);
                    if (rt2 != 31)
                        LoadInteger(m, rt2, data + size, size, sign, true);
                }
            }
            else
            {
                if (vector)
                {
                    memcpy(data, m.V(rt), size);
                    memcpy(data + size, m.V(rt2), size);
                }
                else
                {
                    const uint64_t first = m.X(rt, false), second = m.X(rt2, false);
                    memcpy(data, &first, size);
                    memcpy(data + size, &second, size);
                }
                if (!GuestAccess(guest, data, 2 * size, true, refused))
                    return Emulated::Refused;
            }
            if (has_writeback)
                m.SetX(rn, writeback, true);
            m.uc->uc_mcontext->__ss.__pc += 4;
            return Emulated::Done;
        }
        // Exclusive and ordered loads and stores, compare and swap.
        if ((in & 0x3f000000) == 0x08000000)
        {
            const unsigned size_bits = in >> 30, rs = (in >> 16) & 31, rt2 = (in >> 10) & 31;
            const bool o2 = (in >> 23) & 1, load = (in >> 22) & 1, o1 = (in >> 21) & 1;
            uint64_t address = m.X(rn, true);
            unsigned size = 1u << size_bits;
            if (o1 && !o2 && !(in >> 31))
            {
                // CASP: pairs of 4 or 8 bytes.
                const unsigned element = (in >> 30) & 1 ? 8 : 4;
                if (!guest_of(address, 2 * element, guest))
                    return Emulated::Unknown;
                if (!GuestAccess(guest, data, 2 * element, false, refused))
                    return Emulated::Refused;
                uint64_t old0 = 0, old1 = 0;
                memcpy(&old0, data, element);
                memcpy(&old1, data + element, element);
                const uint64_t mask = element == 8 ? ~0ull : 0xffffffffull;
                if (old0 == (m.X(rs, false) & mask) && old1 == (m.X(rs + 1, false) & mask))
                {
                    const uint64_t new0 = m.X(rt, false), new1 = m.X(rt + 1, false);
                    memcpy(data, &new0, element);
                    memcpy(data + element, &new1, element);
                    write_out = true;
                    if (!GuestAccess(guest, data, 2 * element, true, refused))
                        return Emulated::Refused;
                }
                m.SetX(rs, old0, false);
                m.SetX(rs + 1, old1, false);
                m.uc->uc_mcontext->__ss.__pc += 4;
                return Emulated::Done;
            }
            if (o1 && o2)
            {
                // CAS.
                if (!guest_of(address, size, guest))
                    return Emulated::Unknown;
                if (!GuestAccess(guest, data, size, false, refused))
                    return Emulated::Refused;
                uint64_t old = 0;
                memcpy(&old, data, size);
                const uint64_t mask = size == 8 ? ~0ull : (1ull << (size * 8)) - 1;
                if (old == (m.X(rs, false) & mask))
                {
                    const uint64_t value = m.X(rt, false);
                    memcpy(data, &value, size);
                    write_out = true;
                    if (!GuestAccess(guest, data, size, true, refused))
                        return Emulated::Refused;
                }
                m.SetX(rs, old, false);
                m.uc->uc_mcontext->__ss.__pc += 4;
                return Emulated::Done;
            }
            const bool pair = o1;
            if (pair)
                size = (in >> 30) & 1 ? 8 : 4;
            const unsigned total = pair ? 2 * size : size;
            if (!guest_of(address, total, guest))
                return Emulated::Unknown;
            write_out = !load;
            if (load)
            {
                if (!GuestAccess(guest, data, total, false, refused))
                    return Emulated::Refused;
                LoadInteger(m, rt, data, size, false, true);
                if (pair)
                    LoadInteger(m, rt2, data + size, size, false, true);
            }
            else
            {
                const uint64_t first = m.X(rt, false), second = pair ? m.X(rt2, false) : 0;
                memcpy(data, &first, size);
                if (pair)
                    memcpy(data + size, &second, size);
                if (!GuestAccess(guest, data, total, true, refused))
                    return Emulated::Refused;
                // A store exclusive always succeeds here: every guest thread
                // runs on this host thread.
                if (!o2)
                    m.SetX(rs, 0, false);
            }
            m.uc->uc_mcontext->__ss.__pc += 4;
            return Emulated::Done;
        }
        // LD1/ST1 of one lane, and LD1R.
        if ((in & 0xbf800000) == 0x0d000000 || (in & 0xbf800000) == 0x0d800000)
        {
            const bool q = (in >> 30) & 1, load = (in >> 22) & 1, post = (in >> 23) & 1;
            const unsigned opcode = (in >> 13) & 7, s = (in >> 12) & 1, size_field = (in >> 10) & 3;
            if ((in >> 21) & 1)
                return Emulated::Unknown;
            unsigned size, lane;
            bool replicate = false;
            switch (opcode)
            {
            case 0: size = 1; lane = (q << 3) | (s << 2) | size_field; break;
            case 2: size = 2; lane = (q << 2) | (s << 1) | (size_field >> 1); break;
            case 4:
                if (size_field & 1)
                {
                    size = 8;
                    lane = q;
                }
                else
                {
                    size = 4;
                    lane = (q << 1) | s;
                }
                break;
            case 6:
                if (!load)
                    return Emulated::Unknown;
                size = 1u << size_field;
                lane = 0;
                replicate = true;
                break;
            default: return Emulated::Unknown;
            }
            const uint64_t address = m.X(rn, true);
            if (!guest_of(address, size, guest))
                return Emulated::Unknown;
            write_out = !load;
            if (load)
            {
                if (!GuestAccess(guest, data, size, false, refused))
                    return Emulated::Refused;
                if (replicate)
                {
                    uint8_t *v = m.V(rt);
                    memset(v, 0, 16);
                    for (unsigned at = 0; at < (q ? 16u : 8u); at += size)
                        memcpy(v + at, data, size);
                }
                else
                    memcpy(m.V(rt) + lane * size, data, size);
            }
            else
            {
                memcpy(data, m.V(rt) + lane * size, size);
                if (!GuestAccess(guest, data, size, true, refused))
                    return Emulated::Refused;
            }
            if (post)
            {
                const unsigned rm = (in >> 16) & 31;
                m.SetX(rn, address + (rm == 31 ? size : m.X(rm, false)), true);
            }
            m.uc->uc_mcontext->__ss.__pc += 4;
            return Emulated::Done;
        }
        // LD1/ST1 of one to four whole registers.
        if ((in & 0xbfbf0000) == 0x0c000000 || (in & 0xbfa00000) == 0x0c800000)
        {
            const bool q = (in >> 30) & 1, load = (in >> 22) & 1, post = (in >> 23) & 1;
            const unsigned opcode = (in >> 12) & 15;
            unsigned count;
            switch (opcode)
            {
            case 7: count = 1; break;
            case 10: count = 2; break;
            case 6: count = 3; break;
            case 2: count = 4; break;
            default: return Emulated::Unknown;
            }
            const unsigned each = q ? 16 : 8, total = each * count;
            const uint64_t address = m.X(rn, true);
            if (!guest_of(address, total, guest))
                return Emulated::Unknown;
            uint8_t wide[64] = {};
            write_out = !load;
            if (load)
            {
                if (!GuestAccess(guest, wide, total, false, refused))
                    return Emulated::Refused;
                for (unsigned i = 0; i < count; ++i)
                {
                    memset(m.V((rt + i) & 31), 0, 16);
                    memcpy(m.V((rt + i) & 31), wide + i * each, each);
                }
            }
            else
            {
                for (unsigned i = 0; i < count; ++i)
                    memcpy(wide + i * each, m.V((rt + i) & 31), each);
                if (!GuestAccess(guest, wide, total, true, refused))
                    return Emulated::Refused;
            }
            if (post)
            {
                const unsigned rm = (in >> 16) & 31;
                m.SetX(rn, address + (rm == 31 ? total : m.X(rm, false)), true);
            }
            m.uc->uc_mcontext->__ss.__pc += 4;
            return Emulated::Done;
        }
        return Emulated::Unknown;
    }
    // LSE atomics (LDADD, LDCLR, LDEOR, LDSET, the min/max family, SWP) and
    // LDAPR.
    Emulated EmulateAtomic(Machine &m, uint32_t in, unsigned &refused, bool &write_out)
    {
        const unsigned size = 1u << (in >> 30), rt = in & 31, rn = (in >> 5) & 31, rs = (in >> 16) & 31;
        const unsigned o3 = (in >> 15) & 1, opc = (in >> 12) & 7;
        if ((in >> 26) & 1)
            return Emulated::Unknown;
        const uint64_t address = m.X(rn, true);
        unsigned guest = 0;
        if (!GuestOf(address, size, guest))
            return Emulated::Unknown;
        uint8_t data[8] = {};
        if (!GuestAccess(guest, data, size, false, refused))
            return Emulated::Refused;
        uint64_t old = 0;
        memcpy(&old, data, size);
        const uint64_t mask = size == 8 ? ~0ull : (1ull << (size * 8)) - 1;
        const uint64_t operand = m.X(rs, false) & mask;
        uint64_t result;
        if (o3)
        {
            if (opc == 0)
                result = operand;
            else if (opc == 4)
            {
                // LDAPR: a load.
                if (rt != 31)
                    m.SetX(rt, old, false);
                m.uc->uc_mcontext->__ss.__pc += 4;
                return Emulated::Done;
            }
            else
                return Emulated::Unknown;
        }
        else
        {
            const int64_t sold = (int64_t)SignExtend(old, size * 8), soperand = (int64_t)SignExtend(operand, size * 8);
            switch (opc)
            {
            case 0: result = old + operand; break;
            case 1: result = old & ~operand; break;
            case 2: result = old ^ operand; break;
            case 3: result = old | operand; break;
            case 4: result = (uint64_t)std::max(sold, soperand); break;
            case 5: result = (uint64_t)std::min(sold, soperand); break;
            case 6: result = std::max(old, operand); break;
            default: result = std::min(old, operand); break;
            }
        }
        result &= mask;
        memcpy(data, &result, size);
        write_out = true;
        if (!GuestAccess(guest, data, size, true, refused))
            return Emulated::Refused;
        if (rt != 31)
            m.SetX(rt, old, false);
        m.uc->uc_mcontext->__ss.__pc += 4;
        return Emulated::Done;
    }

    // A fault in this processor's window. Answers whether the handler dealt
    // with it; the ucontext is changed where needed.
    bool OnFault(uint64_t host_address, ucontext_t *uc)
    {
        const uint64_t pc = uc->uc_mcontext->__ss.__pc;
        const uint64_t esr = uc->uc_mcontext->__es.__esr;
        bool write = (esr >> 6) & 1;
        // Data fault status code 0x21: an unaligned access the instruction
        // does not allow (atomics, exclusives); the handler carries it out.
        const bool alignment = (esr & 0x3f) == 0x21;
        unsigned guest = 0;
        if (!GuestOf(host_address, 1, guest))
            return false;
        uint8_t bits = pages[guest >> GuestPageBits];
        const bool jit = ctx->IsAddressInCodeBuffer(thread, pc);
        if (Debug() && ++debug_faults <= 200)
            fprintf(stderr, "FEXCPU fault guest=%08x write=%d bits=%02x hostprot=%u pc=%llx jit=%d\n", guest, write, bits,
                    host_prot[guest >> host_page_bits], (unsigned long long)pc, (int)jit);
        if (!jit)
            return false;
        ++memory_faults;
        write_faults += write;
        uint32_t instruction = 0;
        memcpy(&instruction, reinterpret_cast<const void *>(pc), 4);
        if (Allowed(bits, write))
        {
            // The guest may do this; the host page refused it for a
            // neighbour's sake, for code to drop or for GetWriteWatch.
            if (write && !alignment)
            {
                const unsigned long long drops_before = smc_drops;
                BeforeGuestWrite(guest >> GuestPageBits);
                // The store dropped translated code, and the block doing the
                // store may be part of it: Cinepak's generated loops patch the
                // immediate of their own CMP and MOV. Going on in the
                // old host code would run the old immediate. The instruction
                // starts over from its guest address instead, in code
                // translated anew; the second drop of a host page makes its
                // blocks check their own bytes, so this happens at most twice
                // per page. A host page with side pages never gets checked
                // blocks and keeps the old way.
                if (smc_drops != drops_before && !has_side[guest >> host_page_bits])
                {
                    // Code on this host page gets rewritten while it runs, so
                    // its blocks check their own bytes from now on; the block
                    // translated for the restart still holds the bytes from
                    // before the store and finds out at its next check.
                    const uint64_t written = guest >> host_page_bits;
                    if (!checked[written])
                    {
                        checked[written] = 1;
                        DropHostPage(written);
                        ApplyHost(written);
                    }
                    auto *frame = thread->CurrentFrame;
                    frame->State.rip = ctx->RestoreRIPFromHostPC(thread, pc);
                    frame->State.NEO98Exit = 4;
                    if (Debug())
                        fprintf(stderr, "FEXCPU smc restart guest=%08x rip=%08llx checked=%d side=%d bits=%02x\n", guest,
                                (unsigned long long)frame->State.rip, (int)checked[written], (int)has_side[written],
                                pages[guest >> GuestPageBits]);
                    ++smc_restarts;
                    uc->uc_mcontext->__ss.__pc = frame->Pointers.GuestSignal_SIGSEGV;
                    return true;
                }
                if (host_prot[guest >> host_page_bits] & PROT_WRITE)
                    return true;
            }
            Machine m {uc};
            unsigned refused = 0;
            bool emulated_write = false;
            const Emulated result = Emulate(m, instruction, refused, emulated_write);
            if (result == Emulated::Done)
            {
                // NEO98_FEX_TRACE_PAGE=<hex guest address>: every emulated
                // access to that host page, for finding out why it is slow.
                static const unsigned trace_page = getenv("NEO98_FEX_TRACE_PAGE") ? (unsigned)strtoul(getenv("NEO98_FEX_TRACE_PAGE"), nullptr, 16) >> host_page_bits : ~0u;
                if ((guest >> host_page_bits) == trace_page)
                {
                    const unsigned page = guest >> GuestPageBits;
                    fprintf(stderr, "FEXCPU emulated %08x guest=%08x write=%d rip=%08x bits=%02x side=%d checked=%d reported=%d watch=%d code_checked=%d read_ok=%d write_ok=%d\n",
                            instruction, guest, (int)emulated_write, (unsigned)ctx->RestoreRIPFromHostPC(thread, pc), pages[page],
                            (int)has_side[page >> (host_page_bits - GuestPageBits)], (int)checked[page >> (host_page_bits - GuestPageBits)],
                            (int)write_reported[page], writes.hook != nullptr,
                            (int)(access_emulations[(unsigned)ctx->RestoreRIPFromHostPC(thread, pc)] >= 4),
                            (int)read_ok[page], (int)write_ok[page]);
                }
                ++emulations;
                // Learn the RIP restored by the same metadata boundaries the
                // JIT uses for checked accesses. Invalidate every translation
                // covering it, including entries on other pages.
                const unsigned code_page = (unsigned)ctx->GetGuestBlockEntry(thread) >> GuestPageBits;
                const unsigned instruction_rip = (unsigned)ctx->RestoreRIPFromHostPC(thread, pc);
                if (!alignment && access_emulations[instruction_rip] < 4 && ++access_emulations[instruction_rip] == 4)
                {
                    if (Debug())
                        fprintf(stderr, "FEXCPU check accesses entry=%08x fault_rip=%08x\n",
                                (unsigned)ctx->GetGuestBlockEntry(thread), (unsigned)ctx->RestoreRIPFromHostPC(thread, pc));
                    if (!checked_code[code_page])
                        ++checked_code_pages;
                    checked_code[code_page] = 1;
                    InvalidateRange(instruction_rip, 1);
                    if ((instruction_rip >> GuestPageBits) != code_page)
                        InvalidateRange((uint64_t)code_page << GuestPageBits, 1u << GuestPageBits);
                }
                const uint64_t h = guest >> host_page_bits;
                if (Debug())
                    ++emulated_pages[h];
                // A host page whose code keeps the data beside it read-only:
                // its blocks check their own bytes from now on and the page
                // stays writable.
                // NEO98_FEX_CRC_MIXED=1: the older way, for comparison.
                static const bool crc_mixed = getenv("NEO98_FEX_CRC_MIXED") != nullptr;
                if (crc_mixed && emulated_write && !has_side[h] && emul[h] < 255 && ++emul[h] == 32)
                {
                    bool code = false;
                    for (unsigned i = 0; i < PerHost(); ++i)
                        code |= (pages[h * PerHost() + i] & PageCode) != 0;
                    if (code && !checked[h])
                    {
                        checked[h] = 1;
                        DropHostPage(h);
                    }
                }
                return true;
            }
            if (result == Emulated::Unknown)
            {
                fprintf(stderr, "FEXCPU cannot emulate %08x at pc %llx for guest %08x\n", instruction,
                        (unsigned long long)pc, guest);
                return false;
            }
            // The access runs into a page the guest may not touch.
            guest = refused;
            write = emulated_write;
            bits = pages[guest >> GuestPageBits];
        }
        unsigned kind;
        if (!(bits & PageMapped))
            kind = write ? CpuFault::WriteUnmapped : CpuFault::ReadUnmapped;
        else
            kind = write ? CpuFault::WriteProtected : CpuFault::ReadProtected;
        // The access size from the load or store instruction itself.
        unsigned size = 1u << (instruction >> 30);
        if (((instruction >> 26) & 1) && ((instruction >> 23) & 1) && size == 1)
            size = 16;
        if (fault.hook && fault.hook(*this, kind, guest, size, fault.context))
            return true;
        static const unsigned errors[] = {0, CpuError::ReadUnmapped, CpuError::WriteUnmapped, CpuError::FetchUnmapped,
                                          CpuError::ReadProtected, CpuError::WriteProtected, CpuError::FetchProtected};
        fault_error = errors[kind];
        auto *frame = thread->CurrentFrame;
        frame->State.rip = ctx->RestoreRIPFromHostPC(thread, pc);
        // The guard of the dispatcher spills the guest registers from where
        // they are and returns from ExecuteThread (EnableExitOnHLT).
        uc->uc_mcontext->__ss.__pc = frame->Pointers.GuestSignal_SIGSEGV;
        return true;
    }
};

void SignalHandler(int signal, siginfo_t *info, void *context)
{
    auto *uc = static_cast<ucontext_t *>(context);
    const uint64_t address = reinterpret_cast<uint64_t>(info->si_addr);
    for (auto &slot : live)
    {
        FexCpu *cpu = slot.load(std::memory_order_acquire);
        if (!cpu || !cpu->reservation)
            continue;
        // The call-return stack ran over one of its ends, which guest code that
        // switches stacks does all the time: start it over in the middle.
        if (cpu->callret && address - reinterpret_cast<uint64_t>(cpu->callret) < cpu->callret_bytes)
        {
            uc->uc_mcontext->__ss.__x[25] = cpu->callret_default;
            return;
        }
        const uint64_t low = reinterpret_cast<uint64_t>(cpu->reservation);
        if (address - low >= ReservationBytes)
            continue;
        if (cpu->OnFault(address, uc))
            return;
        break;
    }
    if (Debug())
        fprintf(stderr, "FEXCPU unhandled signal %d address=%llx pc=%llx\n", signal, (unsigned long long)address,
                (unsigned long long)uc->uc_mcontext->__ss.__pc);
    Chain(signal, info, context);
}

unsigned CreateFexCpu(Cpu *&cpu)
{
    auto *made = new FexCpu;
    const unsigned error = made->Open();
    if (error)
    {
        delete made;
        return error;
    }
    cpu = made;
    return CpuError::Ok;
}

struct Registration
{
    Registration() { RegisterCpuBackend(CpuBackend::Fex, CreateFexCpu); }
} registration;
} // namespace
} // namespace X86
