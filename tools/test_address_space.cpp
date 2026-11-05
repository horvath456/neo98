// SPDX-License-Identifier: GPL-2.0-or-later
// CpuBackend::None, the address space without a processor of the DOS process
// (src/x86_address_space.h): its memory answers like an
// engine's, its processor calls are refused, its contexts are empty, and
// CopyAddressSpace hands its pages to a Unicorn processor that then runs code
// out of them, reads the copied bytes, shares the caller bytes of a MapHost
// range with the host and keeps the rights of every page.
// Build and run: tools/test_address_space.py.
#include "../src/x86_address_space.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using X86::CpuError;
using X86::Prot;
static unsigned checks = 0;
static void require(bool condition, const char *what)
{
    if (!condition)
    {
        printf("ADDRESS SPACE FAIL %s\n", what);
        exit(1);
    }
    ++checks;
}
static bool Fault(X86::Cpu &, unsigned, unsigned, unsigned, void *context)
{
    ++*static_cast<unsigned *>(context);
    return false;
}
int main()
{
    X86::Cpu *space = nullptr;
    require(X86::CreateCpu(X86::CpuBackend::None, space) == CpuError::Ok && space, "create");
    require(space->Backend() == X86::CpuBackend::None && !space->gpr && !space->eip, "no registers in place");

    // Memory like an engine's: zeroed pages, reads and writes across pages,
    // the refusals of Unicorn for a second map and for holes.
    require(space->Map(0x10000, 0x3000, Prot::Read | Prot::Write) == CpuError::Ok, "map");
    unsigned char zero[16] = {}, bytes[16];
    require(space->Read(0x10ff8, bytes, 16) == CpuError::Ok && !memcmp(bytes, zero, 16), "fresh pages are zero");
    const char text[] = "across a page boundary";
    require(space->Write(0x10ff0, text, sizeof text) == CpuError::Ok, "write across pages");
    char back[sizeof text] = {};
    require(space->Read(0x10ff0, back, sizeof back) == CpuError::Ok && !strcmp(back, text), "read across pages");
    require(space->Map(0x11000, 0x1000, Prot::Read) == CpuError::Map, "a mapped page cannot be mapped again");
    require(space->Read(0x13000, bytes, 1) == CpuError::ReadUnmapped, "read of an unmapped page");
    require(space->Write(0x12ffc, bytes, 8) == CpuError::WriteUnmapped, "write running into an unmapped page");
    require(space->Protect(0x12000, 0x2000, Prot::Read) == CpuError::NoMemory, "protect over a hole");
    require(space->Unmap(0x12000, 0x2000) == CpuError::NoMemory, "unmap over a hole");
    require(space->Map(0x14001, 0x1000, Prot::Read) == CpuError::Argument, "unaligned map");
    require(space->HostPointer(0x10000, 0x1000) == nullptr, "no host pointer");
    // A page out of the middle of a mapping goes back alone.
    require(space->Unmap(0x11000, 0x1000) == CpuError::Ok, "unmap the middle page");
    require(space->Read(0x11000, bytes, 1) == CpuError::ReadUnmapped, "the middle page is gone");
    require(space->Read(0x10ff0, back, 16) == CpuError::Ok && !memcmp(back, text, 16), "the first page stays");
    require(space->Map(0x11000, 0x1000, Prot::Read | Prot::Write) == CpuError::Ok, "map the middle page again");
    require(space->Read(0x11000, bytes, 16) == CpuError::Ok && !memcmp(bytes, zero, 16), "the new middle page is zero");

    // Caller bytes, as the session arena maps them.
    static unsigned char shared[0x2000];
    require(space->MapHost(0x20000, 0x2000, Prot::Read | Prot::Write, shared) == CpuError::Ok, "map host bytes");
    require(space->Write(0x20010, "host", 5) == CpuError::Ok && !memcmp(shared + 0x10, "host", 5), "write reaches the host bytes");

    // Code, and a page without access.
    const unsigned char code[] = {0xa1, 0x00, 0x00, 0x01, 0x00,  // mov eax,[10000h]
                                  0x05, 0x01, 0x00, 0x00, 0x00,  // add eax,1
                                  0xa3, 0x04, 0x00, 0x02, 0x00}; // mov [20004h],eax
    require(space->Map(0x30000, 0x1000, Prot::All) == CpuError::Ok && space->Write(0x30000, code, sizeof code) == CpuError::Ok,
            "code page");
    require(space->Map(0x40000, 0x1000, Prot::None) == CpuError::Ok && space->Protect(0x12000, 0x1000, Prot::Read) == CpuError::Ok,
            "page rights");
    const unsigned value = 41;
    require(space->Write(0x10000, &value, 4) == CpuError::Ok, "data for the code");

    // No processor: registers, runs and hooks are refused; contexts are empty.
    unsigned eax = 7;
    require(space->Get(X86::Reg::Eax, eax) == CpuError::Arch && eax == 0, "no register");
    require(space->Set(X86::Reg::Eax, 1) == CpuError::Arch, "no register write");
    require(space->Run(0x30000, 0x3000f) == CpuError::Arch, "no run");
    unsigned faults = 0;
    require(space->SetFaultHook(Fault, &faults) == CpuError::Arch, "no hooks");
    X86::CpuContext *context = nullptr;
    require(space->NewContext(context) == CpuError::Ok && context && space->Save(context) == CpuError::Ok &&
                space->Restore(context) == CpuError::Ok,
            "empty contexts");
    require(!space->StateRefusal() && !space->ContextStateRefusal(context) && space->ContextBytes() == 8,
            "typed empty state context");
    unsigned long long empty = 0;
    require(space->ContextExport(context, &empty) && empty == 0x31585443454e4f4eull &&
                space->ContextImport(context, &empty), "empty context roundtrip");
    empty ^= 1;
    require(!space->ContextImport(context, &empty), "a foreign empty-context tag is refused");
    require(space->ContextStateRefusal(nullptr) && !space->ContextExport(nullptr, &empty),
            "an invalid empty context is refused");
    struct Runs { unsigned count = 0, end = 0, shared = 0; bool sorted = true, rights = false; } runs;
    require(space->Regions([](void *opaque, unsigned address, unsigned bytes, unsigned prot, void *host) {
        auto &r = *static_cast<Runs *>(opaque);
        r.sorted = r.sorted && address >= r.end;
        r.end = address + bytes; ++r.count;
        if (address == 0x20000 && bytes == 0x2000 && host == shared) ++r.shared;
        if (address == 0x12000 && prot == Prot::Read && !host) r.rights = true;
    }, &runs) && runs.count && runs.sorted && runs.shared == 1 && runs.rights,
            "state regions retain owned pages, shared bytes and rights in order");
    space->FreeContext(context);

    // The hand-over to a processor.
    X86::Cpu *cpu = nullptr;
    require(X86::CreateCpu(X86::CpuBackend::Unicorn, cpu) == CpuError::Ok && cpu, "create Unicorn");
    require(X86::CopyAddressSpace(*space, *cpu) == CpuError::Ok, "copy the address space");
    require(X86::CopyAddressSpace(*cpu, *space) == CpuError::Argument, "only an address space can be copied");
    // The space is unchanged and still answers; it is deleted now, as
    // AttachProcessor does, and its bytes must no longer matter.
    require(space->Read(0x10ff0, back, 16) == CpuError::Ok && !memcmp(back, text, 16), "the space keeps its bytes");
    delete space;
    require(cpu->Read(0x10ff0, back, 16) == CpuError::Ok && !memcmp(back, text, 16), "copied bytes");
    require(cpu->Read(0x11000, bytes, 16) == CpuError::Ok && !memcmp(bytes, zero, 16), "copied zero page");
    require(cpu->Read(0x13000, bytes, 1) != CpuError::Ok, "the hole stays a hole");
    require(cpu->SetFaultHook(Fault, &faults) == CpuError::Ok, "fault hook");
    require(cpu->Run(0x30000, 0x30000 + sizeof code, 10) == CpuError::Ok, "the processor runs the copied code");
    unsigned result = 0;
    require(cpu->Read(0x20004, &result, 4) == CpuError::Ok && result == 42, "the code computed 42");
    memcpy(&result, shared + 4, 4);
    require(result == 42, "the result is in the shared host bytes");
    // Rights came along: a read-only page refuses a guest write, a page
    // without access a guest read.
    const unsigned char store[] = {0xa3, 0x00, 0x20, 0x01, 0x00}; // mov [12000h],eax
    require(cpu->Write(0x30100, store, sizeof store) == CpuError::Ok, "store code");
    require(cpu->Run(0x30100, 0x30105, 10) == CpuError::WriteProtected, "a read-only page stays read-only");
    const unsigned char load[] = {0xa1, 0x00, 0x00, 0x04, 0x00}; // mov eax,[40000h]
    require(cpu->Write(0x30200, load, sizeof load) == CpuError::Ok, "load code");
    require(cpu->Run(0x30200, 0x30205, 10) == CpuError::ReadProtected, "a page without access stays closed");
    delete cpu;
    printf("ADDRESS SPACE PASS checks=%u\n", checks);
    return 0;
}
