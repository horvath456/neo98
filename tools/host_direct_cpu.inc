// SPDX-License-Identifier: GPL-2.0-or-later
// Included after NativeCpu: register decoding and the Win98 segment model
// are shared with the ptrace backend. Execution and low mappings are local.
constexpr uint64_t DirectMinimum = 0x10000, DirectBridgeAddress = 0xfff00000;
struct DirectBacking
{
    int fd;
    explicit DirectBacking(int value) : fd(value) {}
    ~DirectBacking()
    {
        if (fd >= 0)
            close(fd);
    }
};
struct DirectMapping
{
    unsigned address, size;
    uint64_t offset;
    std::shared_ptr<DirectBacking> backing;
};
struct DirectArena
{
    std::recursive_mutex mutex;
    void *owner = nullptr;
    bool initialized = false;
    unsigned signal_number = 0;
    struct sigaction previous[NSIG] = {};
    bool ldt_valid[8192] = {};

    bool Initialize()
    {
        if (initialized)
            return true;
        if (!(getauxval(AT_HWCAP2) & HWCAP2_FSGSBASE))
            return false;
        void *base = mmap(reinterpret_cast<void *>(DirectMinimum), AddressBytes - DirectMinimum, PROT_NONE,
                          MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
        if (base == MAP_FAILED)
            return false;
        struct Reservation
        {
            DirectArena &arena;
            std::vector<int> signals;
            ~Reservation()
            {
                if (arena.initialized)
                    return;
                for (int signal : signals)
                    sigaction(signal, &arena.previous[signal], nullptr);
                munmap(reinterpret_cast<void *>(DirectMinimum), AddressBytes - DirectMinimum);
            }
        } reservation{*this, {}};
        auto *bridge = static_cast<unsigned char *>(mmap(reinterpret_cast<void *>(DirectBridgeAddress), Page,
                                                         PROT_READ | PROT_WRITE,
                                                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0));
        if (bridge == MAP_FAILED)
            return false;
        const size_t length = neo98_direct_gate_end - neo98_direct_gate_start;
        if (length > Page || length < 12)
            return false;
        memcpy(bridge, neo98_direct_gate_start, length);
        const auto finish = reinterpret_cast<uint64_t>(neo98_direct_finish);
        memcpy(bridge + length - 10, &finish, sizeof finish);
        if (mprotect(bridge, Page, PROT_READ | PROT_EXEC))
            return false;
        signal_number = unsigned(SIGRTMIN + 6);
        for (int signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP, SIGSYS, int(signal_number)})
        {
            struct sigaction action = {};
            action.sa_sigaction = neo98_direct_signal;
            action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
            sigemptyset(&action.sa_mask);
            if (sigaction(signal, &action, &previous[signal]))
                return false;
            reservation.signals.push_back(signal);
        }
        initialized = true;
        return true;
    }
    bool ClearLdt()
    {
        for (unsigned slot = 0; slot < 8192; ++slot)
            if (ldt_valid[slot])
            {
                user_desc empty = {};
                empty.entry_number = slot;
                empty.read_exec_only = empty.seg_not_present = 1;
                if (syscall(SYS_modify_ldt, 0x11, &empty, sizeof empty))
                    return false;
                ldt_valid[slot] = false;
            }
        return true;
    }
    bool ResetMappings()
    {
        // The arena owns this entire range. Replace its guest mappings in
        // two calls, preserving the shared transition code between them.
        // Resetting each allocation separately dominated process switches.
        const uint64_t starts[] = {DirectMinimum, DirectBridgeAddress + Page};
        const uint64_t ends[] = {DirectBridgeAddress, AddressBytes};
        for (unsigned i = 0; i < 2; ++i)
            if (mmap(reinterpret_cast<void *>(uintptr_t(starts[i])), ends[i] - starts[i], PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0) == MAP_FAILED)
                return false;
        owner = nullptr;
        return ClearLdt();
    }
};
DirectArena *direct_arena = nullptr;
DirectArena &Arena()
{
    // Providers and font machines can outlive ordinary host objects.
    static auto *value = new DirectArena;
    direct_arena = value;
    return *value;
}
class DirectCpu;
struct DirectRunRecord
{
    DirectRunRecord *previous = nullptr;
    DirectCpu *cpu = nullptr;
    void *saved_frame = nullptr;
};
struct DirectThreadState
{
    bool initialized = false;
    unsigned depth = 0;
    pid_t tid = 0;
    timer_t timer = {};
    unsigned char dispatch = SYSCALL_DISPATCH_FILTER_BLOCK;
    void *signal_stack = nullptr;
    DirectRunRecord *runs = nullptr;
    stack_t previous_stack = {};
    bool stack_installed = false, timer_created = false, dispatcher_on = false;
    ~DirectThreadState() { Cleanup(); }
    void Cleanup()
    {
        if (dispatcher_on)
            prctl(PR_SET_SYSCALL_USER_DISPATCH, PR_SYS_DISPATCH_OFF, 0, 0, nullptr);
        if (depth)
            GuestTraps(false);
        neo98_direct_clock_trap = false;
        if (neo98_direct_dispatch_selector == &dispatch)
            neo98_direct_dispatch_selector = nullptr;
        if (timer_created)
            timer_delete(timer);
        if (stack_installed)
            sigaltstack(&previous_stack, nullptr);
        if (signal_stack)
            munmap(signal_stack, SignalBytes);
        signal_stack = nullptr;
        initialized = stack_installed = timer_created = dispatcher_on = false;
    }
    bool Initialize()
    {
        if (initialized)
            return true;
        Cleanup();
        tid = pid_t(syscall(SYS_gettid));
        signal_stack = mmap(nullptr, SignalBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (signal_stack == MAP_FAILED)
        {
            signal_stack = nullptr;
            return false;
        }
        stack_t stack = {};
        stack.ss_sp = signal_stack;
        stack.ss_size = SignalBytes;
        if (sigaltstack(&stack, &previous_stack))
            return false;
        stack_installed = true;
        sigevent event = {};
        event.sigev_notify = SIGEV_THREAD_ID;
        event.sigev_signo = int(Arena().signal_number);
        event._sigev_un._tid = tid;
        if (timer_create(CLOCK_THREAD_CPUTIME_ID, &event, &timer))
            return false;
        timer_created = true;
        // Native host syscall instructions are above the guest's 4 GiB.
        if (prctl(PR_SET_SYSCALL_USER_DISPATCH, PR_SYS_DISPATCH_ON, AddressBytes,
                  std::numeric_limits<unsigned long>::max() - AddressBytes, &dispatch))
            return false;
        dispatcher_on = true;
        neo98_direct_dispatch_selector = &dispatch;
        initialized = true;
        return true;
    }
    bool Enter()
    {
        if (depth++)
            return true;
        neo98_direct_clock_trap = true;
        if (GuestTraps(true))
        {
            neo98_direct_clock_trap = false;
            --depth;
            return false;
        }
        itimerspec time = {};
        time.it_value.tv_nsec = time.it_interval.tv_nsec = 2000000;
        if (timer_settime(timer, 0, &time, nullptr))
        {
            GuestTraps(false);
            neo98_direct_clock_trap = false;
            --depth;
            return false;
        }
        return true;
    }
    void Leave()
    {
        if (--depth)
            return;
        itimerspec time = {};
        timer_settime(timer, 0, &time, nullptr);
        GuestTraps(false);
        neo98_direct_clock_trap = false;
    }
};
DirectThreadState &DirectThread()
{
    static thread_local DirectThreadState value;
    return value;
}
std::atomic<unsigned long long> direct_host_tsc_traps{0};
extern "C" int neo98_direct_handle_signal(int signal, siginfo_t *info, void *opaque, Neo98DirectFrame *live)
{
    auto &context = *static_cast<ucontext_t *>(opaque);
    auto &r = context.uc_mcontext.gregs;
    const auto ip = uint64_t(r[REG_RIP]);
    const unsigned cs = unsigned(r[REG_CSGSFS]) & 0xffff;
    if (live && direct_arena && signal == int(direct_arena->signal_number) && !live->want_slice &&
        (!live->stop_requested || !live->stop_requested->load(std::memory_order_relaxed)))
        return 0;
    // A host library may read TSC without calling the interposed clock.
    // Read the actual hardware counter for it, then restore guest trapping.
    if (signal == SIGSEGV && r[REG_TRAPNO] == 13 && ip >= AddressBytes && neo98_direct_clock_trap)
    {
        const auto *op = reinterpret_cast<const unsigned char *>(ip);
        if (op[0] == 0x0f && (op[1] == 0x31 || (op[1] == 1 && op[2] == 0xf9)))
        {
            direct_host_tsc_traps.fetch_add(1, std::memory_order_relaxed);
            unsigned low, high, aux = 0;
            GuestTraps(false);
            if (op[1] == 0x31)
                asm volatile("rdtsc" : "=a"(low), "=d"(high));
            else
                asm volatile("rdtscp" : "=a"(low), "=d"(high), "=c"(aux));
            GuestTraps(true);
            r[REG_RAX] = low;
            r[REG_RDX] = high;
            if (op[1] == 1)
                r[REG_RCX] = aux;
            r[REG_RIP] += op[1] == 0x31 ? 2 : 3;
            return 0;
        }
    }
    // The same for CPUID of a host library: the real processor.
    if (signal == SIGSEGV && r[REG_TRAPNO] == 13 && ip >= AddressBytes && neo98_direct_clock_trap)
    {
        const auto *op = reinterpret_cast<const unsigned char *>(ip);
        if (op[0] == 0x0f && op[1] == 0xa2)
        {
            unsigned a, b, c, d;
            syscall(SYS_arch_prctl, ArchSetCpuid, 1);
            __cpuid_count(unsigned(r[REG_RAX]), unsigned(r[REG_RCX]), a, b, c, d);
            syscall(SYS_arch_prctl, ArchSetCpuid, 0);
            r[REG_RAX] = a;
            r[REG_RBX] = b;
            r[REG_RCX] = c;
            r[REG_RDX] = d;
            r[REG_RIP] += 2;
            return 0;
        }
    }
    const bool entry_failure = live && (signal == SIGSEGV || signal == SIGBUS || signal == SIGILL) &&
                               cs == 0x33 && ip >= reinterpret_cast<uintptr_t>(neo98_direct_enter) &&
                               ip < reinterpret_cast<uintptr_t>(neo98_direct_enter_end);
    if (live && (cs != 0x33 || entry_failure))
    {
        const unsigned registers[] = {REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP, REG_RSI, REG_RDI};
        if (!entry_failure)
        {
            for (unsigned i = 0; i < 8; ++i)
                live->gpr[i] = unsigned(r[registers[i]]);
            live->ip = unsigned(ip);
            live->flags = uint64_t(r[REG_EFL]);
            live->segments[1] = cs;
            live->segments[2] = (uint64_t(r[REG_CSGSFS]) >> 48) & 0xffff;
        }
        // The wrapper captured DS/ES/FS/GS before restoring host TLS.
        // Linux's 64-bit sigcontext deliberately leaves FS and GS zero.
        if (!entry_failure && context.uc_mcontext.fpregs)
            memcpy(&live->guest_fp, context.uc_mcontext.fpregs, sizeof live->guest_fp);
        live->event = entry_failure ? 3 : 2;
        live->signal = signal;
        live->signal_code = info->si_code;
        live->trap = uint64_t(r[REG_TRAPNO]);
        live->error = uint64_t(r[REG_ERR]);
        live->address = reinterpret_cast<uintptr_t>(info->si_addr);
        r[REG_RIP] = reinterpret_cast<uintptr_t>(neo98_direct_finish);
        r[REG_RSP] = live->host_sp;
        r[REG_R12] = reinterpret_cast<uintptr_t>(live);
        r[REG_EFL] = 0x202;
        r[REG_CSGSFS] = 0x33ull | (live->host_gss << 16) | (live->host_fss << 32) | (live->host_ss << 48);
        context.uc_flags |= 6;
        if (context.uc_mcontext.fpregs)
            memcpy(context.uc_mcontext.fpregs, &live->host_fp, sizeof live->host_fp);
        return 1;
    }
    if (direct_arena && signal == int(direct_arena->signal_number))
        return 0;
    if (direct_arena)
    {
        const auto &before = direct_arena->previous[signal];
        if (before.sa_handler == SIG_IGN)
            return 0;
        if (before.sa_handler != SIG_DFL && before.sa_handler)
        {
            if (before.sa_flags & SA_SIGINFO)
                before.sa_sigaction(signal, info, opaque);
            else
                before.sa_handler(signal);
            return 0;
        }
        sigaction(signal, &before, nullptr);
        syscall(SYS_tgkill, getpid(), syscall(SYS_gettid), signal);
        return 0;
    }
    _exit(128 + signal);
}

class DirectCpu final : public NativeCpu
{
    std::vector<DirectMapping> mappings;
    std::vector<DirectMapping> activation_mappings;
    bool mappings_changed = true;
    std::shared_ptr<DirectBacking> private_backing;
    // Original gate bytes stay in the process image. Prepared far calls live
    // in a separate backing, reused across activations and invalidated on writes.
    std::shared_ptr<DirectBacking> gate_backing;
    std::vector<unsigned char> gate_ready{std::vector<unsigned char>(Pages)};
    std::atomic<pid_t> running_tid{0};
    bool return_gates = false;
    uint64_t entries = 0, direct_gates = 0, signals = 0, low_maps = 0;

    long Syscall(long number, unsigned long a = 0, unsigned long b = 0, unsigned long c = 0,
                 unsigned long d = 0, unsigned long e = 0, unsigned long f = 0) override
    {
        ++injected_syscalls;
        const long result = syscall(number, a, b, c, d, e, f);
        if (number == SYS_modify_ldt && result == 0 && a == 0x11)
        {
            const auto &desc = *reinterpret_cast<const user_desc *>(b);
            Arena().ldt_valid[desc.entry_number] = !desc.seg_not_present;
        }
        return result == -1 ? -errno : result;
    }
    bool ValidRange(unsigned address, unsigned size) const
    {
        return address >= DirectMinimum &&
               (uint64_t(address) + size <= DirectBridgeAddress || address >= DirectBridgeAddress + Page);
    }
    bool GatePage(unsigned page) const
    {
        return gate && page * uint64_t(Page) <= gate_end && (page + 1) * uint64_t(Page) > gate_begin;
    }
    bool PrepareGate(unsigned page)
    {
        if (gate_ready[page])
            return true;
        if (!gate_backing)
        {
            const int fd = int(syscall(SYS_memfd_create, "neo98-gates", MFD_CLOEXEC));
            if (fd < 0)
                return false;
            gate_backing = std::make_shared<DirectBacking>(fd);
            if (ftruncate(fd, AddressBytes))
                return false;
        }
        unsigned char out[Page];
        memcpy(out, bytes + page * uint64_t(Page), Page);
        const unsigned start = std::max(gate_begin, page * Page);
        const uint64_t end = std::min(uint64_t(gate_end) + 1, (page + 1) * uint64_t(Page));
        for (uint64_t address = start; address + 7 <= end; address += 8)
            if (bytes[address] == 0xf4 || (return_gates && !((address - gate_begin) & 15) &&
                                           (bytes[address] == 0xc2 || bytes[address] == 0xc3)))
            {
                unsigned char bridge[] = {0x9a, 0, 0, 0, 0, 0x33, 0};
                const unsigned target = DirectBridgeAddress;
                memcpy(bridge + 1, &target, 4);
                memcpy(out + address - uint64_t(page) * Page, bridge, sizeof bridge);
            }
        size_t written = 0;
        while (written < Page)
        {
            const auto n = pwrite(gate_backing->fd, out + written, Page - written,
                                  uint64_t(page) * Page + written);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                return false;
            written += size_t(n);
        }
        gate_ready[page] = 1;
        return true;
    }
    bool MapLow(const DirectMapping &map)
    {
        const uint64_t end = uint64_t(map.address) + map.size;
        for (uint64_t at = map.address; at < end;)
        {
            unsigned p = unsigned(at / Page);
            const bool overlay = GatePage(p) && execute_gate_code;
            const int prot = Protection(unsigned(at), rights[p]);
            uint64_t stop = at + Page;
            while (stop < end && (GatePage(unsigned(stop / Page)) && execute_gate_code) == overlay &&
                   prot == Protection(unsigned(stop), rights[stop / Page]))
                stop += Page;
            if (overlay)
                for (uint64_t a = at; a < stop; a += Page)
                    if (!PrepareGate(unsigned(a / Page)))
                        return false;
            if (mmap(reinterpret_cast<void *>(uintptr_t(at)), stop - at, prot,
                     (overlay ? MAP_PRIVATE : MAP_SHARED) | MAP_FIXED,
                     overlay ? gate_backing->fd : map.backing->fd,
                     overlay ? at : map.offset + at - map.address) == MAP_FAILED)
                return false;
            ++low_maps;
            at = stop;
        }
        return true;
    }
    unsigned Activate()
    {
        auto &arena = Arena();
        if (arena.owner == this)
            return CpuError::Ok;
        if (!arena.ResetMappings())
            return CpuError::Map;
        memset(descriptor_valid, 0, sizeof descriptor_valid);
        ldt_snapshot_base = ldt_snapshot_count = 0;
        if (mappings_changed)
        {
            activation_mappings = mappings;
            std::sort(activation_mappings.begin(), activation_mappings.end(),
                      [](const DirectMapping &a, const DirectMapping &b) { return a.address < b.address; });
            size_t count = 0;
            for (const auto &map : activation_mappings)
            {
                if (count && uint64_t(activation_mappings[count - 1].address) +
                                     activation_mappings[count - 1].size == map.address &&
                    activation_mappings[count - 1].backing == map.backing &&
                    activation_mappings[count - 1].offset + activation_mappings[count - 1].size == map.offset)
                    activation_mappings[count - 1].size += map.size;
                else
                    activation_mappings[count++] = map;
            }
            activation_mappings.resize(count);
            mappings_changed = false;
        }
        // MapLow still splits each run at protection changes and gate pages.
        for (const auto &map : activation_mappings)
            if (!MapLow(map))
                return CpuError::Map;
        arena.owner = this;
        return CpuError::Ok;
    }
    unsigned RefreshGates(unsigned address, unsigned size)
    {
        if (!gate || !size)
            return CpuError::Ok;
        const uint64_t end = uint64_t(address) + size;
        for (unsigned p = address / Page; p * uint64_t(Page) < end; ++p)
        {
            if (!GatePage(p) || !(rights[p] & Mapped))
                continue;
            gate_ready[p] = 0;
            if (Arena().owner != this)
                continue;
            for (const auto &map : mappings)
                if (p * uint64_t(Page) >= map.address &&
                    p * uint64_t(Page) < uint64_t(map.address) + map.size)
                {
                    DirectMapping one{p * Page, Page, map.offset + p * uint64_t(Page) - map.address,
                                      map.backing};
                    if (!MapLow(one))
                        return CpuError::Map;
                    break;
                }
        }
        return CpuError::Ok;
    }
    void RemoveMappings(unsigned address, unsigned size)
    {
        const uint64_t end = uint64_t(address) + size;
        std::vector<DirectMapping> kept;
        for (auto map : mappings)
        {
            const uint64_t stop = uint64_t(map.address) + map.size;
            if (stop <= address || map.address >= end)
                kept.push_back(map);
            else
            {
                if (map.address < address)
                    kept.push_back({map.address, address - map.address, map.offset, map.backing});
                if (stop > end)
                    kept.push_back(
                        {unsigned(end), unsigned(stop - end), map.offset + end - map.address, map.backing});
            }
        }
        mappings.swap(kept);
        mappings_changed = true;
        activation_mappings.clear();
        for (uint64_t at = address; at < end; at += Page)
            gate_ready[at / Page] = 0;
    }
    unsigned AddMapping(unsigned address, unsigned size, unsigned prot,
                        std::shared_ptr<DirectBacking> backing, uint64_t offset)
    {
        unsigned error = Range(address, size, 0);
        if (error || (prot & ~7u) || !ValidRange(address, size))
            return error ? error : CpuError::Argument;
        if (mmap(bytes + address, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, backing->fd,
                 offset) == MAP_FAILED)
            return CpuError::Map;
        mappings.push_back({address, size, offset, std::move(backing)});
        mappings_changed = true;
        activation_mappings.clear();
        for (uint64_t at = address; at < uint64_t(address) + size; at += Page)
        {
            rights[at / Page] = prot | Mapped;
            gate_ready[at / Page] = 0;
            Access(unsigned(at / Page));
        }
        if (Arena().owner == this)
        {
            if (!MapLow(mappings.back()))
                return CpuError::Map;
        }
        return CpuError::Ok;
    }
    unsigned NativeBase(unsigned selector) const
    {
        if (!(selector & 4) || (selector >> 3) >= 8192)
            return 0;
        const auto desc = descriptors[selector >> 3];
        return ((desc >> 16) & 0xffffff) | ((desc >> 32) & 0xff000000);
    }
    unsigned Pull(Neo98DirectFrame &bridge)
    {
        if (bridge.event == 1)
        {
            unsigned saved[2];
            const unsigned sp = unsigned(bridge.gpr[Reg::Esp]);
            if (Read(NativeBase(unsigned(bridge.segments[2])) + sp, saved, sizeof saved))
                return CpuError::Exception;
            bridge.gpr[Reg::Esp] += 8;
            bridge.segments[1] = saved[1] & 0xffff;
            bridge.ip = saved[0] - 7;
        }
        user_regs_struct r = {};
        r.rax = bridge.gpr[0];
        r.rcx = bridge.gpr[1];
        r.rdx = bridge.gpr[2];
        r.rbx = bridge.gpr[3];
        r.rsp = bridge.gpr[4];
        r.rbp = bridge.gpr[5];
        r.rsi = bridge.gpr[6];
        r.rdi = bridge.gpr[7];
        r.rip = bridge.ip;
        r.eflags = bridge.flags;
        r.es = bridge.segments[0];
        r.cs = bridge.segments[1];
        r.ss = bridge.segments[2];
        r.ds = bridge.segments[3];
        r.fs = bridge.segments[4];
        r.gs = bridge.segments[5];
        state.fp = bridge.guest_fp;
        ApplyRegisters(r);
        return CpuError::Ok;
    }

  public:
    ~DirectCpu() override
    {
        std::lock_guard<std::recursive_mutex> lock(Arena().mutex);
        if (Arena().owner == this)
            Arena().ResetMappings();
        if (getenv("NEO98_NATIVE_STATS"))
            fprintf(stderr,
                    "DIRECT STATS pid=%ld entries=%llu gates=%llu signals=%llu low_maps=%llu syscalls=%llu "
                    "mirror_bytes=%llu host_tsc_traps=%llu\n",
                    long(getpid()), (unsigned long long)entries, (unsigned long long)direct_gates,
                    (unsigned long long)signals, (unsigned long long)low_maps, injected_syscalls,
                    mirror_bytes, direct_host_tsc_traps.load(std::memory_order_relaxed));
    }
    unsigned Open()
    {
        std::lock_guard<std::recursive_mutex> lock(Arena().mutex);
        if (!Arena().Initialize())
            return CpuError::Arch;
        if (!DirectThread().Initialize())
            return CpuError::Resource;
        fd = int(syscall(SYS_memfd_create, "neo98-direct", MFD_CLOEXEC));
        if (fd < 0 || ftruncate(fd, AddressBytes))
            return CpuError::NoMemory;
        bytes = static_cast<unsigned char *>(
            mmap(nullptr, AddressBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
        if (bytes == MAP_FAILED)
        {
            bytes = nullptr;
            return CpuError::NoMemory;
        }
        scratch = static_cast<unsigned char *>(
            mmap(nullptr, Page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (scratch == MAP_FAILED)
        {
            scratch = nullptr;
            return CpuError::NoMemory;
        }
        private_backing = std::make_shared<DirectBacking>(fcntl(fd, F_DUPFD_CLOEXEC, 0));
        if (private_backing->fd < 0)
            return CpuError::Resource;
        state.regs[Reg::Cs] = 8;
        state.regs[Reg::Ss] = state.regs[Reg::Ds] = state.regs[Reg::Es] = 16;
        state.regs[Reg::Eflags] = 0x202;
        state.regs[Reg::Cr0] = 1;
        state.fp.cwd = 0x37f;
        state.fp.mxcsr = 0x1f80;
        memory = bytes;
        page_read = readable.data();
        page_write = writable.data();
        if (getenv("NEO98_TRACE_CPU"))
            fprintf(stderr, "CPU native mode=direct pid=%ld\n", long(getpid()));
        return CpuError::Ok;
    }
    unsigned Map(unsigned address, unsigned size, unsigned prot) override
    {
        auto error = AddMapping(address, size, prot, private_backing, address);
        if (!error)
            memset(bytes + address, 0, size);
        return error;
    }
    unsigned MapHost(unsigned address, unsigned size, unsigned prot, void *host) override
    {
        if (!host)
            return CpuError::Argument;
        int backing = -1;
        uint64_t offset = 0;
        if (LinuxSharedMemory::Find(host, size, backing, offset))
        {
            const int copy = fcntl(backing, F_DUPFD_CLOEXEC, 0);
            if (copy < 0)
                return CpuError::Resource;
            const unsigned result = AddMapping(address, size, prot, std::make_shared<DirectBacking>(copy), offset);
            if (!result) state_shared.push_back({address, size, static_cast<unsigned char *>(host)});
            return result;
        }
        unsigned error = Map(address, size, prot);
        if (!error)
        {
            state_shared.push_back({address, size, static_cast<unsigned char *>(host)});
            shared.push_back({address, size, static_cast<unsigned char *>(host)});
            memcpy(bytes + address, host, size);
            for (uint64_t at = address; at < uint64_t(address) + size; at += Page)
                Access(unsigned(at / Page));
        }
        return error;
    }
    unsigned Unmap(unsigned address, unsigned size) override
    {
        const unsigned error = Range(address, size, 1);
        if (error)
            return error;
        if (Arena().owner == this &&
            mmap(reinterpret_cast<void *>(uintptr_t(address)), size, PROT_NONE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE, -1, 0) == MAP_FAILED)
            return CpuError::Map;
        ForgetMapping(address, size);
        RemoveMappings(address, size);
        return CpuError::Ok;
    }
    unsigned Protect(unsigned address, unsigned size, unsigned prot) override
    {
        unsigned error = Range(address, size, 1);
        if (error || (prot & ~7u))
            return error ? error : CpuError::Argument;
        for (uint64_t at = address; at < uint64_t(address) + size; at += Page)
        {
            rights[at / Page] = prot | Mapped;
            Access(unsigned(at / Page));
            if (Arena().owner == this &&
                mprotect(reinterpret_cast<void *>(uintptr_t(at)), Page, Protection(unsigned(at), prot)))
                return CpuError::Map;
        }
        return CpuError::Ok;
    }
    unsigned Write(unsigned address, const void *in, unsigned size) override
    {
        const auto error = NativeCpu::Write(address, in, size);
        if (error && getenv("NEO98_TRACE_DIRECT"))
            fprintf(stderr, "DIRECT Write failed address=%08x bytes=%u ip=%08x\n", address, size,
                    InstructionAddress());
        return error ? error : RefreshGates(address, size);
    }
    unsigned SetGateHook(unsigned begin, unsigned end, GateHook hook, void *context) override
    {
        if (begin > end)
            return CpuError::Argument;
        const unsigned old_begin = gate_begin, old_end = gate_end;
        const bool had_gate = gate != nullptr;
        gate_begin = begin;
        gate_end = end;
        gate = hook;
        gate_context = context;
        std::fill(gate_ready.begin(), gate_ready.end(), 0);
        if (Arena().owner == this)
            for (const auto &map : mappings)
                if ((uint64_t(map.address) <= end && uint64_t(map.address) + map.size > begin) ||
                    (had_gate && uint64_t(map.address) <= old_end &&
                     uint64_t(map.address) + map.size > old_begin))
                    if (!MapLow(map))
                        return CpuError::Map;
        return CpuError::Ok;
    }
    unsigned SetGateCodeExecution(bool enabled) override
    {
        execute_gate_code = enabled;
        return gate ? RefreshGates(gate_begin, gate_end - gate_begin + 1) : unsigned(CpuError::Ok);
    }
    unsigned SetTimeSliceHook(BudgetHook hook, void *context) override
    {
        time_slice = hook;
        time_context = context;
        return CpuError::Ok;
    }
    unsigned SetReturnGateExecution(bool enabled) override
    {
        return_gates = enabled;
        return SetGateCodeExecution(enabled);
    }
    unsigned AddTrace(TraceKind kind, unsigned begin, unsigned end, TraceHook hook, void *context) override
    {
        if (kind != TraceKind::Instruction || begin != end || !hook || instruction_traces.size() == 4)
            return CpuError::Hook;
        instruction_traces.push_back({begin, hook, context});
        return CpuError::Ok;
    }
    void RequestStop() override
    {
        requested = true;
        const auto tid = running_tid.load();
        if (tid)
            syscall(SYS_tgkill, getpid(), tid, Arena().signal_number);
    }
    void *DetachFrame() override
    {
        auto &thread = DirectThread();
        auto *saved = thread.runs;
        if (!saved)
        {
            frame = nullptr;
            return nullptr;
        }
        thread.runs = nullptr;
        // Yielding a fiber suspends the whole native call stack. Release its
        // per-thread policies and lock counts even if that fiber is later
        // discarded without unwinding its C++ Run scopes.
        for (auto *run = saved; run; run = run->previous)
        {
            run->saved_frame = run->cpu->frame;
            run->cpu->frame = nullptr;
            thread.Leave();
            Arena().mutex.unlock();
        }
        return saved;
    }
    void AttachFrame(void *opaque) override
    {
        auto *saved = static_cast<DirectRunRecord *>(opaque);
        if (!saved)
        {
            frame = nullptr;
            return;
        }
        auto &thread = DirectThread();
        for (auto *run = saved; run; run = run->previous)
        {
            Arena().mutex.lock();
            if (!thread.Enter())
                _exit(125);
            if (run->saved_frame)
                run->cpu->frame = static_cast<Frame *>(run->saved_frame);
        }
        thread.runs = saved;
    }
    unsigned Run(unsigned begin, unsigned until, unsigned instructions) override
    {
        std::lock_guard<std::recursive_mutex> lock(Arena().mutex);
        auto &thread = DirectThread();
        if (!thread.Initialize() || !thread.Enter())
            return CpuError::Resource;
        DirectRunRecord run_record{thread.runs, this};
        thread.runs = &run_record;
        struct Scope
        {
            DirectThreadState &thread;
            DirectRunRecord &record;
            ~Scope()
            {
                thread.runs = record.previous;
                thread.Leave();
            }
        } scope{thread, run_record};
        Frame local{frame};
        frame = &local;
        struct RestoreFrame
        {
            DirectCpu &cpu;
            Frame *previous;
            ~RestoreFrame() { cpu.frame = previous; }
        } restore{*this, local.previous};
        requested = false;
        state.regs[Reg::Eip] = begin;
        unsigned count = 0;
        bool service_gate = InGate(InstructionAddress());
        unsigned char until_opcode = 0;
        const bool checked_until =
            until && Has(until, 1) && (!InGate(until) ||
                      (execute_gate_code && (Read(until, &until_opcode, 1) || until_opcode != 0xf4)));
        const bool step = instructions || budget || checked_until || !instruction_traces.empty();
        while (!local.stopped && !requested)
        {
            unsigned ip = InstructionAddress();
            if ((until && ip == until) || (instructions && count >= instructions))
                return CpuError::Ok;
            if (InGate(ip) && (!execute_gate_code || service_gate))
            {
                service_gate = false;
                if (time_slice)
                {
                    time_slice(*this, ip, time_context);
                    if (local.stopped || requested)
                        break;
                }
                gate(*this, ip, gate_context);
                if (local.stopped || requested)
                    break;
                if (InstructionAddress() != ip)
                    continue;
                if (return_gates)
                {
                    unsigned char code[3];
                    if (!Read(ip, code, sizeof code) && (code[0] == 0xc2 || code[0] == 0xc3))
                    {
                        unsigned ret;
                        const unsigned sp = state.regs[Reg::Esp];
                        if (Read(Base(state.regs[Reg::Ss]) + sp, &ret, 4))
                            return CpuError::ReadUnmapped;
                        state.regs[Reg::Esp] =
                            sp + 4 + (code[0] == 0xc2 ? code[1] | (unsigned(code[2]) << 8) : 0);
                        state.regs[Reg::Eip] = ret;
                        ++count;
                        continue;
                    }
                }
                if (!(state.regs[Reg::Cs] & 4) && Bits32(state.regs[Reg::Cs]))
                {
                    unsigned char jump[5];
                    if (!Read(ip, jump, sizeof jump) && jump[0] == 0xe9)
                    {
                        unsigned displacement = 0;
                        memcpy(&displacement, jump + 1, 4);
                        state.regs[Reg::Eip] += 5 + displacement;
                        ++count;
                        continue;
                    }
                }
            }
            if (budget)
            {
                budget(*this, ip, budget_context);
                if (local.stopped || requested)
                    break;
            }
            for (const auto &trace : instruction_traces)
                if (trace.address == ip)
                    trace.hook(*this, ip, 0, trace.context);
            if (local.stopped || requested)
                break;
            if (EnterWin16() || SegmentInstruction() || PortInstruction() || ClockInstruction() || CpuidInstruction() ||
                SystemStatusInstruction())
            {
                ++count;
                continue;
            }
            if (unsigned error = Activate())
                return error;
            {
                RenderProfile::Scope profile(RenderProfile::NativeLdt);
                if (unsigned error = SyncLdt()) return error;
            }
            user_regs_struct r;
            {
                RenderProfile::Scope profile(RenderProfile::NativeRegisters);
                if (unsigned error = PrepareRegisters(r)) return error;
            }
            Neo98DirectFrame bridge;
            const uint64_t values[] = {r.rax, r.rcx, r.rdx, r.rbx, r.rsp, r.rbp, r.rsi, r.rdi};
            memcpy(bridge.gpr, values, sizeof values);
            bridge.ip = r.rip;
            bridge.flags = r.eflags;
            bridge.segments[0] = r.es;
            bridge.segments[1] = r.cs;
            bridge.segments[2] = r.ss;
            bridge.segments[3] = r.ds;
            bridge.segments[4] = r.fs;
            bridge.segments[5] = r.gs;
            bridge.guest_fp = state.fp;
            bridge.stop_requested = &requested;
            bridge.want_slice = time_slice != nullptr;
            const bool gate_step = InGate(ip) && !execute_gate_code;
            if (step || gate_step)
                bridge.flags |= 0x100;
            if (gate_step && mprotect(reinterpret_cast<void *>(uintptr_t(ip & ~(Page - 1))), Page,
                                      Protection(ip & ~(Page - 1), rights[ip / Page], true)))
                return CpuError::Map;
            Sync(true);
            running_tid = thread.tid;
            neo98_direct_active_tid = uint64_t(thread.tid);
            ++entries;
            {
                RenderProfile::Scope profile(RenderProfile::NativeResume);
                neo98_direct_enter(&bridge);
            }
            running_tid = 0;
            if (gate_step && mprotect(reinterpret_cast<void *>(uintptr_t(ip & ~(Page - 1))), Page,
                                      Protection(ip & ~(Page - 1), rights[ip / Page])))
                return CpuError::Map;
            if (!(r.eflags & 0x100))
                bridge.flags &= ~0x100ull;
            Sync(false);
            if (unsigned error = Pull(bridge))
                return error;
            if (bridge.event == 3)
                return CpuError::Exception;
            if (until && InstructionAddress() == until)
                return CpuError::Ok;
            if (getenv("NEO98_TRACE_DIRECT") && entries < 300)
                fprintf(stderr,
                        "DIRECT enter=%08x next=%08x event=%llu signal=%llu trap=%llu esp=%08x cs=%04x "
                        "ss=%04x fs=%04x\n",
                        ip, InstructionAddress(), (unsigned long long)bridge.event,
                        (unsigned long long)bridge.signal, (unsigned long long)bridge.trap,
                        state.regs[Reg::Esp], state.regs[Reg::Cs], state.regs[Reg::Ss], state.regs[Reg::Fs]);
            if (bridge.event == 1)
            {
                ++direct_gates;
                service_gate = true;
                continue;
            }
            ++signals;
            const int signal = int(bridge.signal);
            if (requested)
                break;
            if (signal == int(Arena().signal_number))
            {
                if (time_slice)
                    time_slice(*this, InstructionAddress(), time_context);
                continue;
            }
            if (signal == SIGTRAP && int(bridge.signal_code) == TRAP_TRACE)
            {
                ++count;
                unsigned char opcode = 0;
                service_gate =
                    InGate(InstructionAddress()) &&
                    (!execute_gate_code || (!Read(InstructionAddress(), &opcode, 1) && opcode == 0xf4));
                continue;
            }
            if (signal == SIGSYS)
            {
                if (!interrupt)
                    return CpuError::Exception;
                interrupt(*this, 0x80, interrupt_context);
                continue;
            }
            if (signal == SIGTRAP)
            {
                if (!interrupt)
                    return CpuError::Exception;
                interrupt(*this, 3, interrupt_context);
                continue;
            }
            if (signal == SIGILL)
                return CpuError::InvalidInstruction;
            if (signal == SIGFPE)
            {
                if (!interrupt)
                    return CpuError::Exception;
                interrupt(*this, 0, interrupt_context);
                continue;
            }
            if (signal != SIGSEGV && signal != SIGBUS)
                return CpuError::Exception;
            const unsigned next = InstructionAddress();
            unsigned char gate_opcode = 0;
            if (InGate(next) && (!execute_gate_code || (!Read(next, &gate_opcode, 1) && gate_opcode == 0xf4)))
            {
                service_gate = true;
                continue;
            }
            if (bridge.trap != 14)
            {
                if (SegmentInstruction() || PortInstruction() || ClockInstruction() || CpuidInstruction() ||
                    SystemStatusInstruction())
                    continue;
                unsigned char op[2] = {};
                const bool fetched = !Read(next, op, 2);
                // CLI and STI of a protected-mode application go on, as in
                // the ptrace mode (host_native_cpu.cpp).
                if (fetched && bridge.trap == 13 && (op[0] == 0xfa || op[0] == 0xfb))
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
                if (interrupt && bridge.trap < 32)
                {
                    interrupt(*this, unsigned(bridge.trap), interrupt_context);
                    continue;
                }
                return CpuError::Exception;
            }
            const unsigned address = unsigned(bridge.address);
            if (InDevice(address))
            {
                if (DeviceInstruction())
                    continue;
                return CpuError::Device;
            }
            const bool fetch = bridge.error & 16, write = bridge.error & 2,
                       mapped = rights[address / Page] & Mapped;
            const unsigned kind = fetch   ? (mapped ? CpuFault::FetchProtected : CpuFault::FetchUnmapped)
                                  : write ? (mapped ? CpuFault::WriteProtected : CpuFault::WriteUnmapped)
                                          : (mapped ? CpuFault::ReadProtected : CpuFault::ReadUnmapped);
            if (fault && fault(*this, kind, address, 1, fault_context))
                continue;
            return fetch   ? (mapped ? CpuError::FetchProtected : CpuError::FetchUnmapped)
                   : write ? (mapped ? CpuError::WriteProtected : CpuError::WriteUnmapped)
                           : (mapped ? CpuError::ReadProtected : CpuError::ReadUnmapped);
        }
        return CpuError::Ok;
    }
};
unsigned MakeDirect(Cpu *&out)
{
    auto *cpu = new (std::nothrow) DirectCpu;
    if (!cpu)
        return CpuError::NoMemory;
    const unsigned error = cpu->Open();
    if (error)
    {
        delete cpu;
        return error;
    }
    out = cpu;
    return CpuError::Ok;
}
