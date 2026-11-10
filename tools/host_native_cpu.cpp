// SPDX-License-Identifier: GPL-2.0-or-later
// Native 32-/16-bit execution on Linux x86_64, directly in the host process
// or in a ptrace-controlled process for comparison and sanitizer builds.
#include "../src/x86_cpu.h"
#include "../src/linux_shared_memory.h"
#include "direct_bridge.h"
#include "../src/render_profile.h"
#if !defined(__linux__) || !defined(__x86_64__)
#error The native CPU requires Linux x86_64
#endif
#include <asm/ldt.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <new>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ucontext.h>
#include <vector>
#include <cstddef>
#include <memory>
#include <mutex>
#include <sys/auxv.h>
#include <cpuid.h>
#include <asm/hwcap2.h>
#include <limits>
#include <dlfcn.h>
#include <spawn.h>

extern "C" void neo98_native_syscall();
extern "C" void neo98_native_fault();
asm(".text\n.global neo98_native_syscall\nneo98_native_syscall:\nsyscall\nint3\n");
asm(".text\n.global neo98_native_fault\nneo98_native_fault:\nint3\nud2\n");

// While guest RDTSC is trapped, glibc's vDSO clock would trap as well. Host
// clock calls use the kernel in that interval, with no guest-process stop.
thread_local bool neo98_direct_clock_trap = false;
// Guest code traps on RDTSC and, where the processor can (CPUID faulting),
// on CPUID: the guest sees the clock of the machine and a processor of its
// time (NativeCpu::CpuidInstruction), host code the real ones.
constexpr int ArchSetCpuid = 0x1012;
inline int GuestTraps(bool on)
{
    if (!on)
    {
        syscall(SYS_arch_prctl, ArchSetCpuid, 1);
        return prctl(PR_SET_TSC, PR_TSC_ENABLE);
    }
    if (prctl(PR_SET_TSC, PR_TSC_SIGSEGV)) return -1;
    syscall(SYS_arch_prctl, ArchSetCpuid, 0);
    return 0;
}
thread_local unsigned char *neo98_direct_dispatch_selector = nullptr;
extern "C" int clock_gettime(clockid_t clock, timespec *out) noexcept
{
    if (neo98_direct_clock_trap) return int(syscall(SYS_clock_gettime, clock, out));
    using Clock = int (*)(clockid_t, timespec *);
    static const auto next = reinterpret_cast<Clock>(dlsym(RTLD_NEXT, "clock_gettime"));
    return next ? next(clock, out) : int(syscall(SYS_clock_gettime, clock, out));
}

namespace
{
// Host programs must not inherit the guest thread's TSC policy. glibc's
// posix_spawn uses a new task that can retain PR_SET_TSC across exec.
class NativeHostSpawn
{
    bool clock = neo98_direct_clock_trap;
    unsigned char *selector = neo98_direct_dispatch_selector;
    unsigned char previous = selector ? *selector : 0;
  public:
    NativeHostSpawn()
    {
        const int saved = errno;
        if (clock) { GuestTraps(false); neo98_direct_clock_trap = false; }
        if (selector) *selector = SYSCALL_DISPATCH_FILTER_ALLOW;
        errno = saved;
    }
    ~NativeHostSpawn()
    {
        const int saved = errno;
        if (selector) *selector = previous;
        if (clock) { neo98_direct_clock_trap = true; GuestTraps(true); }
        errno = saved;
    }
};
}
extern "C" int posix_spawn(pid_t *pid, const char *path, const posix_spawn_file_actions_t *actions,
    const posix_spawnattr_t *attributes, char *const arguments[], char *const environment[])
{
    using Spawn = int (*)(pid_t *, const char *, const posix_spawn_file_actions_t *, const posix_spawnattr_t *, char *const [], char *const []);
    static const auto next = reinterpret_cast<Spawn>(dlsym(RTLD_NEXT, "posix_spawn"));
    NativeHostSpawn native;
    return next ? next(pid, path, actions, attributes, arguments, environment) : ENOSYS;
}
// A sanitized runner has its own pthread_create, which this one replaces:
// without it the sanitizer would not know the thread, and its
// pthread_detach and pthread_join stop the process on a thread it never saw.
extern "C" int __interceptor_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *)
    __attribute__((weak));
extern "C" int pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
    void *(*start)(void *), void *argument) noexcept
{
    using Create = int (*)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
    static const auto next = __interceptor_pthread_create ? __interceptor_pthread_create
                                                          : reinterpret_cast<Create>(dlsym(RTLD_NEXT, "pthread_create"));
    // clone inherits PR_SET_TSC, but the new thread has no direct-CPU TLS.
    // PulseAudio and other host workers must start with their own clock.
    NativeHostSpawn native;
    return next ? next(thread, attributes, start, argument) : ENOSYS;
}
extern "C" int posix_spawnp(pid_t *pid, const char *path, const posix_spawn_file_actions_t *actions,
    const posix_spawnattr_t *attributes, char *const arguments[], char *const environment[])
{
    using Spawn = int (*)(pid_t *, const char *, const posix_spawn_file_actions_t *, const posix_spawnattr_t *, char *const [], char *const []);
    static const auto next = reinterpret_cast<Spawn>(dlsym(RTLD_NEXT, "posix_spawnp"));
    NativeHostSpawn native;
    return next ? next(pid, path, actions, attributes, arguments, environment) : ENOSYS;
}
extern "C" int system(const char *command)
{
    using System = int (*)(const char *);
    static const auto next = reinterpret_cast<System>(dlsym(RTLD_NEXT, "system"));
    NativeHostSpawn native;
    return next ? next(command) : -1;
}
extern "C" FILE *popen(const char *command, const char *mode)
{
    using Popen = FILE *(*)(const char *, const char *);
    static const auto next = reinterpret_cast<Popen>(dlsym(RTLD_NEXT, "popen"));
    NativeHostSpawn native;
    return next ? next(command, mode) : nullptr;
}

namespace X86
{
namespace
{
constexpr unsigned Page = 4096, Pages = 1u << 20, Mapped = 8;
constexpr size_t AddressBytes = 1ull << 32;
constexpr unsigned SignalBytes = 65536;
struct NativeContext
{
    unsigned regs[Reg::Count] = {};
    user_fpregs_struct fp = {};
    DescriptorTable gdtr, ldtr;
};
class NativeCpu : public Cpu
{
  protected:
    pid_t child = -1;
    int fd = -1;
    unsigned char *bytes = nullptr, *scratch = nullptr, *signal_stack = nullptr;
    std::vector<unsigned char> rights{std::vector<unsigned char>(Pages)};
    std::vector<unsigned char> readable{std::vector<unsigned char>(Pages)}, writable{std::vector<unsigned char>(Pages)};
    struct Shared { unsigned address, size; unsigned char *host; };
    std::vector<Shared> shared;
    std::vector<Shared> state_shared;
    unsigned long long descriptors[8192] = {};
    bool descriptor_valid[8192] = {};
    unsigned long long ldt_snapshot[4096] = {};
    unsigned ldt_snapshot_base = 0, ldt_snapshot_count = 0;
    NativeContext state;
    unsigned long long resumes = 0, single_steps = 0, injected_syscalls = 0, mirror_bytes = 0;
    user_regs_struct bootstrap = {};
    std::atomic<bool> requested{false};
    std::atomic<bool> executing{false};
    struct Frame { Frame *previous; bool stopped = false; };
    Frame *frame = nullptr;
    unsigned gate_begin = 0, gate_end = 0;
    GateHook gate = nullptr;
    bool execute_gate_code = false;
    struct InstructionTrace { unsigned address; TraceHook hook; void *context; };
    std::vector<InstructionTrace> instruction_traces;
    FaultHook fault = nullptr;
    InterruptHook interrupt = nullptr;
    PortInputHook port_in = nullptr;
    PortOutputHook port_out = nullptr;
    BudgetHook budget = nullptr;
    BudgetHook time_slice = nullptr;
    // GetWriteWatch: an armed page is mapped without write access until the
    // guest's first write to it, which the fault reports (WatchedWrite).
    WriteHook write_hook = nullptr;
    void *write_context = nullptr;
    std::vector<unsigned char> write_armed;
    void *gate_context = nullptr, *fault_context = nullptr, *interrupt_context = nullptr;
    void *port_context = nullptr, *budget_context = nullptr;
    void *time_context = nullptr;

    bool Wait(int &status)
    {
        pid_t result;
        do result = waitpid(child, &status, __WALL); while (result < 0 && errno == EINTR);
        if (result != child || !WIFSTOPPED(status)) return false;
        return true;
    }
    virtual long Syscall(long number, unsigned long a = 0, unsigned long b = 0, unsigned long c = 0,
                 unsigned long d = 0, unsigned long e = 0, unsigned long f = 0)
    {
        ++injected_syscalls;
        auto regs = bootstrap;
        regs.rip = reinterpret_cast<unsigned long>(neo98_native_syscall);
        regs.rax = number;
        regs.orig_rax = -1;
        regs.rdi = a; regs.rsi = b; regs.rdx = c; regs.r10 = d; regs.r8 = e; regs.r9 = f;
        int status = 0;
        if (ptrace(PTRACE_SETREGS, child, nullptr, &regs) || ptrace(PTRACE_CONT, child, nullptr, nullptr))
            return -EIO;
        // A cross-thread RequestStop can leave a SIGSTOP queued after the
        // guest has stopped. Suppress it before completing the host syscall.
        if (!Wait(status)) return -EIO;
        while (WSTOPSIG(status) == SIGSTOP || WSTOPSIG(status) == SIGVTALRM)
        {
            if (ptrace(PTRACE_CONT, child, nullptr, nullptr) || !Wait(status)) return -EIO;
        }
        if (WSTOPSIG(status) != SIGTRAP || ptrace(PTRACE_GETREGS, child, nullptr, &regs)) return -EIO;
        return static_cast<long>(regs.rax);
    }
    bool InGate(unsigned address) const { return gate && address >= gate_begin && address <= gate_end; }
    unsigned long long Descriptor(unsigned selector) const
    {
        if (!selector) return 0;
        const auto &table = selector & 4 ? state.ldtr : state.gdtr;
        unsigned long long result = 0;
        unsigned index = selector >> 3;
        if (table.base && index * 8 + 7 <= table.limit && Has(table.base + index * 8, 8))
            memcpy(&result, bytes + table.base + index * 8, 8);
        return result;
    }
    unsigned Base(unsigned selector) const
    {
        auto desc = Descriptor(selector);
        return ((desc >> 16) & 0xffffff) | ((desc >> 32) & 0xff000000);
    }
    bool Bits32(unsigned selector) const
    {
        const auto &table = selector & 4 ? state.ldtr : state.gdtr;
        return !table.base || (Descriptor(selector) & (1ull << 54));
    }
    unsigned InstructionAddress() const { return Base(state.regs[Reg::Cs]) + state.regs[Reg::Eip]; }
    // The faulting instruction of a device access, run against Read and
    // Write. These are the forms of VGA code: MOV between register or
    // immediate and memory, MOVZX/MOVSX, TEST and CMP, and MOVS, STOS and LODS
    // with or without REP. Any other instruction stops the run with CpuError::Device.
    bool DeviceInstruction()
    {
        const bool code32 = Bits32(state.regs[Reg::Cs]);
        const unsigned ip = InstructionAddress();
        unsigned char code[15] = {};
        unsigned length = 0, at = 0;
        while (length < sizeof code && !Read(ip + length, code + length, 1)) ++length;
        bool operand32 = code32, address32 = code32, rep = false;
        int segment = -1;
        for (;; ++at)
        {
            if (at >= length) return false;
            const unsigned char b = code[at];
            if (b == 0x66) operand32 = !code32;
            else if (b == 0x67) address32 = !code32;
            else if (b == 0xf3) rep = true;
            else if (b == 0x26 || b == 0x2e || b == 0x36 || b == 0x3e) segment = Reg::Es + ((b >> 3) & 3);
            else if (b == 0x64 || b == 0x65) segment = Reg::Fs + (b & 1);
            else break;
        }
        auto take = [&](unsigned bytes, unsigned &value) {
            if (at + bytes > length) return false;
            value = 0;
            for (unsigned n = 0; n < bytes; ++n) value |= unsigned(code[at++]) << (8 * n);
            return true;
        };
        // The register operand: 8-bit numbers 4 to 7 are AH, CH, DH and BH.
        auto get = [&](unsigned reg, unsigned size) -> unsigned {
            if (size == 1) return reg < 4 ? state.regs[reg] & 255 : (state.regs[reg - 4] >> 8) & 255;
            return size == 2 ? state.regs[reg] & 65535 : unsigned(state.regs[reg]);
        };
        auto set = [&](unsigned reg, unsigned size, unsigned value) {
            if (size == 4) state.regs[reg] = value;
            else if (size == 2) state.regs[reg] = (state.regs[reg] & 0xffff0000u) | (value & 65535);
            else if (reg < 4) state.regs[reg] = (state.regs[reg] & ~255u) | (value & 255);
            else state.regs[reg - 4] = (state.regs[reg - 4] & ~0xff00u) | ((value & 255) << 8);
        };
        auto memory = [&](unsigned address, unsigned size, unsigned &value, bool write) {
            if (write) return Write(address, &value, size) == CpuError::Ok;
            value = 0;
            return Read(address, &value, size) == CpuError::Ok;
        };
        // The linear address of a ModRM memory operand; false for a register.
        auto operand = [&](unsigned modrm, unsigned &address) {
            const unsigned mode = modrm >> 6, rm = modrm & 7;
            if (mode == 3) return false;
            unsigned offset = 0, displacement = 0, bytes = 0;
            int base_segment = Reg::Ds;
            if (address32)
            {
                unsigned base = rm;
                bytes = mode == 1 ? 1 : mode == 2 ? 4 : 0;
                if (rm == 4)
                {
                    unsigned sib = 0;
                    if (!take(1, sib)) return false;
                    base = sib & 7;
                    const unsigned index = (sib >> 3) & 7;
                    if (index != 4) offset += unsigned(state.regs[index]) << (sib >> 6);
                }
                if (mode == 0 && base == 5) bytes = 4;
                else
                {
                    offset += state.regs[base];
                    if (base == Reg::Esp || base == Reg::Ebp) base_segment = Reg::Ss;
                }
            }
            else
            {
                static const int first[8] = {Reg::Ebx, Reg::Ebx, Reg::Ebp, Reg::Ebp, Reg::Esi, Reg::Edi, Reg::Ebp, Reg::Ebx};
                static const int second[8] = {Reg::Esi, Reg::Edi, Reg::Esi, Reg::Edi, -1, -1, -1, -1};
                bytes = mode == 1 ? 1 : mode == 2 ? 2 : 0;
                if (mode == 0 && rm == 6) bytes = 2;
                else
                {
                    offset = state.regs[first[rm]] + (second[rm] >= 0 ? state.regs[second[rm]] : 0);
                    if (first[rm] == Reg::Ebp) base_segment = Reg::Ss;
                }
            }
            if (!take(bytes, displacement)) return false;
            if (bytes == 1) displacement = unsigned(int(static_cast<signed char>(displacement)));
            offset += displacement;
            if (!address32) offset &= 65535;
            address = Base(state.regs[segment >= 0 ? segment : base_segment]) + offset;
            return true;
        };
        unsigned op = code[at++], value = 0, address = 0;
        const unsigned wide = operand32 ? 4 : 2;
        if (op == 0x88 || op == 0x89 || op == 0x8a || op == 0x8b || op == 0xc6 || op == 0xc7)
        {
            unsigned modrm = 0;
            if (!take(1, modrm) || !operand(modrm, address)) return false;
            const unsigned size = op & 1 ? wide : 1, reg = (modrm >> 3) & 7;
            if (op >= 0xc6)
            {
                if (reg || !take(size, value) || !memory(address, size, value, true)) return false;
            }
            else if (op & 2)
            {
                if (!memory(address, size, value, false)) return false;
                set(reg, size, value);
            }
            else if (value = get(reg, size), !memory(address, size, value, true)) return false;
        }
        else if (op == 0x84 || op == 0x85 || (op >= 0x38 && op <= 0x3b) || op == 0xf6 || op == 0xf7 ||
                 op == 0x80 || op == 0x81 || op == 0x83)
        {
            // TEST and CMP read the memory operand and leave only the flags:
            // the way a program probes for the VGA window before it draws.
            unsigned modrm = 0, other = 0;
            if (!take(1, modrm) || !operand(modrm, address)) return false;
            const unsigned digit = (modrm >> 3) & 7, size = op & 1 ? wide : 1;
            const bool test = op == 0x84 || op == 0x85 || op >= 0xf6;
            if (op >= 0x80)
            {
                if (digit != (test ? 0u : 7u) || !take(op == 0x83 ? 1 : size, other)) return false;
                if (op == 0x83) other = unsigned(int(static_cast<signed char>(other)));
            }
            else other = get(digit, size);
            if (!memory(address, size, value, false)) return false;
            const unsigned sign = 1u << (8 * size - 1), range = sign | (sign - 1);
            other &= range;
            const bool reversed = op == 0x3a || op == 0x3b;
            const unsigned left = reversed ? other : value, right = reversed ? value : other;
            unsigned result = left & right, flags = 0;
            if (!test)
            {
                result = (left - right) & range;
                if (left < right) flags |= 1;
                if ((left ^ right ^ result) & 0x10) flags |= 0x10;
                if ((left ^ right) & (left ^ result) & sign) flags |= 0x800;
            }
            if (!result) flags |= 0x40;
            if (result & sign) flags |= 0x80;
            if (!(__builtin_popcount(result & 255) & 1)) flags |= 4;
            state.regs[Reg::Eflags] = (state.regs[Reg::Eflags] & ~0x8d5u) | flags;
        }
        else if (op == 0x0f)
        {
            unsigned second = 0, modrm = 0;
            if (!take(1, second) || (second != 0xb6 && second != 0xb7 && second != 0xbe && second != 0xbf) ||
                !take(1, modrm) || !operand(modrm, address))
                return false;
            const unsigned size = second & 1 ? 2 : 1;
            if (!memory(address, size, value, false)) return false;
            if (second & 8) value = size == 1 ? unsigned(int(static_cast<signed char>(value))) : unsigned(int(static_cast<short>(value)));
            set((modrm >> 3) & 7, wide, value);
        }
        else if (op >= 0xa0 && op <= 0xa3)
        {
            unsigned offset = 0;
            if (!take(address32 ? 4 : 2, offset)) return false;
            address = Base(state.regs[segment >= 0 ? segment : Reg::Ds]) + offset;
            const unsigned size = op & 1 ? wide : 1;
            if (op & 2)
            {
                value = get(Reg::Eax, size);
                if (!memory(address, size, value, true)) return false;
            }
            else
            {
                if (!memory(address, size, value, false)) return false;
                set(Reg::Eax, size, value);
            }
        }
        else if (op == 0xa4 || op == 0xa5 || op == 0xaa || op == 0xab || op == 0xac || op == 0xad)
        {
            const unsigned size = op & 1 ? wide : 1, mask = address32 ? ~0u : 65535u;
            const unsigned step = (state.regs[Reg::Eflags] & 0x400) ? -size : size;
            const unsigned source = Base(state.regs[segment >= 0 ? segment : Reg::Ds]), target = Base(state.regs[Reg::Es]);
            auto advance = [&](int reg) { state.regs[reg] = (state.regs[reg] & ~mask) | ((state.regs[reg] + step) & mask); };
            unsigned count = rep ? state.regs[Reg::Ecx] & mask : 1;
            for (; count; --count)
            {
                if (op != 0xaa && op != 0xab)
                {
                    if (!memory(source + (state.regs[Reg::Esi] & mask), size, value, false)) return false;
                    if (op >= 0xac) set(Reg::Eax, size, value);
                }
                else value = get(Reg::Eax, size);
                if (op < 0xac && !memory(target + (state.regs[Reg::Edi] & mask), size, value, true)) return false;
                if (op != 0xaa && op != 0xab) advance(Reg::Esi);
                if (op < 0xac) advance(Reg::Edi);
                if (rep) state.regs[Reg::Ecx] = (state.regs[Reg::Ecx] & ~mask) | ((count - 1) & mask);
            }
        }
        else
            return false;
        const unsigned eip = state.regs[Reg::Eip] + at;
        state.regs[Reg::Eip] = code32 ? eip : eip & 65535;
        return true;
    }
    // Name the instruction the engine stops at, for the log and the crash
    // report: the bytes say which form is missing.
    void ReportInstruction(const char *what)
    {
        unsigned char code[15] = {};
        unsigned length = 0;
        const unsigned ip = InstructionAddress();
        while (length < sizeof code && !Read(ip + length, code + length, 1)) ++length;
        char text[3 * sizeof code + 1] = {};
        for (unsigned i = 0; i < length; ++i) snprintf(text + 3 * i, 4, " %02x", code[i]);
        fprintf(stderr, "CPU native %s at %08x:%s\n", what, ip, text);
    }
    // CPUID as a program of Win98's time expects it: the features of this
    // processor, but at most leaf 2 and extended leaf 0x80000008. Programs of
    // that time read the highest leaf as a small number; one of them takes
    // only its low four bits and finds no CPUID on a processor that reports
    // 0x10 or 0x20.
    bool CpuidInstruction()
    {
        unsigned char op[2] = {};
        if (Read(InstructionAddress(), op, 2) || op[0] != 0x0f || op[1] != 0xa2) return false;
        const unsigned leaf = state.regs[Reg::Eax], sub = state.regs[Reg::Ecx];
        unsigned top = 0, extended = 0, a = 0, b = 0, c = 0, d = 0;
        __cpuid_count(0, 0, top, b, c, d);
        __cpuid_count(0x80000000u, 0, extended, b, c, d);
        top = std::min(top, 2u);
        extended = std::min(extended, 0x80000008u);
        a = b = c = d = 0;
        if (leaf <= top || (leaf >= 0x80000000u && leaf <= extended))
            __cpuid_count(leaf, sub, a, b, c, d);
        if (leaf == 0) a = top;
        else if (leaf == 0x80000000u) a = extended;
        state.regs[Reg::Eax] = a; state.regs[Reg::Ebx] = b; state.regs[Reg::Ecx] = c; state.regs[Reg::Edx] = d;
        state.regs[Reg::Eip] += 2;
        return true;
    }
    bool ClockInstruction()
    {
        unsigned char op[3] = {};
        if (Read(InstructionAddress(), op, 3) || op[0] != 0x0f) return false;
        const bool rdtscp = op[1] == 1 && op[2] == 0xf9;
        if (op[1] != 0x31 && !rdtscp) return false;
        // Use nanoseconds, as the existing Unicorn host does. Machine pauses
        // are excluded from this counter as well as the Win98 clock APIs.
        timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        const auto ticks = static_cast<unsigned long long>(now.tv_sec) * 1000000000 + now.tv_nsec - CounterPause() * 1000;
        state.regs[Reg::Eax] = ticks; state.regs[Reg::Edx] = ticks >> 32;
        if (rdtscp) state.regs[Reg::Ecx] = 0;
        state.regs[Reg::Eip] += rdtscp ? 3 : 2;
        return true;
    }
    int Protection(unsigned address, unsigned prot, bool allow_gate = false) const
    {
        int result = prot & 7;
        if (write_hook && (result & PROT_WRITE) && write_armed[address / Page]) result &= ~PROT_WRITE;
        // Gate pages trap before executing any Windows API entry.
        if (!allow_gate && !execute_gate_code && gate && address <= gate_end &&
            static_cast<unsigned long long>(address) + Page > gate_begin)
            result &= ~PROT_EXEC;
        return result;
    }
    unsigned Range(unsigned address, unsigned size, int occupied) const
    {
        if (!size || (address % Page) || (size % Page) ||
            static_cast<unsigned long long>(address) + size > AddressBytes)
            return CpuError::Argument;
        for (unsigned long long a = address; a < static_cast<unsigned long long>(address) + size; a += Page)
            if (bool(rights[a / Page] & Mapped) != bool(occupied))
                return occupied ? CpuError::NoMemory : CpuError::Map;
        return CpuError::Ok;
    }
    // The device range of MapDevice stays unmapped in the guest, so every
    // guest access there faults; DeviceInstruction then performs it through
    // Read and Write, which reach the device.
    DeviceAccess device_access = nullptr;
    void *device_context = nullptr;
    unsigned device_base = 0, device_size = 0;
    bool InDevice(unsigned long long address) const
    { return device_access && address >= device_base && address < static_cast<unsigned long long>(device_base) + device_size; }
    bool WithinDevice(unsigned address, unsigned size) const
    { return size && InDevice(address) && InDevice(static_cast<unsigned long long>(address) + size - 1); }
    bool Has(unsigned address, unsigned size) const
    {
        if (static_cast<unsigned long long>(address) + size > AddressBytes) return false;
        for (unsigned long long a = address; a < static_cast<unsigned long long>(address) + size;)
        {
            if (!(rights[a / Page] & Mapped)) return false;
            a = (a / Page + 1) * Page;
        }
        return true;
    }
    void Access(unsigned page)
    {
        readable[page] = !!(rights[page] & Prot::Read); writable[page] = !!(rights[page] & Prot::Write);
        for (const auto &s : shared)
            if (page >= s.address / Page && page < (static_cast<unsigned long long>(s.address) + s.size) / Page)
            { readable[page] = writable[page] = 0; break; }
    }
    void Sync(bool to_child)
    {
        for (const auto &s : shared)
        {
            mirror_bytes += s.size;
            if (to_child) memcpy(bytes + s.address, s.host, s.size);
            else memcpy(s.host, bytes + s.address, s.size);
        }
    }
    unsigned Selector(unsigned value, bool code)
    {
        if (!value) return code ? 0x23 : 0;
        const auto &table = (value & 4) ? state.ldtr : state.gdtr;
        const unsigned index = value >> 3;
        unsigned long long desc = 0;
        if (!table.base && !(value & 4)) return code ? 0x23 : 0x2b;
        if (index * 8 + 7 > table.limit || Read(table.base + index * 8, &desc, 8)) return ~0u;
        // Reserve the upper half of the Linux LDT for guest GDT descriptors.
        const unsigned slot = index + ((value & 4) ? 0 : 4096);
        if (slot >= 8192 || ((value & 4) && index >= 4096) || !(desc & (1ull << 47))) return ~0u;
        if (descriptor_valid[slot] && descriptors[slot] == desc) return slot * 8 + 7;
        user_desc native = {};
        native.entry_number = slot;
        native.base_addr = ((desc >> 16) & 0xffffff) | ((desc >> 32) & 0xff000000);
        native.limit = (desc & 0xffff) | ((desc >> 32) & 0xf0000);
        native.seg_32bit = (desc >> 54) & 1;
        native.contents = (desc & (1ull << 43)) ? 2 : ((desc >> 42) & 1);
        native.read_exec_only = !(desc & (1ull << 41));
        native.limit_in_pages = (desc >> 55) & 1;
        native.useable = 1;
        memcpy(scratch, &native, sizeof native);
        // The scratch mapping was inherited from the parent before fork.
        if (Syscall(SYS_modify_ldt, 0x11, reinterpret_cast<unsigned long>(scratch), sizeof native)) return ~0u;
        descriptors[slot] = desc; descriptor_valid[slot] = true;
        return slot * 8 + 7;
    }
    unsigned SyncLdt()
    {
        if (!state.ldtr.base) return CpuError::Ok;
        const unsigned count = std::min(4096u, (state.ldtr.limit + 1) / 8);
        const auto *table = HostPointer(state.ldtr.base, count * 8);
        if (table && ldt_snapshot_base == state.ldtr.base && ldt_snapshot_count == count &&
            !memcmp(ldt_snapshot, table, count * 8)) return CpuError::Ok;
        for (unsigned index = 0; index < count; ++index)
        {
            const unsigned selector = index * 8 + 7;
            auto desc = Descriptor(selector);
            if (desc & (1ull << 47))
            {
                if (Selector(selector, desc & (1ull << 43)) == ~0u) return CpuError::Mode;
            }
            else if (descriptor_valid[index])
            {
                user_desc empty = {}; empty.entry_number = index; empty.read_exec_only = empty.seg_not_present = 1;
                memcpy(scratch, &empty, sizeof empty);
                if (Syscall(SYS_modify_ldt, 0x11, reinterpret_cast<unsigned long>(scratch), sizeof empty)) return CpuError::Mode;
                descriptor_valid[index] = false;
            }
        }
        if (table)
        {
            memcpy(ldt_snapshot, table, count * 8);
            ldt_snapshot_base = state.ldtr.base; ldt_snapshot_count = count;
        }
        else ldt_snapshot_base = 0;
        return CpuError::Ok;
    }
    unsigned PrepareRegisters(user_regs_struct &r)
    {
        r = bootstrap;
        r.rax = state.regs[Reg::Eax]; r.rcx = state.regs[Reg::Ecx]; r.rdx = state.regs[Reg::Edx];
        r.rbx = state.regs[Reg::Ebx]; r.rsp = state.regs[Reg::Esp]; r.rbp = state.regs[Reg::Ebp];
        r.rsi = state.regs[Reg::Esi]; r.rdi = state.regs[Reg::Edi]; r.rip = state.regs[Reg::Eip];
        r.eflags = (state.regs[Reg::Eflags] & 0x240dd5) | 0x202;
        if (!instruction_traces.empty()) r.eflags |= state.regs[Reg::Eflags] & 0x10000;
        r.orig_rax = -1;
        r.cs = Selector(state.regs[Reg::Cs], true);
        r.ss = Selector(state.regs[Reg::Ss], false);
        r.ds = Selector(state.regs[Reg::Ds], false);
        r.es = Selector(state.regs[Reg::Es], false);
        r.fs = Selector(state.regs[Reg::Fs], false);
        r.gs = Selector(state.regs[Reg::Gs], false);
        auto segment_base = [&](unsigned selector) -> unsigned long long {
            if (!(selector & 4) || (selector >> 3) >= 8192) return 0;
            const auto desc = descriptors[selector >> 3];
            return ((desc >> 16) & 0xffffff) | ((desc >> 32) & 0xff000000);
        };
        r.fs_base = segment_base(r.fs); r.gs_base = segment_base(r.gs);
        if (r.cs == ~0u || r.ss == ~0u || r.ds == ~0u || r.es == ~0u || r.fs == ~0u || r.gs == ~0u)
            return CpuError::Exception;
        if (!r.ss) r.ss = 0x2b;
        return CpuError::Ok;
    }
    unsigned PushRegisters()
    {
        user_regs_struct r;
        if (unsigned error = PrepareRegisters(r)) return error;
        return ptrace(PTRACE_SETREGS, child, nullptr, &r) ||
               ptrace(PTRACE_SETFPREGS, child, nullptr, &state.fp) ? CpuError::Resource : CpuError::Ok;
    }
    bool PullRegisters()
    {
        user_regs_struct r;
        if (ptrace(PTRACE_GETREGS, child, nullptr, &r) || ptrace(PTRACE_GETFPREGS, child, nullptr, &state.fp)) return false;
        ApplyRegisters(r);
        return true;
    }
    void ApplyRegisters(const user_regs_struct &r)
    {
        const unsigned long long values[] = {r.rax, r.rcx, r.rdx, r.rbx, r.rsp, r.rbp, r.rsi, r.rdi, r.rip, r.eflags};
        for (unsigned i = 0; i < 10; ++i) state.regs[i] = values[i];
        const unsigned long long segments[] = {r.es, r.cs, r.ss, r.ds, r.fs, r.gs};
        for (unsigned i = 0; i < 6; ++i)
        {
            unsigned selector = segments[i], slot = selector >> 3;
            if (selector & 4)
                state.regs[Reg::Es + i] = slot >= 4096 ? (slot - 4096) * 8 | (state.regs[Reg::Es + i] & 3)
                                                     : slot * 8 | 4 | (state.regs[Reg::Es + i] & 3);
            else if (!selector) state.regs[Reg::Es + i] = 0;
        }
    }
    // Deliver a hardware fault to a minimal 64-bit signal trampoline. Linux
    // puts the actual x86 trap and page-fault error in its ucontext. The
    // trampoline stops before calling libc or using the guest FS value.
    bool HardwareFault(unsigned &trap, unsigned &error)
    {
        int status = 0;
        user_regs_struct r;
        if (ptrace(PTRACE_CONT, child, nullptr, SIGSEGV) || !Wait(status) || WSTOPSIG(status) != SIGTRAP ||
            ptrace(PTRACE_GETREGS, child, nullptr, &r)) return false;
        const auto base = reinterpret_cast<uintptr_t>(signal_stack);
        if (r.rdx < base || r.rdx - base > SignalBytes - sizeof(ucontext_t)) return false;
        auto *context = reinterpret_cast<const ucontext_t *>(r.rdx);
        trap = context->uc_mcontext.gregs[REG_TRAPNO];
        error = context->uc_mcontext.gregs[REG_ERR];
        return true;
    }
    // Segment values in Win98's GDT differ from Linux's selectors. These
    // instructions need service even though arithmetic and memory code run
    // unchanged. The first backend supports register operands only.
    bool SegmentInstruction()
    {
        const unsigned ip = InstructionAddress();
        unsigned char op[3] = {};
        if (Read(ip, op, 2)) return false;
        unsigned prefix = op[0] == 0x66;
        const bool wide = Bits32(state.regs[Reg::Cs]) != bool(prefix);
        if (prefix && Read(ip + 1, op, 2)) return false;
        if ((op[0] != 0x8e && op[0] != 0x8c) || (op[1] & 0xc0) != 0xc0) return false;
        const unsigned seg = (op[1] >> 3) & 7, reg = op[1] & 7;
        if (seg > 5 || (op[0] == 0x8e && seg == 1)) return false;
        // A selector without a present descriptor faults at the load, as on
        // the processor: the general protection fault goes to the interrupt
        // handler with the load as its instruction. Taken as it is, the
        // register would make the next entry fail instead.
        if (op[0] == 0x8e && (state.regs[reg] & 0xfffc) &&
            Selector(state.regs[reg] & 0xffff, false) == ~0u) return false;
        if (op[0] == 0x8e) state.regs[Reg::Es + seg] = state.regs[reg] & 0xffff;
        else state.regs[reg] = !wide ? (state.regs[reg] & 0xffff0000) | state.regs[Reg::Es + seg]
                                    : state.regs[Reg::Es + seg];
        state.regs[Reg::Eip] += 2 + prefix;
        return true;
    }
    bool PortInstruction()
    {
        unsigned char op[2];
        const unsigned ip = InstructionAddress();
        if (Read(ip, op, 2)) return false;
        const unsigned prefix = op[0] == 0x66;
        if (prefix && Read(ip + 1, op, 2)) return false;
        if (op[0] < 0xe4 || op[0] > 0xef || (op[0] >= 0xe8 && op[0] <= 0xeb)) return false;
        bool output = op[0] & 2, immediate = op[0] < 0xe8;
        const bool wide = Bits32(state.regs[Reg::Cs]) != bool(prefix);
        unsigned size = (op[0] & 1) ? (wide ? 4 : 2) : 1, mask = size == 1 ? 255 : size == 2 ? 65535 : ~0u;
        unsigned port = immediate ? op[1] : state.regs[Reg::Edx] & 0xffff;
        if (output && port_out) port_out(*this, port, size, state.regs[Reg::Eax] & mask, port_context);
        else if (!output && port_in)
            state.regs[Reg::Eax] = (state.regs[Reg::Eax] & ~mask) | (port_in(*this, port, size, port_context) & mask);
        else return false;
        state.regs[Reg::Eip] += (immediate ? 2 : 1) + prefix;
        return true;
    }
    // UMIP prevents legacy ring-3 reads of CPU status on current Linux.
    // Windows 98 DLLs use SMSW to check that the FPU is enabled.
    bool SystemStatusInstruction()
    {
        const unsigned ip = InstructionAddress();
        unsigned char op[15] = {};
        unsigned length = 0, prefix = 0, segment = Reg::Ds;
        bool segment_override = false;
        auto byte = [&](unsigned &value) {
            if (length == sizeof op || Read(ip + length, op + length, 1)) return false;
            value = op[length++]; return true;
        };
        unsigned first;
        if (!byte(first)) return false;
        while (first == 0x66 || first == 0x64 || first == 0x65 || first == 0x36 || first == 0x3e)
        {
            if (first == 0x66) prefix = 1;
            else { segment_override = true; segment = first == 0x64 ? Reg::Fs : first == 0x65 ? Reg::Gs : first == 0x36 ? Reg::Ss : Reg::Ds; }
            if (!byte(first)) return false;
        }
        unsigned second, modrm;
        if (first != 0x0f || !byte(second) || second != 0x01 || !byte(modrm) || ((modrm >> 3) & 7) != 4 || !Bits32(state.regs[Reg::Cs])) return false;
        const unsigned value = state.regs[Reg::Cr0] & 0xffff, mode = modrm >> 6, rm = modrm & 7;
        if (mode == 3)
            state.regs[rm] = prefix ? (state.regs[rm] & 0xffff0000) | value : value;
        else
        {
            unsigned address = 0, base = rm, displacement = 0, disp_bytes = mode == 1 ? 1 : mode == 2 ? 4 : 0;
            if (rm == 4)
            {
                unsigned sib; if (!byte(sib)) return false;
                base = sib & 7; unsigned index = (sib >> 3) & 7;
                if (index != 4) address += state.regs[index] << (sib >> 6);
            }
            if (mode == 0 && base == 5) disp_bytes = 4;
            else { address += state.regs[base]; if (!segment_override && (base == Reg::Ebp || base == Reg::Esp)) segment = Reg::Ss; }
            for (unsigned n = 0; n < disp_bytes; ++n) { unsigned b; if (!byte(b)) return false; displacement |= b << (8 * n); }
            if (disp_bytes == 1) displacement = static_cast<unsigned>(static_cast<int>(static_cast<signed char>(displacement)));
            address += displacement + Base(state.regs[segment]);
            for (unsigned n = 0; n < 2; ++n)
            {
                const unsigned p = (address + n) / Page;
                if (!(rights[p] & Mapped) || !(rights[p] & Prot::Write))
                {
                    unsigned kind = (rights[p] & Mapped) ? CpuFault::WriteProtected : CpuFault::WriteUnmapped;
                    if (!fault || !fault(*this, kind, address + n, 1, fault_context)) return false;
                }
            }
            const unsigned short word = static_cast<unsigned short>(value);
            if (Write(address, &word, 2)) return false;
        }
        state.regs[Reg::Eip] += length; return true;
    }
    // The port models flat Win32 setup at ring zero, then enters Win16 with
    // a RETF that also switches SS:SP. Linux keeps both at hardware ring
    // three, so this one transition needs an explicit register change.
    bool EnterWin16()
    {
        if ((state.regs[Reg::Cs] & 3) || !Bits32(state.regs[Reg::Cs])) return false;
        unsigned char opcode;
        if (Read(InstructionAddress(), &opcode, 1) || opcode != 0xcb) return false;
        unsigned target[4];
        if (Read(Base(state.regs[Reg::Ss]) + state.regs[Reg::Esp], target, sizeof target) || (target[1] & 7) != 7) return false;
        state.regs[Reg::Eip] = target[0]; state.regs[Reg::Cs] = target[1] & 0xffff;
        state.regs[Reg::Esp] = target[2]; state.regs[Reg::Ss] = target[3] & 0xffff;
        return true;
    }
    unsigned FullTag() const
    {
        unsigned tag = 0, top = (state.fp.swd >> 11) & 7;
        for (unsigned physical = 0; physical < 8; ++physical)
        {
            unsigned kind = 3;
            if (state.fp.ftw & (1u << physical))
            {
                const auto *data = reinterpret_cast<const unsigned char *>(state.fp.st_space) + ((physical - top) & 7) * 16;
                uint64_t mantissa; uint16_t exponent;
                memcpy(&mantissa, data, 8); memcpy(&exponent, data + 8, 2);
                exponent &= 0x7fff;
                kind = !exponent && !mantissa ? 1 : exponent == 0x7fff || !(mantissa >> 63) ? 2 : 0;
            }
            tag |= kind << (physical * 2);
        }
        return tag;
    }
  public:
    ~NativeCpu() override
    {
        if (child > 0 && getenv("NEO98_NATIVE_STATS"))
            fprintf(stderr, "NATIVE STATS pid=%ld resumes=%llu single_steps=%llu syscalls=%llu mirror_bytes=%llu\n",
                    long(child), resumes, single_steps, injected_syscalls, mirror_bytes);
        if (child > 0) { kill(child, SIGKILL); int status; while (waitpid(child, &status, __WALL) < 0 && errno == EINTR) {} }
        if (bytes) munmap(bytes, AddressBytes);
        if (scratch) munmap(scratch, Page);
        if (signal_stack) munmap(signal_stack, SignalBytes);
        if (fd >= 0) close(fd);
    }
    unsigned Open()
    {
        fd = syscall(SYS_memfd_create, "neo98-native", 0);
        if (fd < 0 || ftruncate(fd, AddressBytes)) return CpuError::NoMemory;
        void *mapped = mmap(nullptr, AddressBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mapped == MAP_FAILED) return CpuError::NoMemory;
        bytes = static_cast<unsigned char *>(mapped);
        mapped = mmap(nullptr, Page, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (mapped == MAP_FAILED) return CpuError::NoMemory;
        scratch = static_cast<unsigned char *>(mapped);
        mapped = mmap(nullptr, SignalBytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (mapped == MAP_FAILED) return CpuError::NoMemory;
        signal_stack = static_cast<unsigned char *>(mapped);
        child = fork();
        if (child < 0) return CpuError::Resource;
        if (!child)
        {
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            stack_t stack = {}; stack.ss_sp = signal_stack; stack.ss_size = SignalBytes;
            struct sigaction action = {};
            action.sa_handler = reinterpret_cast<void (*)(int)>(neo98_native_fault);
            action.sa_flags = SA_ONSTACK | SA_SIGINFO | SA_NODEFER;
            if (sigaltstack(&stack, nullptr) || sigaction(SIGSEGV, &action, nullptr)) _exit(124);
            if (GuestTraps(true)) _exit(124);
            if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr)) _exit(125);
            raise(SIGSTOP);
            _exit(126);
        }
        int status = 0;
        if (!Wait(status) || ptrace(PTRACE_GETREGS, child, nullptr, &bootstrap) ||
            ptrace(PTRACE_SETOPTIONS, child, nullptr, PTRACE_O_EXITKILL | PTRACE_O_TRACESYSGOOD)) return CpuError::Resource;
        // The stopped child executes only the high-address syscall/signal
        // stubs from now on. Clear inherited low mappings, including ASan's
        // shadow gap, before installing its independent Win98 address space.
        if (reinterpret_cast<uintptr_t>(neo98_native_syscall) < AddressBytes ||
            reinterpret_cast<uintptr_t>(neo98_native_fault) < AddressBytes) return CpuError::Arch;
        if (Syscall(SYS_munmap, 0x10000, AddressBytes - 0x10000)) return CpuError::Map;
        state.regs[Reg::Cs] = 8;
        state.regs[Reg::Ss] = state.regs[Reg::Ds] = state.regs[Reg::Es] = 16;
        state.regs[Reg::Eflags] = 0x202;
        state.regs[Reg::Cr0] = 1;
        state.fp.cwd = 0x37f; state.fp.mxcsr = 0x1f80;
        memory = bytes; page_read = readable.data(); page_write = writable.data();
        return CpuError::Ok;
    }
    CpuBackend Backend() const override { return CpuBackend::Native; }
    bool Runs16Bit() const override { return true; }
    // A descriptor of the guest GDT lies in the upper half of the Linux LDT
    // (Selector), and the guest reads that LDT selector with MOV from a
    // segment register.
    unsigned GuestSelector(unsigned value) const override
    {
        const unsigned slot = (value & 0xffff) >> 3;
        return (value & 4) && slot >= 4096 ? (slot - 4096) * 8 | (value & 3) : value;
    }
    unsigned Get(int reg, unsigned &value) override
    {
        if (reg < 0 || reg >= Reg::Fp0) return CpuError::Argument;
        switch (reg)
        {
        case Reg::Fpcw: value = state.fp.cwd; break;
        case Reg::Fpsw: value = state.fp.swd; break;
        case Reg::Fptag: value = FullTag(); break;
        case Reg::Fip: value = state.fp.rip; break;
        case Reg::Fdp: value = state.fp.rdp; break;
        case Reg::Fop: value = state.fp.fop; break;
        case Reg::Mxcsr: value = state.fp.mxcsr; break;
        default: value = state.regs[reg];
        }
        return CpuError::Ok;
    }
    unsigned Set(int reg, unsigned value) override
    {
        if (reg < 0 || reg >= Reg::Fp0) return CpuError::Argument;
        switch (reg)
        {
        case Reg::Fpcw: state.fp.cwd = value; break;
        case Reg::Fpsw: state.fp.swd = value; break;
        case Reg::Fptag:
            state.fp.ftw = 0;
            for (unsigned i = 0; i < 8; ++i) if (((value >> (i * 2)) & 3) != 3) state.fp.ftw |= 1u << i;
            break;
        case Reg::Fip: state.fp.rip = value; break;
        case Reg::Fdp: state.fp.rdp = value; break;
        case Reg::Fop: state.fp.fop = value; break;
        case Reg::Mxcsr: state.fp.mxcsr = value; break;
        default: state.regs[reg] = value;
        }
        return CpuError::Ok;
    }
    unsigned GetWide(int reg, void *out) override
    {
        if (!out) return CpuError::Argument;
        if (reg == Reg::Gdtr || reg == Reg::Ldtr) *static_cast<DescriptorTable *>(out) = reg == Reg::Gdtr ? state.gdtr : state.ldtr;
        else if (reg >= Reg::Fp0 && reg <= Reg::Fp7) memcpy(out, state.fp.st_space + (((reg - Reg::Fp0) - ((state.fp.swd >> 11) & 7)) & 7) * 4, 16);
        else if (reg >= Reg::Xmm0 && reg <= Reg::Xmm7) memcpy(out, state.fp.xmm_space + (reg - Reg::Xmm0) * 4, 16);
        else return CpuError::Argument;
        return CpuError::Ok;
    }
    unsigned SetWide(int reg, const void *in) override
    {
        if (!in) return CpuError::Argument;
        if (reg == Reg::Gdtr || reg == Reg::Ldtr) (reg == Reg::Gdtr ? state.gdtr : state.ldtr) = *static_cast<const DescriptorTable *>(in);
        else if (reg >= Reg::Fp0 && reg <= Reg::Fp7) memcpy(state.fp.st_space + (((reg - Reg::Fp0) - ((state.fp.swd >> 11) & 7)) & 7) * 4, in, 16);
        else if (reg >= Reg::Xmm0 && reg <= Reg::Xmm7) memcpy(state.fp.xmm_space + (reg - Reg::Xmm0) * 4, in, 16);
        else return CpuError::Argument;
        return CpuError::Ok;
    }
    unsigned NewContext(CpuContext *&out) override
    {
        out = reinterpret_cast<CpuContext *>(new (std::nothrow) NativeContext);
        return out ? CpuError::Ok : CpuError::NoMemory;
    }
    void FreeContext(CpuContext *context) override { delete reinterpret_cast<NativeContext *>(context); }
    unsigned Save(CpuContext *context) override
    {
        if (!context) return CpuError::Argument;
        *reinterpret_cast<NativeContext *>(context) = state; return CpuError::Ok;
    }
    unsigned Restore(const CpuContext *context) override
    {
        if (!context) return CpuError::Argument;
        state = *reinterpret_cast<const NativeContext *>(context); return CpuError::Ok;
    }
    unsigned ContextGet(const CpuContext *context, int reg, unsigned &value) override
    {
        if (!context) return CpuError::Argument;
        auto previous = state;
        state = *reinterpret_cast<const NativeContext *>(context);
        unsigned result = Get(reg, value); state = previous; return result;
    }
    unsigned Map(unsigned address, unsigned size, unsigned prot) override
    {
        unsigned error = Range(address, size, 0);
        if (error || (prot & ~7u)) return error ? error : CpuError::Argument;
        long result = Syscall(SYS_mmap, address, size, prot, MAP_SHARED | MAP_FIXED_NOREPLACE, fd, address);
        if (result != address) return CpuError::Map;
        if (mmap(bytes + address, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, address) == MAP_FAILED)
        { Syscall(SYS_munmap, address, size); return CpuError::Map; }
        memset(bytes + address, 0, size);
        for (unsigned long long a = address; a < static_cast<unsigned long long>(address) + size; a += Page)
            rights[a / Page] = prot | Mapped;
        return Protect(address, size, prot);
    }
    unsigned MapHost(unsigned address, unsigned size, unsigned prot, void *host) override
    {
        if (!host) return CpuError::Argument;
        int backing = -1; uint64_t offset = 0;
        if (LinuxSharedMemory::Find(host, size, backing, offset))
        {
            unsigned error = Range(address, size, 0);
            if (error || (prot & ~7u)) return error ? error : CpuError::Argument;
            // The host allocated this file after the guest was forked. Open
            // its descriptor through procfs in the stopped guest.
            snprintf(reinterpret_cast<char *>(scratch), Page, "/proc/%ld/fd/%d", long(getpid()), backing);
            const long guest_fd = Syscall(SYS_openat, static_cast<unsigned long>(AT_FDCWD), reinterpret_cast<unsigned long>(scratch), O_RDWR | O_CLOEXEC);
            if (guest_fd >= 0)
            {
                const long mapped = Syscall(SYS_mmap, address, size, prot, MAP_SHARED | MAP_FIXED_NOREPLACE, guest_fd, offset);
                Syscall(SYS_close, guest_fd);
                if (mapped != address) return CpuError::Map;
                if (mmap(bytes + address, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, backing, offset) == MAP_FAILED)
                { Syscall(SYS_munmap, address, size); return CpuError::Map; }
                for (unsigned long long a = address; a < static_cast<unsigned long long>(address) + size; a += Page)
                    rights[a / Page] = prot | Mapped;
                const unsigned result = Protect(address, size, prot);
                if (!result) state_shared.push_back({address, size, static_cast<unsigned char *>(host)});
                return result;
            }
        }
        unsigned error = Map(address, size, prot);
        if (!error)
        {
            state_shared.push_back({address, size, static_cast<unsigned char *>(host)});
            shared.push_back({address, size, static_cast<unsigned char *>(host)}); memcpy(bytes + address, host, size);
            for (unsigned long long a = address; a < static_cast<unsigned long long>(address) + size; a += Page) Access(a / Page);
        }
        return error;
    }
    unsigned Unmap(unsigned address, unsigned size) override
    {
        unsigned error = Range(address, size, 1);
        if (error) return error;
        if (Syscall(SYS_munmap, address, size)) return CpuError::Map;
        ForgetMapping(address, size);
        return CpuError::Ok;
    }
  protected:
    void ForgetMapping(unsigned address, unsigned size)
    {
        const unsigned long long end = static_cast<unsigned long long>(address) + size;
        auto forget = [&](std::vector<Shared> &ranges) {
        std::vector<Shared> kept;
        for (auto s : ranges)
        {
            const unsigned long long se = static_cast<unsigned long long>(s.address) + s.size;
            if (se <= address || s.address >= end) kept.push_back(s);
            else
            {
                if (s.address < address) kept.push_back({s.address, address - s.address, s.host});
                if (se > end) kept.push_back({static_cast<unsigned>(end), static_cast<unsigned>(se - end), s.host + end - s.address});
            }
        }
        ranges.swap(kept);
        };
        forget(shared); forget(state_shared);
        for (unsigned long long a = address; a < end; a += Page)
        { rights[a / Page] = 0; Access(a / Page); }
    }
  public:
    unsigned Protect(unsigned address, unsigned size, unsigned prot) override
    {
        unsigned error = Range(address, size, 1);
        if (error || (prot & ~7u)) return error ? error : CpuError::Argument;
        for (unsigned long long a = address; a < static_cast<unsigned long long>(address) + size; a += Page)
        {
            if (Syscall(SYS_mprotect, a, Page, Protection(a, prot))) return CpuError::Map;
            rights[a / Page] = prot | Mapped;
            Access(a / Page);
        }
        return CpuError::Ok;
    }
    unsigned MapDevice(unsigned address, unsigned size, DeviceAccess access, void *context) override
    {
        if (device_access || !access || Range(address, size, 0)) return CpuError::Argument;
        device_base = address; device_size = size; device_access = access; device_context = context;
        return CpuError::Ok;
    }
    unsigned Read(unsigned address, void *out, unsigned size) override
    {
        if (out && WithinDevice(address, size))
        { device_access(device_context, address, static_cast<unsigned char *>(out), size, false); return CpuError::Ok; }
        if (!Has(address, size) || (!out && size)) return CpuError::ReadUnmapped;
        if (size) memcpy(out, bytes + address, size);
        for (const auto &s : shared)
        {
            unsigned long long begin = address > s.address ? address : s.address;
            unsigned long long end = static_cast<unsigned long long>(address) + size;
            if (end > static_cast<unsigned long long>(s.address) + s.size) end = static_cast<unsigned long long>(s.address) + s.size;
            if (end > begin) memcpy(static_cast<unsigned char *>(out) + begin - address, s.host + begin - s.address, end - begin);
        }
        return CpuError::Ok;
    }
    unsigned Write(unsigned address, const void *in, unsigned size) override
    {
        if (in && WithinDevice(address, size))
        {
            device_access(device_context, address, static_cast<unsigned char *>(const_cast<void *>(in)), size, true);
            return CpuError::Ok;
        }
        if (!Has(address, size) || (!in && size)) return CpuError::WriteUnmapped;
        if (size) memcpy(bytes + address, in, size);
        for (const auto &s : shared)
        {
            unsigned long long begin = address > s.address ? address : s.address;
            unsigned long long end = static_cast<unsigned long long>(address) + size;
            if (end > static_cast<unsigned long long>(s.address) + s.size) end = static_cast<unsigned long long>(s.address) + s.size;
            if (end > begin) memcpy(s.host + begin - s.address, bytes + begin, end - begin);
        }
        return CpuError::Ok;
    }
    void *HostPointer(unsigned address, unsigned size) override
    {
        if (!Has(address, size)) return nullptr;
        for (const auto &s : shared)
            if (address >= s.address && static_cast<unsigned long long>(address) + size <= static_cast<unsigned long long>(s.address) + s.size)
                return s.host + address - s.address;
        // A span that crosses shared and private backing has no contiguous
        // host pointer, even though each page can be read through Read.
        for (const auto &s : shared)
            if (static_cast<unsigned long long>(address) + size > s.address &&
                address < static_cast<unsigned long long>(s.address) + s.size) return nullptr;
        return bytes + address;
    }
    unsigned InvalidateCode(unsigned, unsigned) override { return CpuError::Ok; }
    unsigned FlushCode() override { return CpuError::Ok; }
    unsigned Run(unsigned begin, unsigned until, unsigned instructions) override
    {
        Frame local{frame}; frame = &local;
        struct RestoreFrame { NativeCpu &cpu; Frame *previous; ~RestoreFrame() { cpu.frame = previous; } } restore{*this, local.previous};
        requested = false;
        state.regs[Reg::Eip] = begin;
        if (unsigned error = SyncLdt()) return error;
        if (time_slice)
        {
            itimerval timer = {}; timer.it_value.tv_usec = timer.it_interval.tv_usec = 2000;
            memcpy(scratch, &timer, sizeof timer);
            if (Syscall(SYS_setitimer, ITIMER_VIRTUAL, reinterpret_cast<unsigned long>(scratch))) return CpuError::Resource;
        }
        unsigned count = 0;
        // The initial scheduler path single-steps. A run ending in a gate
        // without instrumentation uses PTRACE_SYSEMU and hardware speed.
        const bool step = instructions || budget || (until && Has(until, 1) && !InGate(until));
        while (!local.stopped && !requested)
        {
            unsigned ip = InstructionAddress();
            if ((until && ip == until) || (instructions && count >= instructions)) return CpuError::Ok;
            if (InGate(ip))
            {
                if (time_slice) { time_slice(*this, ip, time_context); if (local.stopped || requested) break; }
                gate(*this, ip, gate_context);
                if (local.stopped || requested) break;
                // A serviced API can load a hosted NE library without leaving
                // this run. Its far targets need real Linux LDT descriptors.
                if (unsigned error = SyncLdt()) return error;
                if (InstructionAddress() != ip) continue;
                // Critical-section gates contain a near jump into their
                // guest implementation. Follow it without opening the NX
                // gate page and injecting two mprotect calls per entry.
                if (!(state.regs[Reg::Cs] & 4) && Bits32(state.regs[Reg::Cs]))
                {
                    unsigned char jump[5];
                    if (!Read(ip, jump, sizeof jump) && jump[0] == 0xe9)
                    {
                        unsigned displacement = 0; memcpy(&displacement, jump + 1, 4);
                        state.regs[Reg::Eip] += 5 + displacement; ++count; continue;
                    }
                }
            }
            if (budget) { budget(*this, ip, budget_context); if (local.stopped || requested) break; }
            if (EnterWin16() || SegmentInstruction() || PortInstruction() || ClockInstruction() || CpuidInstruction() || SystemStatusInstruction()) { ++count; continue; }
            Sync(true);
            unsigned error = PushRegisters();
            if (error) return error;
            const bool gate_page = gate && !execute_gate_code && (ip & ~(Page - 1)) <= gate_end &&
                                   static_cast<unsigned long long>(ip & ~(Page - 1)) + Page > gate_begin;
            if (gate_page && Syscall(SYS_mprotect, ip & ~(Page - 1), Page, Protection(ip & ~(Page - 1), rights[ip / Page], true))) return CpuError::Map;
            // Syscall injection changes the tracee registers, so restore them.
            if (gate_page && (error = PushRegisters())) return error;
            executing = true;
            ++resumes; single_steps += step || gate_page;
            if (ptrace(step || gate_page ? PTRACE_SYSEMU_SINGLESTEP : PTRACE_SYSEMU, child, nullptr, nullptr))
            { executing = false; return CpuError::Resource; }
            int status = 0;
            const bool waited = Wait(status);
            executing = false;
            if (!waited || !PullRegisters()) return CpuError::Exception;
            siginfo_t info = {};
            if (ptrace(PTRACE_GETSIGINFO, child, nullptr, &info)) return CpuError::Resource;
            Sync(false);
            if (gate_page && Syscall(SYS_mprotect, ip & ~(Page - 1), Page, Protection(ip & ~(Page - 1), rights[ip / Page]))) return CpuError::Map;
            const int signal = WSTOPSIG(status);
            if (requested) break;
            if (signal == SIGSTOP) continue;
            if (signal == SIGVTALRM)
            {
                if (time_slice) time_slice(*this, InstructionAddress(), time_context);
                continue;
            }
            if (signal == (SIGTRAP | 0x80))
            {
                // Windows guest system calls must never enter the Linux ABI.
                if (!interrupt) return CpuError::Exception;
                interrupt(*this, 0x80, interrupt_context);
                continue;
            }
            if (signal == SIGTRAP)
            {
                if (info.si_code == TRAP_HWBKPT)
                {
                    const unsigned address = InstructionAddress();
                    if (ptrace(PTRACE_POKEUSER, child, offsetof(user, u_debugreg[6]), nullptr)) return CpuError::Resource;
                    bool found = false;
                    const size_t count = instruction_traces.size();
                    for (size_t i = 0; i < count; ++i)
                    {
                        const auto trace = instruction_traces[i];
                        if (trace.address == address) { found = true; trace.hook(*this, address, 0, trace.context); }
                    }
                    if (!found) return CpuError::Exception;
                    // Resume the instruction once without re-triggering its execution breakpoint.
                    if (InstructionAddress() == address) state.regs[Reg::Eflags] |= 0x10000;
                    continue;
                }
                if (info.si_code == TRAP_TRACE) { ++count; continue; }
                if (!interrupt) return CpuError::Exception;
                interrupt(*this, 3, interrupt_context);
                continue;
            }
            if (signal == SIGILL) { ReportInstruction("invalid instruction"); return CpuError::InvalidInstruction; }
            if (signal == SIGFPE)
            {
                if (!interrupt) return CpuError::Exception;
                interrupt(*this, 0, interrupt_context); continue;
            }
            if (signal != SIGSEGV && signal != SIGBUS) return CpuError::Exception;
            const unsigned next = InstructionAddress();
            if (InGate(next)) continue;
            if (time_slice)
            {
                time_slice(*this, next, time_context);
                if (local.stopped || requested) break;
            }
            unsigned trap = 0, hardware_error = 0;
            if (!HardwareFault(trap, hardware_error)) return CpuError::Exception;
            if (trap != 14)
            {
                // A native segment load can trap in the uninterrupted path.
                if (SegmentInstruction() || PortInstruction() || ClockInstruction() || CpuidInstruction() || SystemStatusInstruction()) continue;
                unsigned char op[2] = {};
                const bool fetched = !Read(next, op, 2);
                // CLI and STI of a protected-mode application go on: the VMM
                // of Win98 virtualizes its interrupt flag, the processor
                // here faults on them at IOPL 0.
                if (fetched && trap == 13 && (op[0] == 0xfa || op[0] == 0xfb))
                {
                    state.regs[Reg::Eip] += 1;
                    continue;
                }
                if (interrupt && fetched && op[0] == 0xcd)
                {
                    state.regs[Reg::Eip] += 2;
                    interrupt(*this, op[1], interrupt_context);
                    continue;
                }
                if (interrupt && trap < 32) { interrupt(*this, trap, interrupt_context); continue; }
                return CpuError::Exception;
            }
            const unsigned address = reinterpret_cast<uintptr_t>(info.si_addr);
            if (InDevice(address))
            {
                if (DeviceInstruction()) continue;
                ReportInstruction("device access by an instruction without a decoder");
                return CpuError::Device;
            }
            const bool fetch = hardware_error & 16, write = hardware_error & 2;
            bool mapped = rights[address / Page] & Mapped;
            if (write && mapped && WatchedWrite(address)) continue;
            unsigned kind = fetch ? (mapped ? CpuFault::FetchProtected : CpuFault::FetchUnmapped)
                                 : write ? (mapped ? CpuFault::WriteProtected : CpuFault::WriteUnmapped)
                                         : (mapped ? CpuFault::ReadProtected : CpuFault::ReadUnmapped);
            if (fault && fault(*this, kind, address, 1, fault_context)) continue;
            return fetch ? (mapped ? CpuError::FetchProtected : CpuError::FetchUnmapped)
                         : write ? (mapped ? CpuError::WriteProtected : CpuError::WriteUnmapped)
                                 : (mapped ? CpuError::ReadProtected : CpuError::ReadUnmapped);
        }
        return CpuError::Ok;
    }
    void Stop() override { if (frame) frame->stopped = true; }
    void RequestStop() override { requested = true; if (executing && child > 0) kill(child, SIGSTOP); }
    void *DetachFrame() override { auto *previous = frame; frame = nullptr; return previous; }
    void AttachFrame(void *saved) override { frame = static_cast<Frame *>(saved); }
    void Budget(int) override {}
    unsigned SetGateHook(unsigned begin, unsigned end, GateHook hook, void *context) override
    {
        if (begin > end) return CpuError::Argument;
        gate_begin = begin; gate_end = end; gate = hook; gate_context = context;
        for (unsigned p = 0; p < Pages; ++p)
            if ((rights[p] & Mapped) && Syscall(SYS_mprotect, p * Page, Page, Protection(p * Page, rights[p]))) return CpuError::Map;
        return CpuError::Ok;
    }
    unsigned SetFaultHook(FaultHook hook, void *context) override { fault = hook; fault_context = context; return CpuError::Ok; }
    unsigned SetGateCodeExecution(bool enabled) override
    {
        execute_gate_code = enabled;
        if (gate)
            for (unsigned p = gate_begin / Page; p <= gate_end / Page; ++p)
                if ((rights[p] & Mapped) && Syscall(SYS_mprotect, p * Page, Page, Protection(p * Page, rights[p]))) return CpuError::Map;
        return CpuError::Ok;
    }
    unsigned SetInterruptHook(InterruptHook hook, void *context) override { interrupt = hook; interrupt_context = context; return CpuError::Ok; }
    unsigned SetPortHooks(PortInputHook in, PortOutputHook out, void *context) override
    { port_in = in; port_out = out; port_context = context; return CpuError::Ok; }
    unsigned SetBudgetHook(BudgetHook hook, void *context, bool &counted) override
    { budget = hook; budget_context = context; counted = false; return CpuError::Ok; }
    unsigned SetTimeSliceHook(BudgetHook hook, void *context) override
    {
        if (!hook)
        {
            itimerval timer = {}; memcpy(scratch, &timer, sizeof timer);
            if (Syscall(SYS_setitimer, ITIMER_VIRTUAL, reinterpret_cast<unsigned long>(scratch))) return CpuError::Resource;
        }
        time_slice = hook; time_context = context; return CpuError::Ok;
    }
    unsigned WatchWrites(WriteHook hook, void *context) override
    {
        if (write_armed.empty()) write_armed.assign(AddressBytes / Page, 0);
        write_hook = hook; write_context = context;
        return CpuError::Ok;
    }
    void UnwatchWrites() override
    {
        if (!write_hook) return;
        write_hook = nullptr;
        for (size_t p = 0; p < write_armed.size(); ++p)
            if (write_armed[p])
            {
                write_armed[p] = 0;
                if (rights[p] & Mapped) ApplyPage(unsigned(p * Page));
            }
    }
    void RearmWrites(unsigned address, unsigned size) override
    {
        if (!write_hook || !size) return;
        for (unsigned long long p = address / Page; p <= (static_cast<unsigned long long>(address) + size - 1) / Page; ++p)
            if (!write_armed[p])
            {
                write_armed[p] = 1;
                if ((rights[p] & Mapped) && (rights[p] & Prot::Write)) ApplyPage(unsigned(p * Page));
            }
    }
    // The first write of the guest to an armed page: report it and give the
    // page its write access back.
    bool WatchedWrite(unsigned address)
    {
        const unsigned p = address / Page;
        if (!write_hook || !write_armed[p] || !(rights[p] & Mapped) || !(rights[p] & Prot::Write)) return false;
        write_armed[p] = 0;
        if (!ApplyPage(p * Page)) return false;
        write_hook(*this, p * Page, Page, write_context);
        return true;
    }
    // The protection of one page in the guest, after its rights or its
    // write watch changed.
    virtual bool ApplyPage(unsigned address)
    {
        return !Syscall(SYS_mprotect, address, Page, Protection(address, rights[address / Page]));
    }
    unsigned AddTrace(TraceKind kind, unsigned begin, unsigned end, TraceHook hook, void *context) override
    {
        if (kind != TraceKind::Instruction || begin != end || !hook || instruction_traces.size() == 4)
            return CpuError::Hook;
        const unsigned slot = unsigned(instruction_traces.size());
        if (ptrace(PTRACE_POKEUSER, child, offsetof(user, u_debugreg) + slot * sizeof(unsigned long),
                   static_cast<unsigned long>(begin))) return CpuError::Resource;
        unsigned control = 0;
        for (unsigned i = 0; i <= slot; ++i) control |= 1u << (i * 2);
        if (ptrace(PTRACE_POKEUSER, child, offsetof(user, u_debugreg[7]), static_cast<unsigned long>(control)))
            return CpuError::Resource;
        instruction_traces.push_back({begin, hook, context});
        return CpuError::Ok;
    }
    const char *StateRefusal() const override { return nullptr; }
    unsigned ContextBytes() override { return sizeof(NativeContext); }
    bool ContextExport(const CpuContext *context, void *out) override
    { if (!context || !out) return false; memcpy(out, context, sizeof(NativeContext)); return true; }
    bool ContextImport(CpuContext *context, const void *in) override
    { if (!context || !in) return false; memcpy(context, in, sizeof(NativeContext)); return true; }
    bool Regions(RegionVisitor visitor, void *context) override
    {
        for (unsigned p = 0; p < Pages;)
        {
            if (!(rights[p] & Mapped)) { ++p; continue; }
            unsigned end = p + 1;
            auto host_at = [&](unsigned page) -> unsigned char * {
                for (const auto &range : state_shared)
                    if (uint64_t(page) * Page >= range.address && uint64_t(page) * Page < uint64_t(range.address) + range.size)
                        return range.host + uint64_t(page) * Page - range.address;
                return nullptr;
            };
            unsigned char *host = host_at(p);
            while (end < Pages && rights[end] == rights[p] && host_at(end) == (host ? host + uint64_t(end - p) * Page : nullptr)) ++end;
            visitor(context, p * Page, (end - p) * Page, rights[p] & 7, host);
            p = end;
        }
        return true;
    }
};
#include "host_direct_cpu.inc"
unsigned MakeNative(Cpu *&out)
{
    const char *mode = getenv("NEO98_NATIVE_MODE");
    if (mode && strcmp(mode, "direct") && strcmp(mode, "ptrace")) return CpuError::Argument;
    bool direct = !mode || !strcmp(mode, "direct");
#if defined(__SANITIZE_ADDRESS__)
    if (!mode) direct = false;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
    if (!mode) direct = false;
#endif
#endif
    if (direct) return MakeDirect(out);
    if (getenv("NEO98_TRACE_CPU")) fprintf(stderr, "CPU native mode=ptrace\n");
    auto *cpu = new (std::nothrow) NativeCpu;
    if (!cpu) return CpuError::NoMemory;
    unsigned error = cpu->Open();
    if (error) { delete cpu; return error; }
    out = cpu; return CpuError::Ok;
}
struct Register { Register() { RegisterCpuBackend(CpuBackend::Native, MakeNative); } } registration;
} // namespace
} // namespace X86
