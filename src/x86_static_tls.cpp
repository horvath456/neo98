// SPDX-License-Identifier: GPL-2.0-or-later
// Static thread-local storage from PE directory 9. Assign indexes when modules
// load, allocate each thread's blocks, copy initial data, and release the blocks
// when the thread exits.
#include "x86_app.h"
#include <stdio.h>
namespace X86
{
// IMAGE_TLS_DIRECTORY32 fields at +0, +4, +8, +0Ch, +10h, +14h. Win98 reads
// StartAddressOfRawData, EndAddressOfRawData, AddressOfIndex and SizeOfZeroFill
// and never touches AddressOfCallBacks, Characteristics or the directory size.
// The four addresses are virtual addresses and carry base relocations, so they
// are already correct for the loaded image.
bool Application::RegisterStaticTls(unsigned module)
{
    auto &m = modules[module];
    if (!m.pe || !m.pe->HasTls())
        return true;
    if (static_tls_count == 32)
    {
        Fail("More static TLS modules than the loader supports");
        return false;
    }
    const unsigned directory = m.pe->TlsDirectory();
    // An ARM64 image has IMAGE_TLS_DIRECTORY64: the same fields, the four
    // addresses with 64 bits (+0, +8, +10h, +18h, then SizeOfZeroFill at
    // +20h). The image lies below 4 GiB, so the low halves are the addresses.
    const bool wide = m.pe->Arm64();
    const unsigned start = Read32(directory), end = Read32(directory + (wide ? 8 : 4));
    const unsigned index_address = Read32(directory + (wide ? 16 : 8)), zero_fill = Read32(directory + (wide ? 32 : 16));
    if (state == Failed)
        return false;
    // The original computes (End - Start) + SizeOfZeroFill without any check
    // and passes it to the heap. Its heap refuses more than 0FFFFF98h bytes;
    // a reversed or absurd template therefore fails there, not here.
    const unsigned raw_size = end - start;
    if (end < start || raw_size > 0x0fffff98 || zero_fill > 0x0fffff98 - raw_size)
    {
        Fail("Static TLS template exceeds the Win98 heap limit");
        return false;
    }
    // Win64 counts the static TLS modules of a process from zero, apart from
    // the slots of TlsAlloc: the index selects a pointer in the array the TEB
    // names at +58h (StartThreadStaticTls).
    const unsigned index = wide ? static_tls_count : AllocateTlsSlot();
    if (index == ~0u)
    {
        // an exhausted slot pool tears the process down. The 80
        // slots are shared with TlsAlloc.
        Fail("No free TLS slot for a static TLS module");
        return false;
    }
    auto &record = static_tls[static_tls_count++];
    record.module = module;
    record.index = index;
    record.raw = start;
    record.raw_size = raw_size;
    record.zero_fill = zero_fill;
    record.index_address = index_address;
    // publishes the index into the module's own data before any of its
    // code runs. The.data page is writable, but the loader flag keeps this
    // independent of the section attributes the file asks for.
    ++loader_writes;
    Write32(index_address, index);
    --loader_writes;
    if (log)
    {
        char message[320];
        snprintf(message, sizeof message, "Static TLS module %s index=%u raw=%u zero=%u",
                 m.path[0] ? m.path : "process image", index, raw_size, zero_fill);
        log(message);
    }
    return state != Failed;
}
// Runs once, after the static import graph is bound and before any guest code.
// Windows 98 walks the process module list from its head; the EXE is its first
// entry, so the EXE takes the first index.
bool Application::InitializeStaticTls()
{
    if (!RegisterStaticTls(0))
        return false;
    // Nothing is unloaded while the import graph is bound, so the slot numbers
    // in modules are the load order.
    for (unsigned i = 1; i < module_watermark; ++i)
        if (modules[i].kind == 3 && !RegisterStaticTls(i))
            return false;
    return static_tls_count == 0 || StartThreadStaticTls(0);
}
// One process heap block per module and thread: zero filled by the
// allocator, then the template copied over its front.
bool Application::StartThreadStaticTls(unsigned thread)
{
    auto &t = threads[thread];
    const unsigned teb = TebOf(thread);
    for (unsigned i = 0; i < static_tls_count; ++i)
    {
        const auto &record = static_tls[i];
        const unsigned block = ProcessHeapBlock(record.raw_size + record.zero_fill);
        if (!block)
        {
            // Windows 98 answers a message box and fails the caller. A thread
            // without its blocks cannot run guest code at all.
            EndThreadStaticTls(thread);
            last_error = 8;
            return false;
        }
        t.static_tls_blocks[i] = block;
        unsigned char buffer[1024];
        for (unsigned done = 0; done < record.raw_size;)
        {
            const unsigned rest = record.raw_size - done;
            const unsigned count = rest < sizeof buffer ? rest : (unsigned)sizeof buffer;
            if (!Read(record.raw + done, buffer, count) || !Store(block + done, buffer, count))
                return false;
            done += count;
        }
        if (Arm64Process())
        {
            // The compiler's code reads x18+58h (ThreadLocalStoragePointer)
            // and takes the block of its module from the array there, eight
            // bytes per index. The array lies in the 64-bit part of the TEB
            // page (src/arm64_cpu.cpp, Teb64Offset).
            const unsigned array = teb + Arm64TlsArrayOffset;
            Write32(array + record.index * 8, block);
            Write32(array + record.index * 8 + 4, 0);
            Write32(teb + Arm64Teb64Offset + 0x58, array);
            Write32(teb + Arm64Teb64Offset + 0x5c, 0);
        }
        else
            Write32(teb + 0x88 + record.index * 4, block);
        if (state == Failed)
            return false;
    }
    return true;
}
// called after the module's DLL_THREAD_DETACH.
void Application::EndThreadStaticTls(unsigned thread)
{
    auto &t = threads[thread];
    const unsigned teb = TebOf(thread);
    for (unsigned i = 0; i < static_tls_count; ++i)
    {
        if (!t.static_tls_blocks[i])
            continue;
        FreeProcessHeapBlock(t.static_tls_blocks[i]);
        t.static_tls_blocks[i] = 0;
        if (cpu && state != Failed)
        {
            if (Arm64Process()) Write32(teb + Arm64TlsArrayOffset + static_tls[i].index * 8, 0);
            else Write32(teb + 0x88 + static_tls[i].index * 4, 0);
        }
    }
}
} // namespace X86
