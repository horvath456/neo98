// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 process-memory public decisions with the existing address-space provider.
bool Application::InitializeCpuDescription()
{
    // Before loading the application or installing hooks, ask the same x86 CPU
    // that will execute CPUID in the guest. No host ARM CPU identity leaks out.
    constexpr unsigned address = 0x10000;
    const unsigned char query[] = {0xb8, 1, 0, 0, 0, 0x0f, 0xa2};
    const unsigned char vendor[] = {0x31, 0xc0, 0x0f, 0xa2};
    if (!Check(cpu->Map(address, 4096, Prot::All))) return false;
    unsigned words[3] = {};
    const bool ok = Check(cpu->Write(address, query, sizeof query)) &&
        Check(cpu->Run(address, address + sizeof query, 2)) &&
        Check(cpu->Get(Reg::Eax, cpu_signature)) &&
        Check(cpu->Write(address, vendor, sizeof vendor)) &&
        Check(cpu->Run(address, address + sizeof vendor, 2)) &&
        Check(cpu->Get(Reg::Ebx, words[0])) && Check(cpu->Get(Reg::Edx, words[1])) &&
        Check(cpu->Get(Reg::Ecx, words[2]));
    if (ok)
        DescribeProcessor(words);
    return Check(cpu->Unmap(address, 4096)) && ok;
}
// KERNEL32, called once while the system starts :
// When CPUID is present, it creates
// HKLM\Hardware\Description\System\CentralProcessor\0 and sets
// VendorIdentifier to the twelve bytes of CPUID leaf 0 in EBX, EDX, ECX order,
// REG_SZ without a terminating zero (Windows 98 answers 0Ch as the size). SYSDM
// reads it, and only when the key exists does it write "Identifier" itself.
// This port has no system start of its
// own, so the first process of a run writes it, and later ones only when the
// value in the store is different, which keeps the journal to one entry.
void Application::DescribeProcessor(const unsigned *words)
{
    char text[12];
    memcpy(text, words, sizeof text);
    auto &store = registry.store;
    unsigned key = 0;
    bool created = false;
    if (store.Open(store.roots[2], "Hardware\\Description\\System\\CentralProcessor\\0", true, key, created))
        return;
    const Registry::Value *value = nullptr;
    if (!store.Query(key, "VendorIdentifier", value) && value && value->type == 1 && value->size == sizeof text &&
        !memcmp(value->data, text, sizeof text))
        return;
    store.Set(key, "VendorIdentifier", 1, text, sizeof text);
}
unsigned Application::ProcessMemoryApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    API_CASE("GetSystemInfo") {
        // KERNEL32. Win98 reports one processor even on SMP
        // hardware; family/model/stepping describe our actual emulated CPU.
        const unsigned family = (cpu_signature >> 8) & 15;
        const unsigned revision = ((cpu_signature & 0xf0) << 4) | (cpu_signature & 15);
        const unsigned data[] = {0, 4096, 0x400000, 0x7fffffff, 1, 1,
            family > 4 ? 586u : family == 4 ? 486u : 386u, 65536, family | (revision << 16)};
        Store(a[0], data, sizeof data);
        return 0;
    }
    API_CASE("IsDebuggerPresent") {
        return debugger_process != 0;
    }
    API_CASE("GetCommandLineW") {
        // Windows 98 allocates once and converts the process's ANSI command line
        // with CP_ACP/MB_PRECOMPOSED. The returned buffer belongs to the process.
        if (!command_line_w) {
            const unsigned query[] = {0, 1, command_line, ~0u, 0, 0};
            const unsigned length = NlsApi(594, query);
            if (!length) return 0;
            const unsigned buffer = Allocate(length * 2);
            if (!buffer) { last_error = 8; return 0; }
            const unsigned convert[] = {0, 1, command_line, ~0u, buffer, length};
            if (!NlsApi(594, convert)) { Free(buffer); return 0; }
            command_line_w = buffer;
        }
        return command_line_w;
    }
    API_CASE("VirtualUnlock") {
        // Windows 98 uses wrapping DWORD addition and leaves LastError alone.
        // The native backing pages are resident; no pager lock is held here.
        return a[0] >= 0x400000 && a[0] + a[1] <= 0x7fffffff;
    }
    API_CASE("FlushInstructionCache") {
        // is unconditional TRUE on x86. The translated backend also
        // invalidates generated blocks, while retaining that public contract.
        auto *target = ProcessFromHandle(a[0]);
        if (target && !target->Check(target->cpu->FlushCode())) return 0;
        return 1;
    }
    default:
        break;
    }
    const bool read = API_IS(name,"ReadProcessMemory");
    const bool write = API_IS(name,"WriteProcessMemory");
    if ((read || write) && a[4]) {
        if (!virtual_memory.Access(a[4], 4, true)) { last_error = 87; return 0; }
        Write32(a[4], 0);
    }
    // WriteProcessMemory checks the caller's source before resolving the
    // process; ReadProcessMemory checks the destination after resolving it.
    if (write && (a[2] < 0x400000 || a[2] > 0xc0000000)) { last_error = 87; return 0; }
    unsigned protect_flags = 0;
    if (API_IS(name,"VirtualProtectEx")) {
        if (a[2] > 0x7fc00000 || a[1] > 0xbfffffff) { last_error = 487; return 0; }
        if (a[4] && !virtual_memory.Access(a[4], 4, true)) { last_error = 87; return 0; }
        MemoryBackend validation(*this);
        protect_flags = Win98Virtual::Protection(validation, a[3]);
        if (protect_flags == ~0u) return 0;
    }
    auto *target = ProcessFromHandle(a[0]);
    if (!target) { last_error = 6; return 0; }
    if (API_IS(name,"GetProcessAffinityMask")) {
        // Windows 98 writes system then process mask; both are one on Win98.
        if (!virtual_memory.Access(a[1], 4, true) || !virtual_memory.Access(a[2], 4, true)) {
            last_error = 87; return 0;
        }
        Write32(a[2], 1); Write32(a[1], 1);
        return 1;
    }
    if (API_IS(name,"VirtualQueryEx")) {
        VirtualMemory::Info info{};
        if (a[1] >= 0xc0000000 || a[3] < sizeof info || !target->virtual_memory.Query(a[1], info)) return 0;
        if (!virtual_memory.Access(a[2], sizeof info, true)) { last_error = 87; return 0; }
        return Store(a[2], &info, sizeof info) ? sizeof info : 0;
    }
    if (API_IS(name,"VirtualProtectEx")) {
        MemoryBackend backend(*target, *this);
        const unsigned before = target->virtual_memory.Protect(backend, a[1], a[2], protect_flags);
        if (before == ~0u) { last_error = 87; return 0; }
        const unsigned previous = Win98Virtual::PageProtection(before);
        if (a[4]) Write32(a[4], previous);
        return previous; // Original BOOL return is the previous PAGE_* value.
    }
    if (read || write) {
        if (read && (a[2] < 0x400000 || a[2] > 0xc0000000)) { last_error = 87; return 0; }
        if (target->state == Exited || target->state == Failed) { last_error = 1067; return 0; }
        Application &source = write ? *this : *target, &destination = write ? *target : *this;
        const unsigned from = write ? a[2] : a[1], to = write ? a[1] : a[2], size = a[3];
        // Check both complete spans before mutation. Host uc_mem_write bypasses
        // page permissions, so checking the guest provider is mandatory.
        if (!source.virtual_memory.Access(from, size, false) ||
            !destination.virtual_memory.Access(to, size, true)) { last_error = 299; return 0; }
        unsigned char buffer[4096];
        const bool backward = &source == &destination && to > from && to - from < size;
        for (unsigned done = 0; done < size;) {
            unsigned bytes = size - done;
            if (bytes > sizeof buffer) bytes = sizeof buffer;
            const unsigned offset = backward ? size - done - bytes : done;
            if (source.cpu->Read(from + offset, buffer, bytes) != CpuError::Ok ||
                destination.cpu->Write(to + offset, buffer, bytes) != CpuError::Ok) {
                if (a[4]) Write32(a[4], done);
                last_error = 299; return 0;
            }
            done += bytes;
        }
        if (a[4]) Write32(a[4], size);
        return 1;
    }
    Fail(name);
    return 0;
}
