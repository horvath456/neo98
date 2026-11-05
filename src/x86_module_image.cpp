// SPDX-License-Identifier: GPL-2.0-or-later
// Expose a readable PE header page for each native service module. Programs
// may inspect headers and exports directly instead of using GetProcAddress.
// Import names and ordinals from the user's DLLs and point entries to the
// same native adapters used by GetProcAddress. Adapter RVAs wrap from the
// module handle to the lower gate region and remain outside the export
// directory, so walkers recognize functions rather than forwarders.
#include "x86_app.h"
#include <stdio.h>
#include <string.h>

namespace X86
{
namespace
{
#include "interface_native_module_exports.h"
#include "interface_export_contract.h"

const char *Leaf(const char *name)
{
    const char *result = name;
    for (const char *p = name; *p; ++p)
        if (*p == '\\' || *p == '/')
            result = p + 1;
    return result;
}
bool SameName(const char *a, const char *b)
{
    while (*a || *b)
    {
        unsigned x = static_cast<unsigned char>(*a++), y = static_cast<unsigned char>(*b++);
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return false;
    }
    return true;
}

void Put16(unsigned char *at, unsigned value)
{
    at[0] = value & 255;
    at[1] = (value >> 8) & 255;
}
void Put32(unsigned char *at, unsigned value)
{
    Put16(at, value);
    Put16(at + 2, value >> 16);
}
// The page is one section of initialized, readable data behind 4 KiB of
// headers. Both alignments are the page size, so the file view and the memory
// view of the export directory are the same.
constexpr unsigned HeaderBytes = 0x1000, PeOffset = 0x80;
// A data export is not a function. Windows 98 answers its name with the address
// of the block itself, so the page carries one zeroed slot per data export at
// its front and the export directory begins behind them. Zero is what an
// uninitialised block holds; a connector that checks its signature fails, which
// is the honest answer and not a pretended success.
constexpr unsigned ExportDataRva = 0x1000, ExportDataSlot = 64;
// One collected export of a module, in the order the export directory wants.
struct HeaderExport
{
    const char *name;
    unsigned ordinal;
};
// Every export of a natively provided DLL, or, for a provider the contract does
// not cover, the names the runtime registers. Returns the number collected.
unsigned CollectExports(const char *leaf, HeaderExport *out, unsigned capacity)
{
    unsigned count = 0;
    bool contract = false;
    for (const auto &alias : ExportContractAliases)
        if (SameName(alias.dll, leaf)) { contract = true; break; }
    auto add = [&](const char *name, unsigned ordinal) {
        if (count == capacity)
            return;
        // Ascending by name, as the PE contract wants for the binary search.
        // An export without a name sorts to the end and gets no name entry.
        unsigned at = count++;
        while (at && (!out[at - 1].name ||
                      (name && strcmp(out[at - 1].name, name) > 0)))
        {
            out[at] = out[at - 1];
            --at;
        }
        out[at] = {name, ordinal};
    };
    if (SameName(leaf, "NEO98TOUCH.DLL")) {
        add("Neo98TouchGetCaps", 1);
        add("Neo98TouchRegister", 2);
        add("Neo98TouchUnregister", 3);
        add("Neo98TouchRead", 4);
    }
    else if (contract)
    {
        for (const auto &alias : ExportContractAliases)
            if (SameName(alias.dll, leaf))
                add(alias.name, alias.ordinal);
    }
    else
    {
        for (const auto &alias : NativeExportAliases)
            if (SameName(alias.dll, leaf))
                add(alias.name, alias.ordinal);
    }
    return count;
}
// The page RVA of a data export of this module, or zero. The order is the order
// of the generated table, so the address never depends on runtime state.
unsigned DataSlotRva(const char *leaf, const char *name, unsigned ordinal)
{
    unsigned slot = 0;
    for (const auto &entry : ExportContractData)
    {
        if (!SameName(entry.dll, leaf))
            continue;
        if ((name && entry.name && !strcmp(name, entry.name)) || (!name && ordinal == entry.ordinal))
            return ExportDataRva + slot * ExportDataSlot;
        ++slot;
    }
    return 0;
}
constexpr unsigned HeaderExportCapacity =
    sizeof ExportContractAliases / sizeof ExportContractAliases[0] +
    sizeof NativeExportAliases / sizeof NativeExportAliases[0];
} // namespace

unsigned Application::ContractDataAddress(const char *dll, const char *name, unsigned ordinal)
{
    if (!dll)
        return 0;
    const char *leaf = Leaf(dll);
    unsigned slot = 0;
    for (const auto &entry : ExportContractData)
    {
        if (!SameName(entry.dll, leaf))
            continue;
        if ((name && entry.name && !strcmp(name, entry.name)) ||
            (!name && ordinal && ordinal == entry.ordinal))
        {
            for (unsigned i = 0; i < ModuleCapacity; ++i)
            {
                const auto &m = modules[i];
                if (m.kind == 2 && m.header_bytes && SameName(Leaf(m.path), leaf))
                    return m.handle + ExportDataRva + slot * ExportDataSlot;
            }
            return 0;
        }
        ++slot;
    }
    return 0;
}

bool Application::CreateModuleHeader(unsigned index)
{
    if (!cpu || index >= ModuleCapacity)
        return false;
    auto &m = modules[index];
    if (m.kind != 2 || m.header_bytes)
        return false;
    const char *leaf = Leaf(m.path);
    // MSVCRT is the one provider whose exports are not all gates: _pctype,
    // __lc_handle and the CRT time and argument blocks are data, and looking
    // one of them up creates its storage. Filling a table eagerly would
    // therefore allocate the locale block before the program asks for it, and
    // a program that watches localeconv fail under memory pressure would see
    // it succeed. MSVCRT runs as original code under the DLL rule anyway and
    // is a native provider only in fixtures, so it keeps no header page.
    if (SameName(leaf, "MSVCRT.DLL"))
        return false;
    // Collect the exports of this module, sorted by name. The PE contract wants
    // AddressOfNames in ascending byte order so that a binary search works.
    auto *collected = new HeaderExport[HeaderExportCapacity];
    const unsigned entries = CollectExports(leaf, collected, HeaderExportCapacity);
    unsigned names = 0, base = 0, top = 0, strings = 0;
    for (unsigned i = 0; i < entries; ++i)
    {
        const unsigned ordinal = collected[i].ordinal;
        if (!base || ordinal < base) base = ordinal;
        if (ordinal > top) top = ordinal;
        if (!collected[i].name)
            continue;
        ++names;
        strings += unsigned(strlen(collected[i].name)) + 1;
    }
    if (!entries)
    {
        delete[] collected;
        return false;
    }
    unsigned data_slots = 0;
    for (const auto &entry : ExportContractData)
        if (SameName(entry.dll, leaf))
            ++data_slots;
    const unsigned functions = top - base + 1;
    const unsigned directory = ExportDataRva + data_slots * ExportDataSlot,
                   table = directory + 40, name_table = table + functions * 4,
                   ordinal_table = name_table + names * 4, module_name = ordinal_table + names * 2,
                   name_strings = module_name + unsigned(strlen(leaf)) + 1;
    const unsigned export_bytes = name_strings + strings - directory;
    const unsigned image = (directory + export_bytes + 4095) & ~4095u;
    // The handle sits in a permanent native reservation; a module index beyond
    // it would map into the guest arena, so the page is skipped and the module
    // keeps working without an image.
    const bool reserved = (m.handle >= 0x71e00000 && m.handle + image <= 0x72200000) ||
                          (m.handle == Win98KernelBase && image <= 0x10000);
    if (!reserved)
    {
        if (log)
        {
            char text[256];
            snprintf(text, sizeof text, "MODULE HEADER skipped module=%s handle=%08x bytes=%u", leaf, m.handle, image);
            log(text);
        }
        delete[] collected;
        return false;
    }
    unsigned char *page = new unsigned char[image]();
    // DOS header. Only the signature and e_lfanew carry meaning; the rest keeps
    // the shape a walker expects.
    page[0] = 'M';
    page[1] = 'Z';
    Put16(page + 2, 0x90);
    Put16(page + 4, 3);
    Put16(page + 8, 4);
    Put16(page + 10, 0xffff);
    Put16(page + 16, 0xb8);
    Put16(page + 24, 0x40);
    Put32(page + 0x3c, PeOffset);
    if (m.handle == Win98KernelBase)
    {
        // The place a process entry returns to: inside the image of KERNEL32,
        // as BaseProcessStart is under Win98, and from there to the exit gate.
        // The DOS stub area between the header and e_lfanew is free for it.
        const unsigned at = ProcessReturnOffset, target = ProcessExitGate;
        page[at] = 0xe9;
        Put32(page + at + 1, target - (m.handle + at + 5));
    }
    unsigned char *pe = page + PeOffset;
    memcpy(pe, "PE\0\0", 4);
    Put16(pe + 4, 0x14c);   // i386
    Put16(pe + 6, 1);       // one section
    Put16(pe + 20, 0xe0);   // SizeOfOptionalHeader
    Put16(pe + 22, 0x2103); // stripped, executable, 32 bit, DLL
    unsigned char *opt = pe + 24;
    Put16(opt, 0x10b); // PE32
    opt[2] = 5;
    Put32(opt + 4, image - HeaderBytes);  // SizeOfCode
    Put32(opt + 8, image - HeaderBytes);  // SizeOfInitializedData
    Put32(opt + 20, ExportDataRva);       // BaseOfCode
    Put32(opt + 24, ExportDataRva);       // BaseOfData
    Put32(opt + 28, m.handle);            // Image
    Put32(opt + 32, HeaderBytes);         // SectionAlignment
    Put32(opt + 36, HeaderBytes);         // FileAlignment
    Put16(opt + 40, 4);                   // MajorOperatingSystemVersion
    Put16(opt + 48, 4);                   // MajorSubsystemVersion
    Put32(opt + 56, image);               // SizeOfImage
    Put32(opt + 60, HeaderBytes);         // SizeOfHeaders
    Put16(opt + 68, 2);                   // Subsystem: GUI
    Put32(opt + 72, 0x100000);            // SizeOfStackReserve
    Put32(opt + 76, 0x1000);              // SizeOfStackCommit
    Put32(opt + 80, 0x100000);            // SizeOfHeapReserve
    Put32(opt + 84, 0x1000);              // SizeOfHeapCommit
    Put32(opt + 92, 16);                  // NumberOfRvaAndSizes
    Put32(opt + 96, directory);           // export directory RVA
    Put32(opt + 100, export_bytes);
    unsigned char *section = opt + 0xe0;
    memcpy(section, ".rdata", 6);
    Put32(section + 8, image - HeaderBytes);
    Put32(section + 12, ExportDataRva);
    Put32(section + 16, image - HeaderBytes);
    Put32(section + 20, ExportDataRva);
    Put32(section + 36, 0x40000040); // initialized data, readable
    unsigned char *e = page + directory;
    Put32(e + 12, module_name);
    Put32(e + 16, base);
    Put32(e + 20, functions);
    Put32(e + 24, names);
    Put32(e + 28, table);
    Put32(e + 32, name_table);
    Put32(e + 36, ordinal_table);
    memcpy(page + module_name, leaf, strlen(leaf) + 1);
    unsigned at = name_strings, published = 0, functions_published = 0;
    // Every gate minted below belongs to the table, not to the program; the
    // import census tells the two apart by this.
    ++building_module_header;
    for (unsigned i = 0; i < entries; ++i)
    {
        const auto &alias = collected[i];
        unsigned address = 0;
        // A data export answers with the block in this page itself, so its RVA
        // is positive and it never gets a gate.
        if (const unsigned data = DataSlotRva(leaf, alias.name, alias.ordinal))
            address = m.handle + data;
        // Otherwise the same call GetProcAddress makes, with the same
        // allow-missing rule, so the table cannot promise an address the public
        // API refuses.
        else if (!NativeProcedure(leaf, alias.name, alias.name ? 0 : alias.ordinal, &address, false) ||
                 !address)
            continue;
        ++functions_published;
        Put32(page + table + (alias.ordinal - base) * 4, address - m.handle);
        if (!alias.name)
            continue;
        const unsigned length = unsigned(strlen(alias.name)) + 1;
        memcpy(page + at, alias.name, length);
        Put32(page + name_table + published * 4, at);
        Put16(page + ordinal_table + published * 2, (unsigned short)(alias.ordinal - base));
        ++published;
        at += length;
    }
    --building_module_header;
    delete[] collected;
    Put32(e + 24, published);
    if (!published && !functions_published)
    {
        delete[] page;
        return false;
    }
    if (!Map(m.handle, image, 1))
    {
        delete[] page;
        return false;
    }
    ++loader_writes;
    const bool stored = Store(m.handle, page, image);
    --loader_writes;
    delete[] page;
    if (!stored)
    {
        Unmap(m.handle, image);
        return false;
    }
    m.header_bytes = image;
    return true;
}

void Application::ReleaseModuleHeader(unsigned index)
{
    auto &m = modules[index];
    if (!m.header_bytes)
        return;
    if (cpu)
        Unmap(m.handle, m.header_bytes);
    m.header_bytes = 0;
}
} // namespace X86
