// SPDX-License-Identifier: GPL-2.0-or-later
// Engine-independent CPU selection and error reporting.
#include "x86_cpu.h"
#include "x86_address_space.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#include <stdio.h>
#endif
namespace X86
{
#ifndef NEO98_NO_UNICORN
unsigned CreateUnicornCpu(Cpu *&cpu);
#endif
Cpu::~Cpu()
{
    while (image_data_groups)
    {
        auto *group = image_data_groups;
        image_data_groups = group->next;
        delete group;
    }
}
void Cpu::RememberImageData(unsigned address, unsigned read_only)
{
    for (auto *group = image_data_groups; group; group = group->next)
        if (group->base == address) { group->read_only |= read_only; return; }
    image_data_groups = new ImageDataGroup{address, read_only, image_data_groups};
}
unsigned Cpu::RelaxImageData(unsigned address, unsigned read_only)
{
    if (ImageDataPageSize() != 16384 || image_data_watched)
        return CpuError::Ok;
    if ((address & 16383) || !read_only || read_only >= 15)
        return CpuError::Argument;
    for (unsigned page = 0; page < 4; ++page)
        if (read_only & (1u << page))
        {
            const unsigned error = Protect(address + page * 4096, 4096, Prot::Read | Prot::Write);
            if (error) return error;
        }
    RememberImageData(address, read_only);
    return CpuError::Ok;
}
unsigned Cpu::RevokeImageData(unsigned address, unsigned long long size)
{
    if (!size) return CpuError::Ok;
    const auto begin = static_cast<unsigned long long>(address & ~16383u);
    const auto end = (static_cast<unsigned long long>(address) + size + 16383) & ~16383ull;
    for (auto **link = &image_data_groups; *link;)
    {
        auto *group = *link;
        if (group->base < begin || group->base >= end) { link = &group->next; continue; }
        // Remove it before calling Protect, which itself revokes exceptions.
        *link = group->next;
        const unsigned base = group->base, mask = group->read_only;
        delete group;
        for (unsigned page = 0; page < 4; ++page)
            if (mask & (1u << page))
            {
                const unsigned error = Protect(base + page * 4096, 4096, Prot::Read);
                if (error) return error;
            }
    }
    return CpuError::Ok;
}
unsigned Cpu::ImageDataWriteWatch(bool active)
{
    image_data_watched = active;
    return active ? RevokeImageData(0, 1ull << 32) : unsigned(CpuError::Ok);
}
bool Cpu::SavedRegions(RegionVisitor visitor, void *context)
{
    struct Walk { Cpu &cpu; RegionVisitor visitor; void *context; } walk{*this, visitor, context};
    return Regions([](void *opaque, unsigned address, unsigned size, unsigned prot, void *host) {
        auto &w = *static_cast<Walk *>(opaque);
        const auto end = static_cast<unsigned long long>(address) + size;
        for (unsigned long long at = address; at < end;)
        {
            auto stop = end;
            unsigned flags = prot;
            for (auto *group = w.cpu.image_data_groups; group; group = group->next)
                for (unsigned page = 0; page < 4; ++page)
                    if (group->read_only & (1u << page))
                    {
                        const auto start = static_cast<unsigned long long>(group->base) + page * 4096;
                        if (at >= start && at < start + 4096)
                        {
                            flags |= SavedImageReadOnly;
                            if (stop > start + 4096) stop = start + 4096;
                        }
                        else if (at < start && stop > start) stop = start;
                    }
            w.visitor(w.context, (unsigned)at, (unsigned)(stop - at), flags,
                      host ? static_cast<unsigned char *>(host) + (at - address) : nullptr);
            at = stop;
        }
    }, &walk);
}
unsigned Cpu::MapSaved(unsigned address, unsigned size, unsigned prot)
{
    if ((prot & SavedImageReadOnly) &&
        (ImageDataPageSize() != 16384 || prot != (SavedImageReadOnly | Prot::Read | Prot::Write)))
        return CpuError::Argument;
    const unsigned error = Map(address, size, prot & ~SavedImageReadOnly);
    if (!error && (prot & SavedImageReadOnly))
        for (unsigned long long at = address; at < static_cast<unsigned long long>(address) + size; at += 4096)
            RememberImageData((unsigned)at & ~16383u, 1u << ((at >> 12) & 3));
    return error;
}
const char *CpuErrorText(unsigned error)
{
    switch (error)
    {
    case CpuError::Ok: return "OK (UC_ERR_OK)";
    case CpuError::NoMemory: return "No memory available or memory not present (UC_ERR_NOMEM)";
    case CpuError::Arch: return "Invalid/unsupported architecture (UC_ERR_ARCH)";
    case CpuError::Handle: return "Invalid handle (UC_ERR_HANDLE)";
    case CpuError::Mode: return "Invalid mode (UC_ERR_MODE)";
    case CpuError::Version: return "Different API version between core & binding (UC_ERR_VERSION)";
    case CpuError::ReadUnmapped: return "Invalid memory read (UC_ERR_READ_UNMAPPED)";
    case CpuError::WriteUnmapped: return "Invalid memory write (UC_ERR_WRITE_UNMAPPED)";
    case CpuError::FetchUnmapped: return "Invalid memory fetch (UC_ERR_FETCH_UNMAPPED)";
    case CpuError::Hook: return "Invalid hook type (UC_ERR_HOOK)";
    case CpuError::InvalidInstruction: return "Invalid instruction (UC_ERR_INSN_INVALID)";
    case CpuError::Map: return "Invalid memory mapping (UC_ERR_MAP)";
    case CpuError::WriteProtected: return "Write to write-protected memory (UC_ERR_WRITE_PROT)";
    case CpuError::ReadProtected: return "Read from non-readable memory (UC_ERR_READ_PROT)";
    case CpuError::FetchProtected: return "Fetch from non-executable memory (UC_ERR_FETCH_PROT)";
    case CpuError::Argument: return "Invalid argument (UC_ERR_ARG)";
    case CpuError::ReadUnaligned: return "Read from unaligned memory (UC_ERR_READ_UNALIGNED)";
    case CpuError::WriteUnaligned: return "Write to unaligned memory (UC_ERR_WRITE_UNALIGNED)";
    case CpuError::FetchUnaligned: return "Fetch from unaligned memory (UC_ERR_FETCH_UNALIGNED)";
    case CpuError::HookExists: return "Hook for this event already existed (UC_ERR_HOOK_EXIST)";
    case CpuError::Resource: return "Insufficient resource (UC_ERR_RESOURCE)";
    case CpuError::Exception: return "Unhandled CPU exception (UC_ERR_EXCEPTION)";
    case CpuError::Overflow: return "Provided buffer is too small (UC_ERR_OVERFLOW)";
    default: return "Unknown error code";
    }
}

namespace
{
unsigned long long counter_pause = 0;
}
void SetCounterPause(unsigned long long microseconds)
{
    __atomic_store_n(&counter_pause, microseconds, __ATOMIC_RELAXED);
#if defined(__APPLE__) || defined(__linux__)
    // Unicorn answers RDTSC from its clock in nanoseconds; the patched engine
    // of the host takes this much off (tools/build_unicorn_host.py, "tscpause").
    // FEXCore reads CounterPause before every run (tools/host_fex_cpu.cpp).
    static const auto set = reinterpret_cast<void (*)(uint64_t)>(dlsym(RTLD_DEFAULT, "uc_neo98_tsc_pause"));
    if (set)
        set(microseconds * 1000);
#endif
}
unsigned long long CounterPause() { return __atomic_load_n(&counter_pause, __ATOMIC_RELAXED); }
namespace
{
CpuFactory fex_factory = nullptr;
CpuFactory arm64_factory = nullptr;
CpuFactory native_factory = nullptr;
}
void RegisterCpuBackend(CpuBackend backend, CpuFactory factory)
{
    if (backend == CpuBackend::Native)
        native_factory = factory;
    if (backend == CpuBackend::Fex)
        fex_factory = factory;
    // The processor of ARM64 processes registers itself where it is linked
    // (src/arm64_cpu.cpp); a runner without it has no such processes.
    if (backend == CpuBackend::Arm64)
        arm64_factory = factory;
}
CpuBackend ProcessCpuBackend()
{
    const char *wanted = getenv("NEO98_CPU");
    if (wanted && !strcmp(wanted, "unicorn"))
        return CpuBackend::Unicorn;
    if (wanted && !strcmp(wanted, "fex"))
        return CpuBackend::Fex;
    if (wanted && !strcmp(wanted, "native"))
        return CpuBackend::Native;
    return native_factory ? CpuBackend::Native : fex_factory ? CpuBackend::Fex : CpuBackend::Unicorn;
}
unsigned CreateCpu(CpuBackend backend, Cpu *&cpu, const char *purpose)
{
    cpu = nullptr;
#if defined(__APPLE__) || defined(__linux__)
    static const bool trace = getenv("NEO98_TRACE_CPU") != nullptr;
    if (trace)
        fprintf(stderr, "CPU engine=%s purpose=%s\n",
                backend == CpuBackend::Native ? "native" : backend == CpuBackend::Fex ? "fex" : backend == CpuBackend::None ? "none" : "unicorn",
                purpose ? purpose : "unnamed");
#else
    (void)purpose;
#endif
    if (backend == CpuBackend::Native)
        return native_factory ? native_factory(cpu) : (unsigned)CpuError::Arch;
    if (backend == CpuBackend::Fex)
        return fex_factory ? fex_factory(cpu) : (unsigned)CpuError::Arch;
    if (backend == CpuBackend::Arm64)
        return arm64_factory ? arm64_factory(cpu) : (unsigned)CpuError::Arch;
    if (backend == CpuBackend::None)
        return CreateAddressSpace(cpu);
    if (backend != CpuBackend::Unicorn)
        return CpuError::Arch;
#ifdef NEO98_NO_UNICORN
    return CpuError::Arch;
#else
    return CreateUnicornCpu(cpu);
#endif
}
} // namespace X86
